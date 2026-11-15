#!/usr/bin/env python3
"""Discover missing guest blocks by running bounded AOT startup attempts on arm64 Mac."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import time
from trace_preparation import TracePreparationCache
from discovery_targets import deadline_only, missing_targets
from trace_candidates import candidates, nearest
from function_bounds import FunctionBounds
from service_build import compile_services, inputs as service_inputs, link_options
from build_inputs import response_inputs

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--root", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
p.add_argument("--guest-budget-ms", type=int, default=2000, help="GPU guest attempt duration, 100 to 600000 ms")
p.add_argument("--jobs", type=int, default=4, help="Independent trace lifters and compilers (1 to 32)")
p.add_argument("--rounds", type=int, default=16)
p.add_argument("--prefetch", type=int, default=0, help="Extra static candidates per round (0 to 256, or 4096 with --prefetch-unwind); never execution evidence")
p.add_argument("--function-bounds",type=Path,help="Verified original unwind ranges limiting new lifts, with segment fallback for unindexed code")
p.add_argument("--prefetch-unwind",action="store_true",help="Include verified original unwind function entries in static candidates; requires --function-bounds and positive --prefetch")
p.add_argument("--retain-error-paths",action="store_true",help="Keep compilable Remill error/trap branches; executing one still stops explicitly")
p.add_argument("--window-bytes", type=int, default=16384, help="Maximum executed trace window, clipped at instruction boundaries")
p.add_argument("--seed-from", type=Path, help="Re-lift only the PC list from a matching old build-state; never reuse its IR")
p.add_argument("--decoder-prefix", type=Path)
p.add_argument("--identity", action="store_true", help="Experiment with checked host-identity GPU-visible addresses")
p.add_argument("--adopt-existing-cache", action="store_true", help="Record provenance for a known task-local legacy trace cache")
p.add_argument("--gpu-build", type=Path, help="Isolated native arm64 renderer CMake build for real GPU-backed discovery")
p.add_argument("--gpu-driver-manifest", type=Path, help="Explicit native macOS Vulkan ICD manifest for GPU-backed discovery")
p.add_argument("--stop-file", type=Path, help="Stop after publishing a completed checkpoint when this file exists; never interrupts a compiler or guest")
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Root requires a mounted external disk")
source = Path(__file__).resolve().parent
if not 256 <= a.window_bytes <= 65536:
    p.error("--window-bytes must be between 256 and 65536")
if not 100 <= a.guest_budget_ms <= 600000:
    p.error("--guest-budget-ms must be between 100 and 600000")
if a.guest_budget_ms != 2000 and not a.gpu_build:
    p.error("An extended guest budget requires --gpu-build")
if not 1 <= a.jobs <= 32:
    p.error("--jobs must be between 1 and 32")
if a.rounds < 1:
    p.error("--rounds must be positive")
prefetch_cap=4096 if a.prefetch_unwind else 256
if not 0 <= a.prefetch <= prefetch_cap:
    p.error("--prefetch must be between 0 and "+str(prefetch_cap))
if a.prefetch_unwind and (not a.function_bounds or not a.prefetch):
    p.error("--prefetch-unwind requires --function-bounds and positive --prefetch")
layout = json.loads((root / "image.json").read_text())
image = (root / "image.bin").read_bytes()
if hashlib.sha256(image).hexdigest() != layout["image_sha256"]:
    p.error("Prepared image hash mismatch")
function_bounds=FunctionBounds(a.function_bounds.resolve(),root,layout,source.parents[1]) if a.function_bounds else None
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


def atomic_bytes(path, value):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name+'.', suffix='.tmp', delete=False) as stream:
        temporary=Path(stream.name)
        try:
            stream.write(value);stream.flush();os.fsync(stream.fileno())
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
    if function_bounds:code=code[:function_bounds.length(pc,len(code))]
    # Remill must see complete instructions at the window boundary.
    code = code[:decode_size(code, window=True)]
    run("lift-" + hex(pc), [a.remill / "remill-build/bin/lift/remill-lift-23", "--os", "linux", "--arch", "amd64_avx",
                           "--address", str(pc), "--bytes", code.hex(), "--ir_out", output], 120)




repo = source.parents[1]
source_paths = (source / "advance.py", source / "runner.cpp", source / "libc-bulk.h", source / "trace_lookup.h", source / "build_inputs.py", source / "guard_ir.py",source / "trace_preparation.py",source / "discovery_targets.py",source / "trace_candidates.py",
                source / "function_bounds.py",source / "unwind_reader.py",source / "build-function-bounds.py",repo / "scripts/ps4_module.py",
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
    source_paths += (repo / "src/runtime_audio.c",repo / "src/runtime_audio_host.h",repo / "ios/services/guest_audio.h",repo / "CMakeLists.txt", repo / "gpu/CMakeLists.txt",repo / "cmake/OslPinnedSources.cmake",
                     repo / "src/oslgpu.h",repo / "ios/graphics/aot-frontend.cpp",repo / "ios/graphics/save-message-presenter.h",repo / "ios/graphics/bridge-checks.h",repo / "ios/graphics/guest-gpu.h",repo / "ios/graphics/gnm-shader-packets.h",repo / "ios/graphics/gnm-submissions.h",repo / "ios/graphics/guest-presentation.h",repo / "ios/graphics/native-wait.h")
    source_paths += tuple(path for directory in (repo / "gpu/shim",repo / "gpu/shadps4")
                          for path in sorted(directory.rglob("*")) if path.is_file() and path.suffix in (".cpp",".h",".c",".comp",".frag",".vert",".glsl",".hlsl",".cmake"))
    run("gpu-configure",["cmake","-S",repo,"-B",gpu_build,"-DOSL_AOT_ROOT="+str(root),"-DOSL_REMILL_INCLUDE="+str(a.remill / "remill/include")],120)
def source_snapshot():
    return {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
compiled_sources = source_snapshot()
def check_sources():
    if source_snapshot() != compiled_sources:
        raise RuntimeError("Startup sources changed during discovery; rerun before publishing a checkpoint")
    if function_bounds:function_bounds.verify_current()
if a.seed_from:
    old = json.loads(a.seed_from.read_text())
    if old["image_sha256"] != layout["image_sha256"]:
        p.error("Seed PC list belongs to a different prepared image")
    if list(cache.glob("*.ll")):
        p.error("Seed requires a new trace cache")
    def lift_seed(name):
        pc = int(Path(name).stem, 16)
        offset = pc - layout["base"]
        segment = next((s for s in layout["segments"] if s[2] & 1 and s[0] <= offset < s[0] + s[1]), None)
        if segment is None:
            p.error("Seed PC is outside executable memory")
        code = image[offset:min(offset + a.window_bytes, segment[0] + segment[1])]
        lift(pc, code, cache / name)
    # Build the shared decoder once before independent lifters use it.
    decode_size(b"\xc3")
    check_sources()
    seed_started=time.perf_counter()
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for completed,_ in enumerate(pool.map(lift_seed, old["ir"]),1):
            if completed%256==0 or completed==len(old["ir"]):
                print("SEED_PROGRESS:"+json.dumps({"relifted":completed,"total":len(old["ir"]),"seconds":time.perf_counter()-seed_started}),flush=True)
    check_sources()
    print("Re-lifted seed PC list with the isolated backend", flush=True)


service_objects = compile_services(repo, objects / "services", "arm64-apple-macos15", sdk)
check_sources()

preparation = TracePreparationCache(libc_helpers=True,image_sha256=layout['image_sha256'])
static_ledger_path=root/'static-traces.json'
static_ledger={'trace_inputs':identity,'entries':{}}
if static_ledger_path.exists():
    static_ledger=json.loads(static_ledger_path.read_text())
    if static_ledger.get('trace_inputs')!=identity:
        p.error('Speculative trace ledger belongs to different image/backend inputs')
    for name,record in static_ledger['entries'].items():
        if (not re.fullmatch(r'0x[0-9a-f]{1,16}\.ll',name)
                or record.get('status') not in ('accepted','skipped')):
            p.error('Malformed speculative trace ledger record')
        if record['status']!='accepted':continue
        staged=root/'speculative-traces'/name
        if hashlib.sha256(staged.read_bytes()).hexdigest()!=record['raw_sha256']:
            p.error('Speculative staged trace changed: '+name)
        target=cache/name
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest()!=record['raw_sha256']:
                p.error('Speculative promoted trace changed: '+name)
        else:
            # The ledger is published first, leaving a checked recovery source
            # if the process stops before the raw trace is promoted.
            atomic_bytes(target,staged.read_bytes())
pointer_bytes=(root/'image-pointers.bin').read_bytes() if a.prefetch else None
if a.prefetch:candidates(layout,image,pointer_bytes,())  # validate before executing
# A successful narrowing is deterministic for this process's pinned image and
# lifter. Reuse only its exact resulting file hash; x87 remains an explicit gap.
narrowed_traces=set()
def compile_trace(job):
    guarded_path, obj, fingerprint = job
    stamp = obj.with_suffix(".sha256")
    if not obj.exists() or not stamp.exists() or stamp.read_text() != fingerprint:
        temporary = obj.with_suffix(".new.o")
        run("compile-" + obj.stem, [llvm / "clang", "-target", "arm64-apple-macos15",
            "-isysroot", sdk, "-O2", "-c", guarded_path, "-o", temporary])
        stamp.unlink(missing_ok=True)
        temporary.replace(obj)
        stamp.write_text(fingerprint)
    return obj


for round in range(a.rounds):
    preparation_start=time.perf_counter()
    previous_hits,previous_misses=preparation.hits,preparation.misses
    previous_analysis_hits,previous_analysis_misses=preparation.analysis_hits,preparation.analysis_misses
    narrowed_lifted,narrowed_reused,error_paths_retained=0,0,0
    entries = {}
    declarations = set()
    compile_jobs = []
    for ir in sorted(cache.glob("*.ll")):
        raw=ir.read_text()
        analysis=preparation.analyze(raw)
        narrowing_key=(ir.name,analysis.raw_sha256)
        if analysis.extended_precision or (analysis.remill_error and not a.retain_error_paths and narrowing_key not in narrowed_traces):
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
            narrowed_lifted+=1
            raw=ir.read_text()
            analysis=preparation.analyze(raw)
            if analysis.extended_precision:
                raise RuntimeError("Executed block requires unsupported x87 extended precision at " + hex(pc))
            narrowed_traces.add((ir.name,analysis.raw_sha256))
        elif analysis.remill_error and narrowing_key in narrowed_traces:
            narrowed_reused+=1
        elif analysis.remill_error:
            error_paths_retained+=1
        obj = objects / (ir.stem + ".o")
        guarded_path = objects / (ir.stem + ".guarded.ll")
        prepared=preparation.prepare(raw,guarded_path)
        fingerprint=prepared.guarded_sha256
        compile_jobs.append((guarded_path, obj, fingerprint))
        for name in prepared.definitions:
            if int(name, 16) in entries:
                raise RuntimeError("Duplicate lifted entry")
            entries[int(name, 16)] = "sub_" + name
        declarations.update(prepared.declarations)
    # Preserve deterministic archive ordering while each worker owns its output.
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        objs = list(pool.map(compile_trace, compile_jobs))
    print("TRACE_PREPARATION:"+json.dumps({'ir_files':len(objs),'hits':preparation.hits-previous_hits,
          'misses':preparation.misses-previous_misses,'analysis_hits':preparation.analysis_hits-previous_analysis_hits,
          'analysis_misses':preparation.analysis_misses-previous_analysis_misses,'narrowed_lifted':narrowed_lifted,
          'narrowed_reused':narrowed_reused,'error_paths_retained':error_paths_retained,'seconds':time.perf_counter()-preparation_start}),flush=True)
    text = "#pragma once\n"
    for symbol in entries.values():
        text += f'extern "C" Memory *{symbol}(State &,uint64_t,Memory *);\n'
    text += "struct Trace { uint64_t pc; Lifted function; };\nstatic const Trace traces[]={"
    text += ",".join("{" + str(pc) + ",&" + symbol + "}" for pc, symbol in sorted(entries.items())) + "};\n"
    (root / "traces.h").write_text(text)
    thunks = '#include "runtime.h"\n'
    for symbol in sorted(declarations):
        if int(symbol, 16) not in entries:
            thunks += f'extern "C" Memory *sub_{symbol}(State &s,uint64_t pc,Memory *m) {{ return dispatch(s,pc,m); }}\n'
    (root / "thunks.cpp").write_text(thunks)
    check_sources()
    run("runner-build", ["xcrun", "clang++", "-arch", "arm64", "-std=c++17", "-O2", "-DOSL_GAME_CLI=1", "-I" + str(a.remill / "remill/include"),
        "-I" + str(root), "-I" + str(source.parent / "cpu"), "-I" + str(repo / "src"), "-I" + str(repo / "launcher"), source / "runner.cpp", source.parent / "cpu/runtime.cpp", root / "thunks.cpp", response_inputs(root/'runner-inputs.rsp', [*service_objects,*objs]), *link_options(), "-o", root / "guest-startup"], 120)
    state = {"image_sha256": layout["image_sha256"],
             "trace_inputs": identity,
             "ir": {obj.stem + ".ll": hashlib.sha256((cache / (obj.stem + ".ll")).read_bytes()).hexdigest() for obj in objs},
             "headers": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in ("image-layout.h", "traces.h", "thunks.cpp")},
             "sources": compiled_sources}
    state['static_translation']=static_ledger
    state['translation_policy']={'retain_error_paths':a.retain_error_paths,
        'function_bounds':function_bounds.manifest if function_bounds else None,
        'prefetch_unwind_entries':a.prefetch_unwind}
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
        run("gpu-archive",[llvm / "llvm-ar","rcs",archive,response_inputs(mac/'archive-inputs.rsp', [*library_objects,*service_objects,*objs])],120)
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
        command = [gpu_build / "bin/osl-aot-gpu-probe",root / "image.bin",profile]
        command += ["--discover"] if a.guest_budget_ms==2000 else ["--run-ms",str(a.guest_budget_ms)]
        state["execution_budget_ms"]=a.guest_budget_ms
        prefix = "OSL_AOT_GPU_GUEST:"
    attempt = len(list(root.glob("attempt-*.json")))
    def runtime_inputs():
        app0=(root / "App0").resolve();marker=app0 / "eboot.bin"
        script=None
        if "OSL_INPUT_SCRIPT" in os.environ:
            path=Path(os.environ["OSL_INPUT_SCRIPT"]).absolute()
            descriptor=os.open(path,os.O_RDONLY|os.O_NONBLOCK|os.O_NOFOLLOW)
            with os.fdopen(descriptor,'rb') as stream:
                before=os.fstat(stream.fileno())
                if not stat.S_ISREG(before.st_mode) or before.st_size>1024*1024:
                    raise RuntimeError('Input script requires a regular file of at most 1 MiB')
                data=stream.read(1024*1024+1);after=os.fstat(stream.fileno())
                if len(data)>1024*1024 or (before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_size,after.st_mtime_ns,after.st_ctime_ns):
                    raise RuntimeError('Input script changed while fingerprinting')
            script={"path":str(path),"sha256":hashlib.sha256(data).hexdigest()}
        return {"app0":str(app0),"app0_eboot_sha256":hashlib.sha256(marker.read_bytes()).hexdigest() if marker.is_file() else None,
                "input_script":script,
                "asset_marker_only":True}
    state["runtime_inputs"]=runtime_inputs()
    # Replay only a watchdog-only stop, with identical inputs, CPU budget and
    # renderer. Keep each attempt, including one stopped by the operator.
    for replay in range(3):
        name = f"attempt-{attempt + replay:05d}"
        run(name, command, max(30,a.guest_budget_ms/1000+15) if a.gpu_build else 15,
            env=gpu_env if a.gpu_build else dict(os.environ, OSL_GUEST_IDENTITY="1" if a.identity else "0"))
        records = [line.removeprefix(prefix) for line in (root / (name + ".log")).read_text().splitlines() if line.startswith(prefix)]
        if len(records) != 1:
            raise RuntimeError("Expected one startup report in " + name + ".log")
        report = json.loads(records[0])
        check_sources()
        if runtime_inputs() != state["runtime_inputs"]:
            raise RuntimeError("Asset root changed during the startup attempt")
        script=state["runtime_inputs"]["input_script"]
        if script is not None and report.get("input_script_sha256")!=script["sha256"]:
            raise RuntimeError("Guest did not confirm the exact input script snapshot")
        report["trace_count"] = len(entries)
        if replay == 2 or (a.stop_file and a.stop_file.exists()) or not deadline_only(report):
            break
        atomic_json(root / (name + ".json"), report)
        print(f"{name} reached the host deadline. Replaying the same executable.", flush=True)
    state["reference"] = report
    corpus_keys = ("image_sha256", "trace_inputs", "ir", "headers", "sources","runtime_inputs","static_translation","translation_policy") + (("runner_mode","native_gpu","execution_budget_ms") if a.gpu_build else ())
    state["corpus_sha256"] = hashlib.sha256(json.dumps({k: state[k] for k in corpus_keys}, sort_keys=True).encode()).hexdigest()
    atomic_json(root / "build-state.json", state)
    atomic_json(root / (name + ".json"), report)
    atomic_json(root / "latest.json", report)
    print(json.dumps(report), flush=True)
    if a.stop_file and a.stop_file.exists():
        print("Discovery stop requested after completed checkpoint", flush=True)
        break
    pending=missing_targets(report,layout["base"],layout["segments"],entries)
    if not pending:break
    for pc in pending:
        offset=pc-layout["base"]
        segment=next(s for s in layout["segments"] if s[2]&1 and s[0]<=offset<s[0]+s[1])
        length=min(a.window_bytes,segment[0]+segment[1]-offset)
        lift(pc,image[offset:offset+length],cache/(hex(pc)+".ll"))
    if a.prefetch:
        catalog=candidates(layout,image,pointer_bytes,(int(symbol,16) for symbol in declarations),
            function_entries=function_bounds.entries if a.prefetch_unwind else None,
            bounds_sha256=function_bounds.manifest['sha256'] if a.prefetch_unwind else None)
        attempted={int(Path(name).stem,16) for name,record in static_ledger['entries'].items()
                   if record['status']=='accepted' or not a.retain_error_paths or record.get('retain_error_paths',False)}
        known={int(path.stem,16) for path in cache.glob('*.ll')}
        batch=nearest(catalog,pending,known|attempted,a.prefetch)
        staging=root/'speculative-traces';staging.mkdir(exist_ok=True)
        # Lifts own distinct staging/log files. Keep analysis, ledger writes
        # and promotion on this thread in deterministic candidate order.
        # Build the shared decoder once before workers can inspect its stamp.
        if batch:decode_size(b'\xc3')
        def stage_candidate(pc):
            offset=pc-layout['base']
            segment=next(s for s in layout['segments'] if s[2]&1 and s[0]<=offset<s[0]+s[1])
            name=hex(pc)+'.ll';staged=staging/name
            record={'classification':'static candidate; execution unverified','provenance':catalog[pc],
                    'retain_error_paths':a.retain_error_paths}
            try:
                lift(pc,image[offset:min(offset+a.window_bytes,segment[0]+segment[1])],staged)
            except RuntimeError as error:
                # Never suppress an actual demand failure. Speculative failures
                # remain in the ledger and their private logs for investigation.
                record.update(status='skipped',reason=str(error))
            except subprocess.CalledProcessError as error:
                record.update(status='skipped',reason='speculative decoder rejected byte range (exit '+str(error.returncode)+')')
            return pc,name,staged,record
        prefetch_started=time.perf_counter();accepted=skipped=0
        pool=ThreadPoolExecutor(max_workers=a.jobs)
        try:
            for pc,name,staged,record in pool.map(stage_candidate,batch):
                if record.get('status')!='skipped':
                    raw=staged.read_text();analysis=preparation.analyze(raw)
                    record['raw_sha256']=analysis.raw_sha256
                    record['remill_error_paths']=analysis.remill_error
                    if analysis.extended_precision or (analysis.remill_error and not a.retain_error_paths):
                        record.update(status='skipped',reason='speculative unsupported/error path; actual demand remains authoritative')
                    elif analysis.definitions!=(format(pc,'x'),):
                        raise RuntimeError('Unexpected speculative lifted definition at '+hex(pc))
                    else:
                        # Guards and native compilation still run through the same
                        # checked path next round. This changes code coverage only.
                        record['status']='accepted'
                static_ledger['entries'][name]=record
                atomic_json(static_ledger_path,static_ledger)
                if record['status']=='accepted':
                    atomic_bytes(cache/name,staged.read_bytes());accepted+=1
                else:skipped+=1
                print('STATIC_TRANSLATION:'+json.dumps(dict(pc=pc,**record)),flush=True)
        finally:
            # An unexpected result must cancel queued work and join active
            # lifters before the caller reports failure or edits its inputs.
            pool.shutdown(wait=True,cancel_futures=True)
        print('STATIC_PREFETCH:'+json.dumps({'candidates':len(batch),'accepted':accepted,'skipped':skipped,
            'workers':a.jobs,'seconds':time.perf_counter()-prefetch_started,
            'classification':'static staging only; execution unverified'}),flush=True)
