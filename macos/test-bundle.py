#!/usr/bin/env python3
"""Run isolated packaged-app checks against a player's prepared files and copied save."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ("app", "app0", "image", "seed", "output"):
        p.add_argument("--" + key, required=True, type=Path)
    a = p.parse_args()
    if a.output.exists():
        p.error("--output must be a new directory")
    a.output.mkdir(parents=True)
    app = a.output / "Relocated Ω" / a.app.name
    app.parent.mkdir()
    subprocess.run(["ditto", str(a.app), str(app)], check=True)
    original = hashes(app)
    data = a.output / "Données Ω"
    data.mkdir()
    shutil.copytree(a.seed, data / "user")
    saved = {k: v for k, v in hashes(data / "user").items() if "OS2SAVEFILE" in k}
    if not saved:
        p.error("--seed requires a real copied Odin save")
    exe = app / "Contents/MacOS/OdinSphere"
    env = {"HOME": str(Path.home()), "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
           "OSL_DATA_DIR": str(data), "OSL_LOGS_DIR": str(data / "logs"),
           "OSL_AUDIO": "none", "OSL_FRAME_STATS": "1"}
    results = {}

    def check(name, args, extra=None, expected=0):
        with (a.output / (name + ".log")).open("w") as out:
            r = subprocess.run([str(exe), *args], env=env | (extra or {}), stdout=out, stderr=subprocess.STDOUT, timeout=120)
        results[name] = r.returncode
        assert r.returncode == expected, (name, r.returncode)

    check("configure", ["--configure", str(a.app0), str(a.image)])
    config = data / "installation.plist"
    before = config.read_bytes()
    check("reject-invalid-configuration", ["--configure", str(a.output / "absent"), str(a.image)], expected=1)
    assert config.read_bytes() == before
    config.write_bytes(plistlib.dumps({"app0": "/Volumes/OSL-unmounted-fixture/Game", "image": str(a.image)}))
    check("reject-unmounted-disk", ["--bootstrap-check"], expected=1)
    config.write_bytes(before)
    check("bootstrap", ["--bootstrap-check"])
    report = json.loads((a.output / "bootstrap.log").read_text())
    assert report["avx"] and Path(report["icd"]).is_file()
    check("imports", ["--runner", str(a.image / "boot.bin"), "--check-imports", "--app0", str(a.app0),
                       "--user", str(data / "user"), "--logs", str(data / "imports")])
    shots = a.output / "launcher-pages"
    shots.mkdir()
    check("launcher", ["--launcher"], {"OSL_LAUNCHER_SHOTS": str(shots)})
    assert len(list(shots.glob("*.png"))) == 7
    # The first process must retain exclusive ownership through native -> Rosetta exec.
    runtime_env = env | {"OSL_TEST_PRESENT_LIMIT": "300", "DYLD_PRINT_LIBRARIES": "1"}
    with (a.output / "run-1.log").open("w") as out:
        child = subprocess.Popen([str(exe), "--osl-skip-launcher", "--timeout", "90"], env=runtime_env, stdout=out, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                assert child.poll() is None
                with (data / "instance.lock").open("a") as lock:
                    try:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        fcntl.flock(lock, fcntl.LOCK_UN)
                    except BlockingIOError:
                        break
                time.sleep(0.02)
            else:
                raise AssertionError("The running app did not acquire its data lock")
            check("reject-concurrent-profile", ["--osl-skip-launcher"], {"OSL_TEST_PRESENT_LIMIT": "300"}, expected=1)
            results["run-1"] = child.wait(timeout=110)
            assert results["run-1"] == 0
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
    check("relaunch", ["--osl-skip-launcher", "--timeout", "90"], {"OSL_TEST_PRESENT_LIMIT": "300"})
    log = (data / "logs/bootstrap.log").read_text(errors="replace")
    assert log.count("test frame limit reached (300 frames)") == 2
    assert str(app / "Contents/Frameworks/libvulkan_kosmickrisp.dylib") in log
    assert "CPU triangle fan expansion on" in log and "GPU frame lead bound 2" in log
    # Switch the same profile back to native resolution after the 4K launch.
    # Verify VideoOut so a launcher setting that never reaches the guest fails the check.
    settings = data / "user/osl.ini"
    original_settings = settings.read_bytes() if settings.exists() else None
    bootstrap_log = data / "logs/bootstrap.log"
    try:
        for key, width, height in (("4K", 3840, 2160), ("native", 1920, 1080)):
            size = f"{width}x{height}"
            settings.write_text(f"render_resolution={key}\naspect_ratio=16:9\nwindow_width=960\nwindow_height=540\n")
            start_offset = bootstrap_log.stat().st_size
            check(f"render-{key.lower()}", ["--osl-skip-launcher", "--timeout", "90"],
                  {"OSL_TEST_PRESENT_LIMIT": "300"})
            with bootstrap_log.open("rb") as stream:
                stream.seek(start_offset)
                run_log = stream.read().decode(errors="replace")
            expected = [
                f"render resolution {size} ({key})",
                f"VideoOut source {size} pitch {width}",
                "test frame limit reached (300 frames)",
            ]
            if key == "4K":
                expected.append(f"present source {size} -> swapchain")
            for line in expected:
                assert line in run_log, f"{key} packaged run missing {line!r}"
    finally:
        if original_settings is None:
            settings.unlink(missing_ok=True)
        else:
            settings.write_bytes(original_settings)
    after = hashes(data / "user")
    assert all(after.get(k) == v for k, v in saved.items()), "Copied progress changed during bounded startup checks"
    assert original == hashes(app), "The running bundle was modified"
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    results.update({"save_unchanged": True, "bundle_unchanged": True, "launcher_pages": 7,
                    "bundled_driver_loaded": True, "limitations": "Startup/relaunch integrity; no automated claim of loading the visible saved slot or full gameplay."})
    (a.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
