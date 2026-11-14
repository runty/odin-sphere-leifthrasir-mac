#!/usr/bin/env python3
"""Package a relocatable local Mac alpha. Game files and writable data stay outside."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def external(path):
    path = path.resolve()
    if len(path.parts) < 3 or path.parts[1] != "Volumes" or not Path("/Volumes", path.parts[2]).is_mount():
        raise ValueError(f"Output requires a mounted external disk: {path}")
    return path


def system(path):
    return path.startswith(("/usr/lib/", "/System/Library/"))


def dependencies(path):
    return [line.strip().split(" (", 1)[0] for line in run("otool", "-L", path).splitlines()[1:]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--vulkan-prefix", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--license-root", type=Path, action="append", default=[], help="Additional dependency source/license folder")
    args = parser.parse_args()
    if args.output.is_symlink():
        parser.error("Refusing a symlink output")
    output = external(args.output)
    if output.suffix != ".app":
        parser.error("--output must end in .app")
    output.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=".odin-package-", dir=output.parent))
    app = work / output.name
    contents = app / "Contents"
    macos, frameworks, resources = [contents / name for name in ("MacOS", "Frameworks", "Resources")]
    for directory in (macos, frameworks, resources):
        directory.mkdir(parents=True, exist_ok=True)
    records = []
    copied = {}

    def copy_binary(original, destination, library=False):
        original = original.resolve(strict=True)
        if library and original in copied:
            return copied[original]
        if library:
            if destination.exists():
                raise ValueError(f"Conflicting library basename: {destination.name}")
            copied[original] = destination
        if "x86_64" not in run("lipo", "-archs", original):
            raise ValueError(f"Runner dependencies require x86_64: {original}")
        shutil.copy2(original, destination)
        os.chmod(destination, 0o755)
        records.append({"file": str(destination.relative_to(contents)), "source": str(original),
                        "input_sha256": hashlib.sha256(original.read_bytes()).hexdigest()})
        # Strip old signatures before changing load commands; re-sign below.
        subprocess.run(["codesign", "--remove-signature", str(destination)], capture_output=True)
        if library:
            run("install_name_tool", "-id", "@rpath/" + destination.name, destination)
        for dep in dependencies(original):
            if system(dep) or (library and dep in ("@rpath/" + original.name, str(original))):
                continue
            if dep.startswith("@rpath/"):
                source = args.vulkan_prefix / "lib" / dep[len("@rpath/"):]
            elif dep.startswith("@loader_path/"):
                source = original.parent / dep[len("@loader_path/"):]
            elif dep.startswith("/"):
                source = Path(dep)
            else:
                raise ValueError(f"Unresolved dependency {dep} in {original}")
            target = copy_binary(source, frameworks / source.name, True)
            run("install_name_tool", "-change", dep, "@rpath/" + target.name, destination)
        # Use loader-relative paths even for the ICD loaded through dlopen.
        rpath = "@loader_path" if library else "@executable_path/../Frameworks"
        run("install_name_tool", "-add_rpath", rpath, destination)
        return destination

    try:
        copy_binary(args.build_dir / "bin/odin_sphere_leifthrasir", macos / "odin-sphere-runner")
        copy_binary(args.vulkan_prefix / "lib/libvulkan_kosmickrisp.dylib", frameworks / "libvulkan_kosmickrisp.dylib", True)
        run("xcrun", "clang", "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=27.2", "-O2", "-fobjc-arc",
            "-Wall", "-Wextra", "-Werror", SOURCE / "macos/bootstrap.m", "-framework", "AppKit", "-framework", "Foundation",
            "-o", macos / "OdinSphere")
        shutil.copytree(SOURCE / "assets/launcher", resources / "assets/launcher")
        icd = resources / "vulkan/icd.d/kosmickrisp.json"
        icd.parent.mkdir(parents=True)
        icd.write_text(json.dumps({"file_format_version": "1.0.1", "ICD": {
            "api_version": "1.4.363", "library_path": "../../../Frameworks/libvulkan_kosmickrisp.dylib"}}, indent=2) + "\n")
        iconset = work / "OdinSphere.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256):
            for scale in (1, 2):
                run("sips", "-z", size * scale, size * scale, SOURCE / "packaging/icons/app-icon-256.png", "--out",
                    iconset / f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png")
        run("iconutil", "-c", "icns", iconset, "-o", resources / "OdinSphere.icns")
        (contents / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "OdinSphere", "CFBundleIdentifier": "com.wadscorp.odinsphere",
            "CFBundleName": "Odin Sphere", "CFBundleDisplayName": "Odin Sphere Leifthrasir",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1", "CFBundleShortVersionString": "0.2.3",
            "CFBundleIconFile": "OdinSphere.icns", "LSMinimumSystemVersion": "27.2",
            "NSHighResolutionCapable": True, "LSArchitecturePriority": ["arm64", "x86_64"],
            "LSEnvironment": {"ROSETTA_ADVERTISE_AVX": "1"}}))
        notices = resources / "Notices"
        for root in [SOURCE, args.vulkan_prefix / "share/licenses", args.vulkan_prefix / "src/mesa-kosmickrisp", args.vulkan_prefix / "src/vulkan-loader", *args.license_root]:
            if not root.is_dir():
                continue
            # Only inventory existing license texts; no game data or signing material.
            for base, dirs, names in os.walk(root):
                dirs[:] = [d for d in dirs if d not in (".git", "build", "out", "_deps", "game", "deps", "node_modules")]
                for name in names:
                    if name.upper().startswith(("LICENSE", "COPYING", "NOTICE")) or name.upper() in ("FTL.TXT", "GPLV2.TXT", "OFL.TXT"):
                        path = Path(base) / name
                        if path.stat().st_size > 1024 * 1024:
                            continue
                        target = notices / root.name / path.relative_to(root)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(path, target)
        manifest = {"revision": run("git", "-C", SOURCE, "rev-parse", "HEAD"),
                    "dirty": bool(run("git", "-C", SOURCE, "status", "--porcelain")),
                    "compiler": run("xcrun", "clang", "--version"), "sdk": run("xcrun", "--show-sdk-version"),
                    "local_developer_alpha": True, "game_data_bundled": False, "inputs": records}
        manifest["dependency_lock"] = json.loads((SOURCE / "macos/dependency-lock.json").read_text())
        manifest["dependency_sources"] = []
        for directory in ("src/mesa-kosmickrisp", "src/mesa-kosmickrisp/externals/mesa", "src/vulkan-loader", "src/vulkan-headers"):
            path = args.vulkan_prefix / directory
            if (path / ".git").exists():
                diff = run("git", "-C", path, "diff", "HEAD")
                manifest["dependency_sources"].append({"path": directory,
                    "revision": run("git", "-C", path, "rev-parse", "HEAD"),
                    "diff_sha256": hashlib.sha256(diff.encode()).hexdigest(),
                    "dirty": bool(run("git", "-C", path, "status", "--porcelain"))})
        for path in frameworks.iterdir():
            run("codesign", "--force", "--sign", "-", path)
        run("codesign", "--force", "--sign", "-", macos / "odin-sphere-runner")
        manifest["binaries"] = {str(binary.relative_to(contents)): {
            "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "architectures": run("lipo", "-archs", binary),
            "dependencies": dependencies(binary)} for binary in [*macos.iterdir(), *frameworks.iterdir()]}
        (resources / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        run("codesign", "--force", "--sign", "-", app)
        run("codesign", "--verify", "--deep", "--strict", app)
        for binary in [macos / "odin-sphere-runner", *frameworks.iterdir()]:
            for dep in dependencies(binary):
                if not system(dep) and not dep.startswith("@rpath/"):
                    raise ValueError(f"Non-relocatable dependency: {dep}")
        previous = None
        if output.exists():
            backup = output.parent / "app-backups"
            backup.mkdir(exist_ok=True)
            previous = backup / (output.stem + "-" + work.name + ".app")
            output.rename(previous)
        try:
            app.rename(output)
        except BaseException:
            if previous is not None:
                previous.rename(output)
            raise
        print(output)
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
