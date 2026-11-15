#!/usr/bin/env python3
"""Owned fixtures for a full32-bit index copy scheduled after its unsigned CMP."""

from pathlib import Path
import argparse, json, struct, subprocess, sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--xed-root", type=Path, required=True)
p.add_argument("--llvm-bin", type=Path, required=True)
p.add_argument("--cxx", default="clang++")
p.add_argument("--baseline-source", type=Path)
a = p.parse_args()
out = a.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    p.error("Output must be on a mounted external disk")
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent


def run(command, name):
    with (out / name).open("w") as stream:
        subprocess.run(
            list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, check=True
        )


# head executes before CMP; schedule executes after CMP but before JA.
# All source statements are authored; the generated x86 is never executed.
specs = {
    "ordinary": dict(
        head="mov eax,esi", guard="esi", schedule="nop", ok=True, late=False
    ),
    "late_basic": dict(
        head="", guard="esi", schedule="mov eax,esi", ok=True, late=True
    ),
    "late_stores": dict(
        head="mov ecx,0xc0006900\nmov r12d,esi",
        guard="r12d",
        index="rcx",
        schedule="mov dword ptr [rdi],ecx\nmov ecx,r12d\nmov dword ptr [rdi+4],0x191",
        ok=True,
        late=True,
    ),
    "late_future_base": dict(
        head="mov edx,esi", guard="edx", schedule="mov eax,edx", ok=True, late=True
    ),
    "late_eight": dict(
        head="", guard="esi", schedule="nop\n" * 7 + "mov eax,esi", ok=True, late=True
    ),
    "late_zero": dict(
        head="", guard="esi", bound=0, schedule="mov eax,esi", ok=True, late=True
    ),
    "wrong_source": dict(head="", guard="esi", schedule="mov eax,edi", ok=False),
    "source_before": dict(
        head="", guard="esi", schedule="mov esi,99\nmov eax,esi", ok=False
    ),
    "source_after": dict(
        head="", guard="esi", schedule="mov eax,esi\nmov esi,99", ok=False
    ),
    "source_partial": dict(
        head="", guard="esi", schedule="mov si,99\nmov eax,esi", ok=False
    ),
    "flags_add": dict(
        head="", guard="esi", schedule="mov eax,esi\nadd ebx,1", ok=False
    ),
    "flags_inc": dict(head="", guard="esi", schedule="mov eax,esi\ninc ebx", ok=False),
    "dest64": dict(head="", guard="esi", schedule="mov rax,rsi", ok=False),
    "dest16": dict(head="", guard="esi", schedule="mov ax,si", ok=False),
    "double_copy": dict(
        head="", guard="esi", schedule="mov eax,esi\nmov eax,esi", ok=False
    ),
    "late_nine": dict(
        head="", guard="esi", schedule="nop\n" * 8 + "mov eax,esi", ok=False
    ),
    "narrow_guard": dict(head="", guard="si", schedule="mov eax,esi", ok=False),
    "wide_guard": dict(head="", guard="rsi", schedule="mov eax,esi", ok=False),
    "self_copy": dict(head="", guard="eax", schedule="mov eax,eax", ok=False),
    "late_after_ja": dict(
        head="", guard="esi", schedule="", after="mov eax,esi", ok=False
    ),
    "alternate_copy": dict(
        head="test edi,edi\njnz @COPY",
        guard="esi",
        schedule="@COPY:\nmov eax,esi",
        ok=False,
    ),
    "partial_after": dict(
        head="", guard="esi", schedule="mov eax,esi\nmov al,1", ok=False
    ),
}
text = [".intel_syntax noprefix", ".text", ".org 0x100"]
tables = []
for name, s in specs.items():
    fn = "late_" + name
    idx = s.get("index", "rax")
    guard = s["guard"]
    bound = s.get("bound", 2)
    text += [f".global {fn}", f".global {fn}_end", fn + ":"]
    if s["head"]:
        text += s["head"].replace("@COPY", fn + "_copy").splitlines()
    text += [f"cmp {guard},{bound}"]
    if s["schedule"]:
        text += s["schedule"].replace("@COPY", fn + "_copy").splitlines()
    text += [f"ja {fn}_default"]
    if s.get("after"):
        text += s["after"].splitlines()
    text += [
        f"lea rdx,[rip+{fn}_table]",
        f"movsxd r8,dword ptr [rdx+{idx}*4]",
        "add r8,rdx",
        "jmp r8",
    ]
    for i in range(bound + 1):
        text += [fn + f"_case{i}:", f"mov eax,{i + 1}", "ret"]
    text += [fn + "_default:", "xor eax,eax", "ret", fn + "_end:"]
    tables += [".balign 4", fn + "_table:"] + [
        f".long {fn}_case{i}-{fn}_table" for i in range(bound + 1)
    ]
