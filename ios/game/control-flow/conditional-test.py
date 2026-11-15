#!/usr/bin/env python3
"""Owned assumption-tier fixtures proving strict-output and table-closure isolation."""

from pathlib import Path
import argparse, hashlib, json, struct, subprocess, sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--xed-root", type=Path, required=True)
parser.add_argument("--llvm-bin", type=Path, required=True)
parser.add_argument("--cxx", default="clang++")
parser.add_argument("--baseline-source", type=Path)
args = parser.parse_args()
out = args.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    parser.error("Test output must be on a mounted external disk")
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent


def run(command, name):
    with (out / name).open("w") as stream:
        subprocess.run(
            list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, check=True
        )


def snapshot(folder):
    return {
        str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in folder.rglob("*")
        if p.is_file()
    }


specs = {
    "strict_good": ("mov eax,edi\ncmp eax,2", True, False, ""),
    "conditional32": ("mov rax,rdi\ncmp eax,2", False, True, ""),
    "conditional32_scheduled": ("mov rax,rdi\ncmp eax,2\nmov r9,r9", False, True, ""),
    "conditional32_clobber": ("mov rax,rdi\ncmp eax,2", False, False, "mov eax,99"),
    "conditional32_wrong_index": ("mov rax,rdi\ncmp esi,2", False, False, ""),
    "conditional8": ("mov eax,edi\ncmp al,2", False, True, ""),
    "conditional16": ("mov rax,rdi\ncmp ax,2", False, True, ""),
    "wrong_copy": ("mov eax,esi\ncmp bl,2", False, False, ""),
    "stale_copy": ("movzx eax,bl\ninc bl\ncmp bl,2", False, False, ""),
    "highbyte": ("movzx eax,bh\ncmp ah,2", False, False, ""),
    "postguard_clobber": ("mov eax,edi\ncmp al,2", False, False, "mov eax,99"),
    "conditional_unknown": ("mov eax,edi\ncmp al,2", False, True, ""),
    "conditional_external": ("mov eax,edi\ncmp al,2", False, True, ""),
    "copy_bypass": (
        "mov eax,0x100\ntest edi,edi\njnz case_copy_bypass_cmp\nmov eax,esi\ncase_copy_bypass_cmp:\ncmp esi,2",
        False,
        False,
        "",
    ),
}
text = [".intel_syntax noprefix", ".text", ".org 0x100"]
tables = []
for name, (prefix, strict, conditional, scheduled) in specs.items():
    fn = "case_" + name
    if name == "strict_good":
        text.append(".Lstrict_good:")
    text += [f".global {fn}", f".global {fn}_end", fn + ":"] + prefix.splitlines()
    text += [f"ja {fn}_default"]
    if scheduled:
        text.append(scheduled)
    text += [
        f"lea rcx,[rip+{fn}_table]",
        "movsxd rdx,dword ptr [rcx+rax*4]",
        "add rdx,rcx",
        "jmp rdx",
    ]
    for i in range(3):
        text.append(fn + f"_target{i}:")
        if i == 0 and name == "conditional_unknown":
            text.append("jmp r8")
        elif i == 0 and name == "conditional_external":
            text.append("jmp .Lstrict_good")
        else:
            text += [f"mov eax,{i + 7}", "ret"]
    text += [fn + "_default:", "xor eax,eax", "ret", fn + "_end:"]
    tables += [".balign 4", fn + "_table:"] + [
        f".long {fn}_target{i}-{fn}_table" for i in range(3)
    ]
# A conditional table makes a locally strict secondary table discoverable only
# inside the separate assumption-tier pass. Its cases must never enter strict
# results or be mislabeled as conditional table rows with no assumptions.
fn = "case_conditional_parent"
text += [
    f".global {fn}",
    f".global {fn}_end",
    fn + ":",
    "mov eax,edi",
    "cmp al,1",
    f"ja {fn}_default",
    f"lea rcx,[rip+{fn}_table]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    fn + "_secondary:",
    "movzx eax,si",
    "cmp ax,1",
    f"ja {fn}_default",
    f"lea rcx,[rip+{fn}_table2]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    fn + "_return:",
    "mov eax,3",
    "ret",
    fn + "_secondary0:",
    "mov eax,4",
    "ret",
    fn + "_secondary1:",
    "mov eax,5",
    "ret",
    fn + "_default:",
    "xor eax,eax",
    "ret",
    fn + "_end:",
]
tables += [
    ".balign 4",
    fn + "_table:",
    f".long {fn}_secondary-{fn}_table",
    f".long {fn}_return-{fn}_table",
    fn + "_table2:",
    f".long {fn}_secondary0-{fn}_table2",
    f".long {fn}_secondary1-{fn}_table2",
]
(out / "fixture.s").write_text("\n".join(text + tables) + "\n")
run(
    [
        args.cxx,
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
    [args.llvm_bin / "llvm-readobj", "--relocations", out / "fixture.o"],
    "relocations.txt",
)
assert "R_X86_64" not in (out / "relocations.txt").read_text()
run(
    [
        args.llvm_bin / "llvm-objcopy",
        "-O",
        "binary",
        "--only-section=.text",
        out / "fixture.o",
        out / "fixture.image",
    ],
    "objcopy.log",
)
run([args.llvm_bin / "llvm-nm", "--format=posix", out / "fixture.o"], "symbols.txt")
symbols = {}
for line in (out / "symbols.txt").read_text().splitlines():
    fields = line.split()
    if len(fields) >= 3:
        symbols[fields[0]] = int(fields[2], 16)
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
base = 0x500000
names = list(specs) + ["conditional_parent"]
rows = [
    {
        "module": "fixture",
        "canonical_start": base + symbols["case_" + name],
        "canonical_end": base + symbols["case_" + name + "_end"],
        "fde_va": None,
    }
    for name in names
]
(out / "entries.json").write_text(json.dumps(rows, indent=2) + "\n")


def strict_command(script, output):
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
        args.xed_root,
        "--cxx",
        args.cxx,
        "--output",
        output,
    ]


