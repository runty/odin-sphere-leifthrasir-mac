#!/usr/bin/env python3
"""Compile an established Mac startup corpus into a private iPhoneOS static library."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
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
out = root / "ios"
out.mkdir(exist_ok=True)
llvm = Path("/opt/homebrew/opt/llvm/bin")
sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
objects = []


def run(name, command):
    with (out / (name + ".log")).open("w") as stream:
        subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=180)


for name, key in state["ir"].items():
    obj = out / (Path(name).stem + ".o")
    stamp = obj.with_suffix(".sha256")
    if not obj.exists() or not stamp.exists() or stamp.read_text() != key:
        run(obj.stem, [llvm / "clang", "-target", "arm64-apple-ios18", "-isysroot", sdk, "-O2", "-c", root / "traces" / name, "-o", obj])
        stamp.write_text(key)
    objects.append(obj)
for name, path in (("runner", source / "runner.cpp"), ("thunks", root / "thunks.cpp")):
    obj = out / (name + ".o")
    run(name, ["xcrun", "--sdk", "iphoneos", "clang++", "-target", "arm64-apple-ios18", "-isysroot", sdk,
        "-std=c++17", "-O2", "-I" + str(a.remill / "remill/include"), "-I" + str(source.parent / "cpu"),
        "-I" + str(root), "-c", path, "-o", obj])
    objects.append(obj)
archive = out / "libodin-startup.new.a"
archive.unlink(missing_ok=True)
run("archive", [llvm / "llvm-ar", "rcs", archive, *objects])
archive.replace(out / "libodin-startup.a")
runtime_hash = hashlib.sha256((source.parent / "cpu/runtime.h").read_bytes() + (source.parent / "cpu/runtime.cpp").read_bytes()).hexdigest()
(out / "manifest.json").write_text(json.dumps({"corpus_sha256": state["corpus_sha256"], "runtime_sha256": runtime_hash,
    "trace_count": len(state["ir"]), "reference": state["reference"], "full_game_booted": False}, indent=2) + "\n")
print(json.dumps({"archive": str(out / "libodin-startup.a"), "traces": len(state["ir"]), "game_booted": False}))
