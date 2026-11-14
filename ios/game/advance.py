#!/usr/bin/env python3
"""Discover missing guest blocks by running bounded AOT startup attempts on arm64 Mac."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from guard_ir import instrument
from discovery_targets import missing_targets
from service_build import compile_services, inputs as service_inputs, link_options

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--root", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
p.add_argument("--rounds", type=int, default=16)
p.add_argument("--window-bytes", type=int, default=16384, help="Maximum executed trace window, clipped at instruction boundaries")
p.add_argument("--seed-from", type=Path, help="Re-lift only the PC list from a matching old build-state; never reuse its IR")
p.add_argument("--decoder-prefix", type=Path)
p.add_argument("--identity", action="store_true", help="Experiment with checked host-identity GPU-visible addresses")
p.add_argument("--adopt-existing-cache", action="store_true", help="Record provenance for a known task-local legacy trace cache")
p.add_argument("--gpu-build", type=Path, help="Isolated native arm64 renderer CMake build for real GPU-backed discovery")
p.add_argument("--gpu-driver-manifest", type=Path, help="Explicit native macOS Vulkan ICD manifest for GPU-backed discovery")
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
        try:
            result = subprocess.run([str(x) for x in command], stdout=out, stderr=subprocess.STDOUT, timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            raise RuntimeError(f"{name} timed out; see {root / (name + '.log')}") from None
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {root / (name + '.log')}")


def atomic_json(path, value):
    # A stopped discovery process must not truncate the last usable checkpoint.
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            json.dump(value, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


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


repo = source.parents[1]
source_paths = (source / "advance.py", source / "runner.cpp", source / "guard_ir.py",source / "discovery_targets.py",
                source.parent / "cpu/runtime.h", source.parent / "cpu/runtime.cpp", *service_inputs(repo))
gpu_env = None
if a.gpu_build:
    if not a.identity or not a.gpu_driver_manifest:
        p.error("GPU-backed discovery requires --identity and --gpu-driver-manifest")
    gpu_build = a.gpu_build.resolve()
    if len(gpu_build.parts)<3 or gpu_build.parts[1]!="Volumes" or not Path('/Volumes',gpu_build.parts[2]).is_mount():
        p.error("GPU build requires a mounted external disk")
    cmake_cache = (gpu_build / "CMakeCache.txt").read_text()
    if f"CMAKE_HOME_DIRECTORY:INTERNAL={repo}" not in cmake_cache:
        p.error("GPU build belongs to a different source tree")
    loader = Path(re.search(r"^Vulkan_LIBRARY:FILEPATH=(.+)$",cmake_cache,re.M)[1])
    driver_manifest = a.gpu_driver_manifest.resolve()
    driver = Path(json.loads(driver_manifest.read_text())["ICD"]["library_path"])
    if not driver.is_absolute(): driver = (driver_manifest.parent / driver).resolve()
    if "arm64" not in subprocess.check_output(["lipo","-archs",driver],text=True).split():
        p.error("GPU discovery needs a native arm64 driver")
    gpu_env = dict(os.environ,VK_DRIVER_FILES=str(driver_manifest),VK_ICD_FILENAMES=str(driver_manifest),SDL_VULKAN_LIBRARY=str(loader))
    source_paths += (repo / "CMakeLists.txt", repo / "gpu/CMakeLists.txt",repo / "cmake/OslPinnedSources.cmake",
                     repo / "src/oslgpu.h",repo / "ios/graphics/aot-frontend.cpp",repo / "ios/graphics/bridge-checks.h")
    source_paths += tuple(path for directory in (repo / "gpu/shim",repo / "gpu/shadps4")
                          for path in sorted(directory.rglob("*")) if path.is_file() and path.suffix in (".cpp",".h",".c",".comp",".frag",".vert",".glsl",".hlsl",".cmake"))
    run("gpu-configure",["cmake","-S",repo,"-B",gpu_build,"-DOSL_AOT_ROOT="+str(root),"-DOSL_REMILL_INCLUDE="+str(a.remill / "remill/include")],120)
def source_snapshot():
    return {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
compiled_sources = source_snapshot()
def check_sources():
    if source_snapshot() != compiled_sources:
        raise RuntimeError("Startup sources changed during discovery; rerun before publishing a checkpoint")
service_objects = compile_services(repo, objects / "services", "arm64-apple-macos15", sdk)
check_sources()

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
        guarded, _ = instrument(ir.read_text(), libc_helpers=True, image_sha256=layout['image_sha256'])
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
    check_sources()
    run("runner-build", ["xcrun", "clang++", "-arch", "arm64", "-std=c++17", "-O2", "-DOSL_GAME_CLI=1", "-I" + str(a.remill / "remill/include"),
        "-I" + str(root), "-I" + str(source.parent / "cpu"), "-I" + str(repo / "src"), "-I" + str(repo / "launcher"), source / "runner.cpp", source.parent / "cpu/runtime.cpp", root / "thunks.cpp", *service_objects, *objs, *link_options(), "-o", root / "guest-startup"], 120)
    state = {"image_sha256": layout["image_sha256"],
             "trace_inputs": identity,
             "ir": {obj.stem + ".ll": hashlib.sha256((cache / (obj.stem + ".ll")).read_bytes()).hexdigest() for obj in objs},
             "headers": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in ("image-layout.h", "traces.h", "thunks.cpp")},
             "sources": compiled_sources}
    command = [root / "guest-startup",root / "image.bin"]
    prefix = "OSL_STARTUP_RESULT:"
    if a.gpu_build:
        # Reuse this round's exact checked arm64 objects; only the library
        # entry and unresolved-block thunks need separate non-CLI compilation.
        mac = root / "mac"; mac.mkdir(exist_ok=True)
        library_objects = []
        for label,path in (("runner",source / "runner.cpp"),("thunks",root / "thunks.cpp")):
            obj = mac / (label + ".o")
            run("gpu-"+label,["xcrun","clang++","-target","arm64-apple-macos15","-isysroot",sdk,"-std=c++17","-O2",
                "-I"+str(a.remill / "remill/include"),"-I"+str(source.parent / "cpu"),"-I"+str(root),
                "-I"+str(repo / "src"),"-I"+str(repo / "launcher"),"-c",path,"-o",obj],120)
            library_objects.append(obj)
        archive = mac / "libodin-startup.new.a"; archive.unlink(missing_ok=True)
        run("gpu-archive",[llvm / "llvm-ar","rcs",archive,*library_objects,*service_objects,*objs],120)
        check_sources()
        archive.replace(mac / "libodin-startup.a")
        run("gpu-build",["cmake","--build",gpu_build,"--target","osl-aot-gpu-probe","-j6"],180)
        native = {"binary_sha256":hashlib.sha256((gpu_build / "bin/osl-aot-gpu-probe").read_bytes()).hexdigest(),
                  "renderer_sha256":hashlib.sha256((gpu_build / "gpu/liboslgpu.a").read_bytes()).hexdigest(),
                  "driver_sha256":hashlib.sha256(driver.read_bytes()).hexdigest(),
                  "manifest_sha256":hashlib.sha256(driver_manifest.read_bytes()).hexdigest(),
                  "loader_sha256":hashlib.sha256(loader.read_bytes()).hexdigest()}
        state.update(runner_mode="native-arm64-gpu",native_gpu=native)
        profile = gpu_build / "aot-profile"; profile.mkdir(exist_ok=True)
        command = [gpu_build / "bin/osl-aot-gpu-probe",root / "image.bin",profile,"--discover"]
        prefix = "OSL_AOT_GPU_GUEST:"
    attempt = len(list(root.glob("attempt-*.json")))
    name = "attempt-" + str(attempt).zfill(5)
    def runtime_inputs():
        app0=(root / "App0").resolve();marker=app0 / "eboot.bin"
        return {"app0":str(app0),"app0_eboot_sha256":hashlib.sha256(marker.read_bytes()).hexdigest() if marker.is_file() else None,
                "asset_marker_only":True}
    state["runtime_inputs"]=runtime_inputs()
    run(name, command, 30 if a.gpu_build else 15,
        env=gpu_env if a.gpu_build else dict(os.environ, OSL_GUEST_IDENTITY="1" if a.identity else "0"))
    records = [line.removeprefix(prefix) for line in (root / (name + ".log")).read_text().splitlines() if line.startswith(prefix)]
    if len(records) != 1: raise RuntimeError("Expected one startup report in " + name + ".log")
    report = json.loads(records[0])
    check_sources()
    if runtime_inputs()!=state["runtime_inputs"]:raise RuntimeError("Asset root changed during the startup attempt")
    report["trace_count"] = len(entries)
    state["reference"] = report
    corpus_keys = ("image_sha256", "trace_inputs", "ir", "headers", "sources","runtime_inputs") + (("runner_mode","native_gpu") if a.gpu_build else ())
    state["corpus_sha256"] = hashlib.sha256(json.dumps({k: state[k] for k in corpus_keys}, sort_keys=True).encode()).hexdigest()
    atomic_json(root / "build-state.json", state)
    atomic_json(root / (name + ".json"), report)
    atomic_json(root / "latest.json", report)
    print(json.dumps(report), flush=True)
    pending=missing_targets(report,layout["base"],layout["segments"],entries)
    if not pending:break
    for pc in pending:
        offset=pc-layout["base"]
        segment=next(s for s in layout["segments"] if s[2]&1 and s[0]<=offset<s[0]+s[1])
        length=min(a.window_bytes,segment[0]+segment[1]-offset)
        lift(pc,image[offset:offset+length],cache/(hex(pc)+".ll"))