strict = out / "strict"
run(strict_command(source / "catalog.py", strict), "strict.log")
strict_before = snapshot(strict)
strict_tables = json.loads((strict / "accepted-tables.json").read_text())
assert {r["owner"] for r in strict_tables} == {symbols["case_strict_good"]}
assert not any(r.get("assumptions") for r in strict_tables)
checks = 2
if args.baseline_source:
    baseline = out / "baseline-strict"
    run(
        strict_command(args.baseline_source / "catalog.py", baseline),
        "baseline-strict.log",
    )
    original = json.loads((baseline / "provenance.json").read_text())
    for name in original["outputs"]:
        assert (baseline / name).read_bytes() == (strict / name).read_bytes(), name
        checks += 1
conditional = out / "conditional"
run(
    [
        sys.executable,
        source / "conditional.py",
        "--strict-output",
        strict,
        "--output",
        conditional,
        "--xed-root",
        args.xed_root,
        "--cxx",
        args.cxx,
        "--passes",
        "3",
    ],
    "conditional.log",
)
assert strict_before == snapshot(strict)
checks += 1
actual = json.loads((conditional / "conditional-tables.json").read_text())
expected = {symbols["case_" + name] for name, row in specs.items() if row[2]} | {
    symbols["case_conditional_parent"]
}
assert {r["owner"] for r in actual} == expected
checks += 1
for row in actual:
    assert (
        row.get("conditional")
        and row["assumptions"]
        and not row["owner_cfg"]["complete"]
    )
    assert all(
        a["proof_status"] == "unproven" and a["kind"] == "zero-upper-index"
        for a in row["assumptions"]
    )
    assert (
        row["owner_cfg"]["decode_or_reachability_failures"]
        == row["owner_cfg"]["instruction_overlaps"]
        == 0
    )
    checks += 3
assert len(actual) == len(expected)
assert not any(
    r["owner"] == symbols["case_conditional_parent"] and not r.get("assumptions")
    for r in actual
)
secondary = {
    base + symbols["case_conditional_parent_secondary0"],
    base + symbols["case_conditional_parent_secondary1"],
}
conditional_pcs = {
    int(v, 16)
    for v in (conditional / "conditional-targets.txt").read_text().splitlines()
}
assert not secondary & conditional_pcs
assert (
    not (conditional / "accepted-tables.json").exists()
    and not (conditional / "all-targets.txt").exists()
)
unknown = next(r for r in actual if r["owner"] == symbols["case_conditional_unknown"])
external = next(r for r in actual if r["owner"] == symbols["case_conditional_external"])
assert (
    unknown["owner_cfg"]["unresolved_indirect_pcs"]
    and external["owner_cfg"]["external_direct_edges"]
)
checks += 5
# Bit-pattern counterexample documents why these cannot become strict tables.
assert (0x100 & 0xFF) <= 2 and 0x100 > 2
checks += 1
assert ((1 << 32) & 0xFFFFFFFF) <= 2 and (1 << 32) > 2
assert any(a['guard_width']==32 for row in actual for a in row['assumptions'])
checks += 2
result = {
    "owned_functions": len(rows),
    "strict_owners": len(strict_tables),
    "conditional_tables": len(actual),
    "checks": checks,
    "failures": 0,
    "strict_snapshot_unchanged": True,
    "baseline_outputs_identical": bool(args.baseline_source),
    "assumption": "full_index >> guard_width == 0 is unproven",
    "conditional_owner_names": [
        name for name in names if symbols["case_" + name] in expected
    ],
}
(out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
