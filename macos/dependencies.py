#!/usr/bin/env python3
"""Reproduce the pinned Darwin dependencies on a mounted external build disk."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

source = Path(__file__).resolve().parent
lock = json.loads((source / "dependency-lock.json").read_text())
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("output", type=Path)
a = p.parse_args()
root = a.output.resolve()
if len(root.parts) < 3 or root.parts[1] != "Volumes" or not Path("/Volumes", root.parts[2]).is_mount():
    p.error("Dependencies require a mounted external disk")
root.mkdir(parents=True, exist_ok=True)


def run(*args, cwd=None, env=None):
    subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True)


driver = root / "driver-builder"
if not driver.exists():
    run("git", "clone", "--no-checkout", lock["driver_build_repository"], driver)
    run("git", "-C", driver, "checkout", "--detach", lock["driver_build_commit"])
pin = subprocess.check_output(["git", "-C", str(driver), "rev-parse", "HEAD"], text=True).strip()
if pin != lock["driver_build_commit"] or subprocess.check_output(["git", "-C", str(driver), "status", "--porcelain"], text=True).strip():
    raise SystemExit("Driver builder differs from its pin; leaving it untouched")
vk = root / "vulkan"
env = dict(os.environ, SPARTAN_MACOS_DEPS=str(vk), CMAKE_BUILD_PARALLEL_LEVEL="8",
           KOSMICKRISP=lock["kosmickrisp_commit"], VULKAN=lock["vulkan_tag"])
run("bash", driver / "packaging/macos/build-deps.sh", env=env)
sources = {}
for name, item in lock["archives"].items():
    archive = root / (name + ".tar.gz")
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        urllib.request.urlretrieve(item["url"], temporary)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != item["sha256"]:
            raise SystemExit("Download hash mismatch: " + name)
        temporary.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != item["sha256"]:
        raise SystemExit("Cached archive hash mismatch: " + name)
    dest = root / "src" / name
    if not dest.exists():
        extraction = root / "extract" / name
        extraction.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(extraction, filter="data")
        folders = list(extraction.iterdir())
        if len(folders) != 1 or not folders[0].is_dir():
            raise SystemExit("Unexpected archive layout: " + name)
        dest.parent.mkdir(exist_ok=True)
        folders[0].rename(dest)
    sources[name] = dest

prefix = root / "crypto-png"
if not (prefix / "lib/libcrypto.a").exists():
    run("./Configure", "darwin64-x86_64-cc", "no-shared", "no-tests", "--prefix=" + str(prefix),
        "--libdir=lib", cwd=sources["openssl"], env=dict(os.environ, CC="clang", CFLAGS="-arch x86_64 -O2"))
    run("make", "-j8", cwd=sources["openssl"])
    run("make", "install_sw", cwd=sources["openssl"])


def cmake(name, *options):
    build = root / (name + "-build")
    run("cmake", "-S", sources[name], "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_OSX_ARCHITECTURES=x86_64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=27.2", *options)
    run("cmake", "--build", build, "--parallel", "8")


cmake("png", "-DPNG_SHARED=OFF", "-DPNG_TESTS=OFF", "-DCMAKE_INSTALL_PREFIX=" + str(prefix))
run("cmake", "--install", root / "png-build")
cmake("freetype", "-DBUILD_SHARED_LIBS=OFF", "-DFT_DISABLE_ZLIB=ON", "-DFT_DISABLE_BZIP2=ON",
      "-DFT_DISABLE_PNG=ON", "-DFT_DISABLE_HARFBUZZ=ON", "-DFT_DISABLE_BROTLI=ON")
cmake("sdl", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF")
inputs = {"vulkan_prefix": str(vk), "deps_prefix": str(prefix), "freetype_build": str(root / "freetype-build"),
          "freetype_include": str(sources["freetype"] / "include"), "sdl_build": str(root / "sdl-build"),
          "license_roots": [str(s) for s in sources.values()]}
(root / "inputs.json").write_text(json.dumps(inputs, indent=2) + "\n")
print(json.dumps(inputs, indent=2))
