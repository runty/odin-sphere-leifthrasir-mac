#!/usr/bin/env python3
"""Compile an established Mac startup corpus into a private iPhoneOS static library."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from guard_ir import instrument
from service_build import compile_services

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--sdk", choices=("iphoneos", "iphonesimulator"), default="iphoneos")
p.add_argument("--root", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Root requires a mounted external disk")
source = Path(__file__).resolve().parent
repo = source.parents[1]
state = json.loads((root / "build-state.json").read_text())
if state["reference"].get("completed_modules") != 2:
    p.error("First establish both actual module initializers on the Mac")
for group, directory in (("sources", repo), ("headers", root), ("ir", root / "traces")):
    for name, wanted in state[group].items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != wanted:
            p.error("The established startup corpus changed: " + name)
out = root / ("simulator" if a.sdk == "iphonesimulator" else "ios")
out.mkdir(exist_ok=True)
llvm = Path("/opt/homebrew/opt/llvm/bin")
sdk = subprocess.check_output(["xcrun", "--sdk", a.sdk, "--show-sdk-path"], text=True).strip()
target = "arm64-apple-ios18" + ("-simulator" if a.sdk == "iphonesimulator" else "")
objects = compile_services(repo, out / "services", target, sdk)


def run(name, command):
    with (out / (name + ".log")).open("w") as stream:
        result = subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode: raise RuntimeError(str(out / (name + ".log")) + ": compilation failed")


for name, raw_key in state["ir"].items():
    guarded, _ = instrument((root / "traces" / name).read_text())
    guarded_path = out / (Path(name).stem + ".guarded.ll")
    guarded_path.write_text(guarded)
    key = hashlib.sha256(guarded.encode()).hexdigest()
    obj = out / (Path(name).stem + ".o")
    stamp = obj.with_suffix(".sha256")
    if not obj.exists() or not stamp.exists() or stamp.read_text() != key:
        run(obj.stem, [llvm / "clang", "-target", target, "-isysroot", sdk, "-O2", "-c", guarded_path, "-o", obj])
        stamp.write_text(key)
    objects.append(obj)
for name, path in (("runner", source / "runner.cpp"), ("thunks", root / "thunks.cpp")):
    obj = out / (name + ".o")
    run(name, ["xcrun", "--sdk", a.sdk, "clang++", "-target", target, "-isysroot", sdk,
        "-std=c++17", "-O2", "-I" + str(a.remill / "remill/include"), "-I" + str(source.parent / "cpu"),
        "-I" + str(root), "-I" + str(repo / "src"), "-I" + str(repo / "launcher"), "-c", path, "-o", obj])
    objects.append(obj)
archive = out / "libodin-startup.new.a"
archive.unlink(missing_ok=True)
run("archive", [llvm / "llvm-ar", "rcs", archive, *objects])
archive.replace(out / "libodin-startup.a")
runtime_hash = hashlib.sha256((source.parent / "cpu/runtime.h").read_bytes() + (source.parent / "cpu/runtime.cpp").read_bytes()).hexdigest()
(out / "manifest.json").write_text(json.dumps({"corpus_sha256": state["corpus_sha256"], "runtime_sha256": runtime_hash,
    "trace_count": len(state["ir"]), "reference": state["reference"], "full_game_booted": False}, indent=2) + "\n")
print(json.dumps({"archive": str(out / "libodin-startup.a"), "traces": len(state["ir"]), "game_booted": False}))
