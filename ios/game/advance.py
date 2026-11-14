#!/usr/bin/env python3
"""Discover missing guest blocks by running bounded AOT startup attempts on arm64 Mac."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from guard_ir import instrument

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--root", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
p.add_argument("--rounds", type=int, default=16)
p.add_argument("--window-bytes", type=int, default=16384, help="Maximum executed trace window, clipped at instruction boundaries")
p.add_argument("--seed-from", type=Path, help="Re-lift only the PC list from a matching old build-state; never reuse its IR")
p.add_argument("--decoder-prefix", type=Path)
p.add_argument("--identity", action="store_true", help="Experiment with checked host-identity GPU-visible addresses")
p.add_argument("--adopt-existing-cache", action="store_true", help="Record provenance for a known task-local legacy trace cache")
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Root requires a mounted external disk")
source = Path(__file__).resolve().parent
if not 256 <= a.window_bytes <= 65536:
    p.error("--window-bytes must be between 256 and 65536")
if a.rounds < 1:
    p.error("--rounds must be positive")
layout = json.loads((root / "image.json").read_text())
image = (root / "image.bin").read_bytes()
if hashlib.sha256(image).hexdigest() != layout["image_sha256"]:
    p.error("Prepared image hash mismatch")
llvm = Path("/opt/homebrew/opt/llvm/bin")
sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
cache = root / "traces"
cache.mkdir(exist_ok=True)
remill_pin = subprocess.check_output(["git", "-C", str(a.remill / "remill"), "rev-parse", "HEAD"], text=True).strip()
if remill_pin != "66c488c9911fd9b368e4d0c9647d74239f8bba8b":
    p.error("Unexpected Remill revision")
identity = {"image_sha256": layout["image_sha256"], "base": layout["base"], "remill_commit": remill_pin,
            "lifter_sha256": hashlib.sha256((a.remill / "remill-build/bin/lift/remill-lift-23").read_bytes()).hexdigest(),
            "semantics_sha256": hashlib.sha256((a.remill / "remill-build/lib/Arch/X86/Runtime/amd64_avx.bc").read_bytes()).hexdigest()}
identity_path = root / "trace-identity.json"
if identity_path.exists():
    if json.loads(identity_path.read_text()) != identity:
        p.error("Trace inputs changed; preserve this cache and use a new external work directory")
else:
    if list(cache.glob("*.ll")) and not a.adopt_existing_cache:
        p.error("Existing traces have no input ledger; use --adopt-existing-cache only for this task's known cache")
    identity_path.write_text(json.dumps(identity, indent=2) + "\n")
objects = root / "objects"
objects.mkdir(exist_ok=True)
header = "#pragma once\n#include <cstdint>\n"
header += f"static constexpr uint64_t image_base={layout['base']}, image_size={layout['size']}, hle_base={layout['hle_base']}, procparam={layout['procparam']}, guest_entry={layout['entry']}, image_pointer_count={layout['image_pointer_count']};\n"
header += f'static const char *image_sha256_hex="{layout["image_sha256"]}", *pointer_sha256_hex="{layout["image_pointer_sha256"]}";\n'
header += "struct Segment { uint64_t address,size,flags; };\nstatic const Segment segments[]={"
header += ",".join("{" + ",".join(str(n) for n in s) + "}" for s in layout["segments"]) + "};\n"
header += "struct Module { uint64_t base,size,init,tls,memsz,filesz,id; };\nstatic const Module modules[]={"
header += ",".join("{" + ",".join(str(n) for n in s) + "}" for s in layout["modules"]) + "};\n"
header += "struct Import { const char *nid,*symbol; bool data; };\nstatic const Import imports[]={"
header += ",".join("{" + json.dumps(i["nid"]) + "," + json.dumps(i["symbol"]) + "," + str(i["data"]).lower() + "}" for i in layout["imports"]) + "};\n"
(root / "image-layout.h").write_text(header)


def run(name, command, timeout=60, env=None):
    with (root / (name + ".log")).open("w") as out:
        subprocess.run([str(x) for x in command], stdout=out, stderr=subprocess.STDOUT, check=True, timeout=timeout, env=env)


def decode_size(code, window=False):
    decoder = root / "block-size"
    key = hashlib.sha256((source / "block-size.cpp").read_bytes()).hexdigest()
    stamp = decoder.with_suffix(".sha256")
    if not decoder.exists() or not stamp.exists() or stamp.read_text() != key:
        prefix = a.decoder_prefix or root.parent.parent / "GodOfWarIII/deps"
        run("decoder-build", ["xcrun", "clang++", "-arch", "x86_64", "-O2", "-I" + str(prefix / "include"),
            source / "block-size.cpp", prefix / "lib/libZydis.a", prefix / "lib/libZycore.a", "-o", decoder])
        stamp.write_text(key)
    return int(subprocess.check_output([str(decoder), code.hex(), *( ["window"] if window else [])], text=True))


def lift(pc, code, output):
    # Remill must see complete instructions at the window boundary.
    code = code[:decode_size(code, window=True)]
    run("lift-" + hex(pc), [a.remill / "remill-build/bin/lift/remill-lift-23", "--os", "linux", "--arch", "amd64_avx",
                           "--address", str(pc), "--bytes", code.hex(), "--ir_out", output], 120)


if a.seed_from:
    old = json.loads(a.seed_from.read_text())
    if old["image_sha256"] != layout["image_sha256"]:
        p.error("Seed PC list belongs to a different prepared image")
    if list(cache.glob("*.ll")):
        p.error("Seed requires a new trace cache")
    for name in old["ir"]:
        pc = int(Path(name).stem, 16)
        offset = pc - layout["base"]
        segment = next((s for s in layout["segments"] if s[2] & 1 and s[0] <= offset < s[0] + s[1]), None)
        if segment is None:
            p.error("Seed PC is outside executable memory")
        code = image[offset:min(offset + a.window_bytes, segment[0] + segment[1])]
        lift(pc, code, cache / name)
    print("Re-lifted seed PC list with the isolated backend", flush=True)


for round in range(a.rounds):
    entries = {}
    declarations = set()
    objs = []
    for ir in sorted(cache.glob("*.ll")):
        if (re.search(r"\b(?:fcmp|fpext|fptrunc|fadd|fsub|fmul|fdiv|load|store|call)\b[^\n]*\bx86_fp80\b", ir.read_text()) or
                re.search(r"\bcall\b[^\n]*@__remill_error\b", ir.read_text())):
            # Keep unexecuted long-double formatting paths out of a broad trace.
            # An actual x87 block remains an explicit backend gap, never a no-op.
            pc = int(ir.stem, 16)
            offset = pc - layout["base"]
            segment = next(s for s in layout["segments"] if s[2] & 1 and s[0] <= offset < s[0] + s[1])
            code = image[offset:min(offset + a.window_bytes, segment[0] + segment[1])]
            length = decode_size(code)
            archive = root / "broad-trace-backups"
            archive.mkdir(exist_ok=True)
            backup = archive / ir.name
            if not backup.exists():
                backup.write_bytes(ir.read_bytes())
            lift(pc, code[:length], ir)
            if re.search(r"\b(?:fcmp|fpext|fptrunc|fadd|fsub|fmul|fdiv|load|store|call)\b[^\n]*\bx86_fp80\b", ir.read_text()):
                raise RuntimeError("Executed block requires unsupported x87 extended precision at " + hex(pc))
        obj = objects / (ir.stem + ".o")
        guarded, _ = instrument(ir.read_text())
        guarded_path = objects / (ir.stem + ".guarded.ll")
        guarded_path.write_text(guarded)
        fingerprint = hashlib.sha256(guarded.encode()).hexdigest()
        stamp = obj.with_suffix(".sha256")
        if not obj.exists() or not stamp.exists() or stamp.read_text() != fingerprint:
            run("compile-" + ir.stem, [llvm / "clang", "-target", "arm64-apple-macos15", "-isysroot", sdk, "-O2", "-c", guarded_path, "-o", obj])
            stamp.write_text(fingerprint)
        objs.append(obj)
        for name in re.findall(r"^define ptr @sub_([0-9a-f]+)\(", ir.read_text(), re.M):
            if int(name, 16) in entries:
                raise RuntimeError("Duplicate lifted entry")
            entries[int(name, 16)] = "sub_" + name
        declarations.update(re.findall(r"^declare ptr @sub_([0-9a-f]+)\(", ir.read_text(), re.M))
    text = "#pragma once\n"
    for symbol in entries.values():
        text += f'extern "C" Memory *{symbol}(State &,uint64_t,Memory *);\n'
    text += "struct Trace { uint64_t pc; Lifted function; };\nstatic const Trace traces[]={"
    text += ",".join("{" + str(pc) + ",&" + symbol + "}" for pc, symbol in entries.items()) + "};\n"
    (root / "traces.h").write_text(text)
    thunks = '#include "runtime.h"\n'
    for symbol in sorted(declarations):
        if int(symbol, 16) not in entries:
            thunks += f'extern "C" Memory *sub_{symbol}(State &s,uint64_t pc,Memory *m) {{ return dispatch(s,pc,m); }}\n'
    (root / "thunks.cpp").write_text(thunks)
    run("runner-build", ["xcrun", "clang++", "-arch", "arm64", "-std=c++17", "-O2", "-DOSL_GAME_CLI=1", "-I" + str(a.remill / "remill/include"),
        "-I" + str(root), "-I" + str(source.parent / "cpu"), source / "runner.cpp", source.parent / "cpu/runtime.cpp", root / "thunks.cpp", *objs, "-o", root / "guest-startup"], 120)
    state = {"image_sha256": layout["image_sha256"],
             "trace_inputs": identity,
             "ir": {obj.stem + ".ll": hashlib.sha256((cache / (obj.stem + ".ll")).read_bytes()).hexdigest() for obj in objs},
             "headers": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in ("image-layout.h", "traces.h", "thunks.cpp")},
             "sources": {str(path.relative_to(source.parents[1])): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in (source / "runner.cpp", source / "guard_ir.py", source.parent / "cpu/runtime.h", source.parent / "cpu/runtime.cpp")}}
    attempt = len(list(root.glob("attempt-*.json")))
    name = "attempt-" + str(attempt).zfill(5)
    run(name, [root / "guest-startup", root / "image.bin"], 15,
        env=dict(os.environ, OSL_GUEST_IDENTITY="1" if a.identity else "0"))
    report = json.loads((root / (name + ".log")).read_text())
    report["trace_count"] = len(entries)
    state["reference"] = report
    state["corpus_sha256"] = hashlib.sha256(json.dumps({k: state[k] for k in ("image_sha256", "trace_inputs", "ir", "headers", "sources")}, sort_keys=True).encode()).hexdigest()
    (root / "build-state.json").write_text(json.dumps(state, indent=2) + "\n")
    (root / (name + ".json")).write_text(json.dumps(report, indent=2) + "\n")
    (root / "latest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report), flush=True)
    pc = report.get("missing_pc", 0)
    if report.get("error") != "missing translated block":
        break
    offset = pc - layout["base"]
    segment = next((s for s in layout["segments"] if s[2] & 1 and s[0] <= offset < s[0] + s[1]), None)
    if segment is None:
        raise RuntimeError("Requested trace is outside executable guest memory")
    if pc in entries:
        raise RuntimeError("A known trace was not dispatched")
    length = min(a.window_bytes, segment[0] + segment[1] - offset)
    code = image[offset:offset + length]
    lift(pc, code, cache / (hex(pc) + ".ll"))
