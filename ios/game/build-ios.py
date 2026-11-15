#!/usr/bin/env python3
"""Compile an established Mac startup corpus into a private iPhoneOS static library."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
import json
from pathlib import Path
import subprocess
from guard_ir import instrument
from service_build import compile_services
from build_inputs import response_inputs

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--sdk", choices=("iphoneos", "iphonesimulator", "macosx"), default="iphoneos")
p.add_argument("--root", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
p.add_argument("--jobs", type=int, default=4, help="Concurrent trace compilers for this SDK")
a = p.parse_args()
if a.jobs < 1:
    p.error("--jobs must be positive")
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Root requires a mounted external disk")
source = Path(__file__).resolve().parent
repo = source.parents[1]
state = json.loads((root / "build-state.json").read_text())
if state["reference"].get("completed_modules") != 2:
    p.error("First establish both actual module initializers on the Mac")
backend = a.remill.resolve()
expected_backend = state["trace_inputs"]
actual_backend = {
    "remill_commit": subprocess.check_output(["git", "-C", str(backend / "remill"), "rev-parse", "HEAD"], text=True).strip(),
    "lifter_sha256": hashlib.sha256((backend / "remill-build/bin/lift/remill-lift-23").read_bytes()).hexdigest(),
    "semantics_sha256": hashlib.sha256((backend / "remill-build/lib/Arch/X86/Runtime/amd64_avx.bc").read_bytes()).hexdigest(),
}
if any(actual_backend[key] != expected_backend[key] for key in actual_backend):
    p.error("Backend differs from the established corpus; use its matching backend without reusing IR")
def check_inputs():
    for group, directory in (("sources", repo), ("headers", root), ("ir", root / "traces")):
        for name, wanted in state[group].items():
            if hashlib.sha256((directory / name).read_bytes()).hexdigest() != wanted:
                p.error("The established startup corpus changed: " + name)
check_inputs()
out = root / ({"iphonesimulator":"simulator","iphoneos":"ios","macosx":"mac"}[a.sdk])
out.mkdir(exist_ok=True)
# The manifest and the incremental object cache belong to a single SDK.
build_lock = (out / ".build.lock").open("a")
fcntl.flock(build_lock, fcntl.LOCK_EX)
llvm = Path("/opt/homebrew/opt/llvm/bin")
sdk = subprocess.check_output(["xcrun", "--sdk", a.sdk, "--show-sdk-path"], text=True).strip()
target = "arm64-apple-macos15" if a.sdk == "macosx" else "arm64-apple-ios18" + ("-simulator" if a.sdk == "iphonesimulator" else "")
clang = llvm / "clang"
trace_flags = ["-target", target, "-isysroot", sdk, "-O2"]
compiler_version = subprocess.check_output([clang, "--version"], text=True)
compiler_key = json.dumps([str(clang.resolve()), compiler_version, trace_flags]).encode() + b"\0"
objects = compile_services(repo, out / "services", target, sdk)


def run(name, command):
    with (out / (name + ".log")).open("w") as stream:
        result = subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode: raise RuntimeError(str(out / (name + ".log")) + ": compilation failed")


def compile_trace(entry):
    name, raw_key = entry
    raw = (root / "traces" / name).read_bytes()
    if hashlib.sha256(raw).hexdigest() != raw_key:
        raise RuntimeError("Startup trace changed: " + name)
    guarded, _ = instrument(raw.decode(), libc_helpers=True, image_sha256=state["image_sha256"])
    guarded_path = out / (Path(name).stem + ".guarded.ll")
    key = hashlib.sha256(compiler_key + guarded.encode()).hexdigest()
    obj = out / (Path(name).stem + ".o")
    stamp = obj.with_suffix(".sha256")
    if obj.is_file() and stamp.is_file() and stamp.read_text() == key:
        return obj
    guarded_path.write_text(guarded)
    temporary = obj.with_suffix(".new.o")
    run(obj.stem, [clang, *trace_flags, "-c", guarded_path, "-o", temporary])
    stamp.unlink(missing_ok=True)
    temporary.replace(obj)
    stamp.write_text(key)
    return obj


# map retains corpus order for the archiver, regardless of worker completion.
with ThreadPoolExecutor(max_workers=a.jobs) as compiler:
    objects.extend(compiler.map(compile_trace, state["ir"].items()))
for name, path in (("runner", source / "runner.cpp"), ("thunks", root / "thunks.cpp")):
    obj = out / (name + ".o")
    run(name, ["xcrun", "--sdk", a.sdk, "clang++", "-target", target, "-isysroot", sdk,
        "-std=c++17", "-O2", "-I" + str(a.remill / "remill/include"), "-I" + str(source.parent / "cpu"),
        "-I" + str(root), "-I" + str(repo / "src"), "-I" + str(repo / "launcher"), "-c", path, "-o", obj])
    objects.append(obj)
archive = out / "libodin-startup.new.a"
archive.unlink(missing_ok=True)
run("archive", [llvm / "llvm-ar", "rcs", archive, response_inputs(out/'archive-inputs.rsp',objects)])
check_inputs() # Never publish a mixed archive if sources changed during compilation.
archive.replace(out / "libodin-startup.a")
runtime_hash = hashlib.sha256((source.parent / "cpu/runtime.h").read_bytes() + (source.parent / "cpu/runtime.cpp").read_bytes()).hexdigest()
(out / "manifest.json").write_text(json.dumps({"corpus_sha256": state["corpus_sha256"], "runtime_sha256": runtime_hash,
    "trace_inputs": expected_backend,
    "trace_count": len(state["ir"]), "reference": state["reference"],
    "reference_runner_mode":state.get("runner_mode","headless"),"gpu_frontend_linked":False,
    "full_game_booted": False}, indent=2) + "\n")
print(json.dumps({"archive": str(out / "libodin-startup.a"), "traces": len(state["ir"]), "game_booted": False}))
