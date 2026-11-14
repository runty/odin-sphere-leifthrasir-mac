#!/usr/bin/env python3
"""Privately lift three pinned Odin routines and compare with their Mac x86-64 execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--work", required=True, type=Path)
p.add_argument("--app0", required=True, type=Path)
p.add_argument("--image", required=True, type=Path)
p.add_argument("--remill", required=True, type=Path)
p.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
a = p.parse_args()
root = a.work.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Work requires a mounted external disk")
root.mkdir(parents=True, exist_ok=True)
remill_pin = subprocess.check_output(["git", "-C", str(a.remill / "remill"), "rev-parse", "HEAD"], text=True).strip()
if remill_pin != "66c488c9911fd9b368e4d0c9647d74239f8bba8b":
    p.error("Unexpected Remill revision")
source = Path(__file__).resolve().parent
repo = source.parents[1]
sys.path.insert(0, str(repo / "scripts"))
from ps4_module import read_module

expected = {
    "eboot.bin": "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6",
    "sce_module/libc.prx": "e436eb5bd6bc6dd706edea917da1e52460ce760971b8f067149f479fd3410bfe",
    "sce_module/libSceFios2.prx": "cf4f3e57d05df4cd22a54de08ff790d5aca979f02bd5ed5c70c10ac7f5f69410"}
for name, sha in expected.items():
    if hashlib.sha256((a.app0 / name).read_bytes()).hexdigest() != sha:
        p.error("Unsupported module hash: " + name)
boot = (a.image / "boot.bin").read_bytes()
metadata = json.loads((a.image / "image.json").read_text())
if boot[:8] != b"OSLBOOT1" or metadata["eboot_sha256"] != expected["eboot.bin"] or len(boot) <= metadata["image_bytes"]:
    p.error("Boot image metadata mismatch")
image = boot[-metadata["image_bytes"]:]
(root / "image.raw").write_bytes(image)
module_bases = {m["file"]: int(m["base"], 16) for m in metadata["modules"]}
if module_bases != {"libc.prx": 0xca0000, "libSceFios2.prx": 0xd80000}:
    p.error("Unexpected module layout")
libc = read_module(a.app0 / "sce_module/libc.prx")
strlen = next(s for s in libc["symbols"] if s["name"].startswith("j4ViWNHEgww#"))
if (strlen["value"], strlen["size"]) != (0x30630, 221):
    p.error("Unexpected libc strlen export")
base = 0x10000000
routines = [("eboot_getter", 0x44c0, 25), ("libc_strlen", 0xcd0630, 221), ("fios_setter", 0xdc4810, 12)]
routine_hashes = {
    "eboot_getter": "96090404d3a78e51437f423f1bcd84e686f25cad35b8c3d2f792f1485c780c63",
    "libc_strlen": "32728852ba435c9de3a588dcaafead5115d1391785f72a604189b7149574c24a",
    "fios_setter": "3bcca201cb95171ae7bb94d854946bd0e00d561872d309ae62b8c436a42592e9"}
storage = 0xef4560
cases = [(0, 0x44c0, n, 0) for n in range(3)]
cases += [(1, 0xcd0630, offset, length) for offset in range(16) for length in (0, 1, 2, 7, 8, 9, 15, 16, 31, 64, 255, 4095)]
cases += [(2, 0xdc4810, n, 0) for n in (0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678)]


def run(name, command, env=None):
    with (root / (name + ".log")).open("w") as out:
        subprocess.run([str(x) for x in command], stdout=out, stderr=subprocess.STDOUT, check=True, env=env, timeout=300)


entries = {}
traces = []
for name, offset, size in routines:
    code = image[offset:offset + size]
    if hashlib.sha256(code).hexdigest() != routine_hashes[name]:
        p.error("Unexpected routine bytes: " + name)
    traces.append({"name": name, "offset": hex(offset), "size": size, "sha256": hashlib.sha256(code).hexdigest()})
    run(name + "-lift", [a.remill / "remill-build/bin/lift/remill-lift-23", "--os", "linux", "--arch", "amd64_avx",
        "--address", str(base + offset), "--bytes", code.hex(), "--ir_out", root / (name + ".ll")])
    for symbol in re.findall(r"^define ptr @sub_([0-9a-f]+)\(", (root / (name + ".ll")).read_text(), re.M):
        entries[int(symbol, 16)] = "sub_" + symbol
corpus = hashlib.sha256(json.dumps({"modules": expected, "traces": traces, "cases": cases}, sort_keys=True).encode()).hexdigest()


def header(reference=None):
    text = "#pragma once\n#include <cstdint>\n"
    text += f"static constexpr uint64_t image_base={base}, image_size={len(image)}, fios_storage={storage};\n"
    text += f'static const char *corpus_hash="{corpus}";\n'
    text += "struct FixtureCase { unsigned kind; uint64_t offset,a,b,expected,flags,observed; };\nstatic const FixtureCase fixture_cases[]={\n"
    for i, case in enumerate(cases):
        result = reference[i] if reference else (0, 0, 0)
        text += "{" + ",".join(str(n) + "ULL" for n in (*case, *result)) + "},\n"
    text += "};\n#ifndef OSL_NATIVE_ORACLE\n"
    for symbol in entries.values():
        text += f'extern "C" Memory *{symbol}(State &,uint64_t,Memory *);\n'
    text += "struct Entry { uint64_t pc; Lifted function; };\nstatic const Entry entries[]={"
    text += ",".join("{" + str(pc) + ",&" + symbol + "}" for pc, symbol in entries.items()) + "};\n#endif\n"
    (root / "fixtures.h").write_text(text)


header()
(root / "invoke.s").write_text(".text\n.globl _osl_invoke\n_osl_invoke:\nsubq $8,%rsp\nmovq %rdi,%r11\nmovq %rsi,%rdi\nxorl %eax,%eax\ncallq *%r11\npushfq\npopq %rdx\naddq $8,%rsp\nretq\n")
run("oracle-build", ["xcrun", "clang++", "-arch", "x86_64", "-std=c++17", "-O2", "-DOSL_NATIVE_ORACLE=1", "-I" + str(root),
                     source / "oracle.cpp", root / "invoke.s", "-o", root / "oracle"])
run("oracle", [root / "oracle", root / "image.raw"], dict(os.environ, ROSETTA_ADVERTISE_AVX="1"))
reference = []
for i, line in enumerate((root / "oracle.log").read_text().splitlines()):
    kind, result, flags, observed = map(int, line.split())
    assert kind == cases[i][0]
    # Independent semantic sanity checks on the selected routines.
    wanted = int(cases[i][2] != 2) if kind == 0 else cases[i][3] if kind == 1 else 0
    assert result == wanted, (cases[i], result)
    if kind == 2:
        assert observed == cases[i][2]
    reference.append((result, flags, observed))
assert len(reference) == len(cases)
header(reference)
for platform, target, sdk in (("mac", "arm64-apple-macos15", "macosx"),
                              ("ios", "arm64-apple-ios18", "iphoneos"),
                              ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator")):
    directory = root / platform
    directory.mkdir(exist_ok=True)
    sysroot = subprocess.check_output(["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True).strip()
    objects = []
    for name, _, _ in routines:
        obj = directory / (name + ".o")
        run(platform + "-" + name, [a.llvm / "bin/clang", "-target", target, "-isysroot", sysroot,
                                  "-O2", "-c", root / (name + ".ll"), "-o", obj])
        objects.append(obj)
    for name in ("runtime", "harness"):
        obj = directory / (name + ".o")
        run(platform + "-" + name, ["xcrun", "--sdk", sdk, "clang++", "-target", target, "-isysroot", sysroot,
            "-std=c++17", "-O2", "-I" + str(a.remill / "remill/include"), "-I" + str(root), "-c", source / (name + ".cpp"), "-o", obj])
        objects.append(obj)
    run(platform + "-archive", [a.llvm / "bin/llvm-ar", "rcs", directory / "libodin-cpu.a", *objects])
    if platform == "mac":
        (root / "cli.cpp").write_text('#include <cstdio>\n#include <cstring>\nextern "C" const char *osl_mobile_cpu_report();\nint main(){auto p=osl_mobile_cpu_report();puts(p);return strstr(p,"\\"failed\\":0")==nullptr;}\n')
        run("mac-link", ["xcrun", "clang++", "-arch", "arm64", root / "cli.cpp", directory / "libodin-cpu.a", "-o", root / "cpu-check"])
        run("mac-results", [root / "cpu-check"])
manifest = {"profile": "CUSA05083 US base 1.00", "module_hashes": expected, "corpus_sha256": corpus,
            "routines": traces, "cases_per_worker": len(cases), "oracle": "x86-64 game bytes executed under Rosetta",
            "remill_commit": remill_pin,
            "lifter_sha256": hashlib.sha256((a.remill / "remill-build/bin/lift/remill-lift-23").read_bytes()).hexdigest(),
            "limitations": "Three bounded routines; no module startup, callbacks, GPU rendering or full-game claim."}
(root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print((root / "mac-results.log").read_text())