# A known earlier table can bypass CMP and enter the scheduled copy. Its edge
# must invalidate the late-copy proof, even when direct predecessors look unique.
fn = "late_table_bypass"
text += [
    f".global {fn}",
    f".global {fn}_end",
    fn + ":",
    "mov ecx,edi",
    "cmp rcx,1",
    f"ja {fn}_default",
    f"lea rdx,[rip+{fn}_table0]",
    "movsxd r8,dword ptr [rdx+rcx*4]",
    "add r8,rdx",
    "jmp r8",
    fn + "_cmp:",
    "cmp esi,2",
    fn + "_copy:",
    "mov eax,esi",
    f"ja {fn}_default",
    f"lea rdx,[rip+{fn}_table1]",
    "movsxd r8,dword ptr [rdx+rax*4]",
    "add r8,rdx",
    "jmp r8",
]
for i in range(3):
    text += [fn + f"_case{i}:", f"mov eax,{i + 1}", "ret"]
text += [fn + "_default:", "xor eax,eax", "ret", fn + "_end:"]
tables += [
    ".balign 4",
    fn + "_table0:",
    f".long {fn}_copy-{fn}_table0",
    f".long {fn}_cmp-{fn}_table0",
    fn + "_table1:",
] + [f".long {fn}_case{i}-{fn}_table1" for i in range(3)]
(out / "fixture.s").write_text("\n".join(text + tables) + "\n")
run(
    [
        a.cxx,
        "-target",
        "x86_64-unknown-linux-gnu",
        "-c",
        out / "fixture.s",
        "-o",
        out / "fixture.o",
    ],
    "assemble.log",
)
run(
    [a.llvm_bin / "llvm-readobj", "--relocations", out / "fixture.o"], "relocations.txt"
)
assert "R_X86_64" not in (out / "relocations.txt").read_text()
run(
    [
        a.llvm_bin / "llvm-objcopy",
        "-O",
        "binary",
        "--only-section=.text",
        out / "fixture.o",
        out / "fixture.image",
    ],
    "objcopy.log",
)
run([a.llvm_bin / "llvm-nm", "--format=posix", out / "fixture.o"], "symbols.txt")
symbols = {}
for line in (out / "symbols.txt").read_text().splitlines():
    v = line.split()
    if len(v) >= 3:
        symbols[v[0]] = int(v[2], 16)
image = (out / "fixture.image").read_bytes()
header = bytearray(4096)
ident = bytes([127]) + b"ELF" + bytes([2, 1, 1]) + bytes(9)
struct.pack_into(
    "<16sHHIQQQIHHHHHH", header, 0, ident, 2, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0
)
struct.pack_into(
    "<IIQQQQQQ", header, 64, 1, 5, 4096, 0, 0, len(image), len(image), 4096
)
(out / "fixture.elf").write_bytes(header + image)
base = 0x600000
names = list(specs) + ["table_bypass"]
rows = [
    {
        "module": "fixture",
        "canonical_start": base + symbols["late_" + n],
        "canonical_end": base + symbols["late_" + n + "_end"],
        "fde_va": None,
    }
    for n in names
]
(out / "entries.json").write_text(json.dumps(rows, indent=2) + "\n")


def command(script, output):
    return [
        sys.executable,
        script,
        "--image",
        out / "fixture.image",
        "--elf",
        out / "fixture.elf",
        "--atlas",
        out / "entries.json",
        "--module",
        "fixture",
        "--base",
        hex(base),
        "--passes",
        "3",
        "--xed-root",
        a.xed_root,
        "--cxx",
        a.cxx,
        "--output",
        output,
    ]


run(command(source / "catalog.py", out / "catalog"), "catalog.log")
accepted = json.loads((out / "catalog/accepted-tables.json").read_text())
actual = {s["owner"] for s in accepted}
expected = {symbols["late_" + n] for n, s in specs.items() if s["ok"]}
checks = 0
for n in names:
    assert (symbols["late_" + n] in actual) == (n in specs and specs[n]["ok"]), n
    checks += 1
for row in accepted:
    assert not row.get("assumptions")
    name = next(n for n in names if symbols["late_" + n] == row["owner"])
    assert bool(row.get("late_index_copy")) == specs[name].get("late", False), name
    if row.get("late_index_copy"):
        late = row["late_index_copy"]
        assert late["pc"] > late["comparison_pc"] and late["pc"] in row["protected_pcs"]
        checks += 1
    checks += 2
if a.baseline_source:
    run(command(a.baseline_source / "catalog.py", out / "baseline"), "baseline.log")
    before = json.loads((out / "baseline/accepted-tables.json").read_text())
    assert {r["owner"] for r in before} == {symbols["late_ordinary"]}
    for row in before:
        assert row == next(r for r in accepted if r["owner"] == row["owner"])
        checks += 1
# Invalid ESI/EDI equivalence is never assumed; high32 bits are cleared by MOV32.
for upper in [0, 1, 0xFFFFFFFF]:
    for value in range(3):
        original = (upper << 32) | value
        index = original & 0xFFFFFFFF
        assert index == value
        checks += 1
results = {
    "owned_functions": len(rows),
    "accepted_owners": len(actual),
    "accepted_late_tables": sum(bool(r.get("late_index_copy")) for r in accepted),
    "checks": checks,
    "failures": 0,
    "accepted_names": [n for n in specs if specs[n]["ok"]],
    "rejected_names": [n for n in names if n not in specs or not specs[n]["ok"]],
    "baseline_existing_table_unchanged": bool(a.baseline_source),
    "scope": "Proved full32-bit matching late copy only; no runtime or conditional-equivalence assumption",
}
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
