#!/usr/bin/env python3
"""Wrap the private iPhoneOS driver in an auditable, signed local framework."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--headers', required=True, type=Path)
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Use a mounted external driver build directory')
manifest = json.loads((root / 'inputs.json').read_text())
platform = manifest.get('platform', 'iphoneos')
if platform not in ('iphoneos', 'iphonesimulator'):
    p.error('This framework packer accepts iOS platforms only')
driver = root / ('build-' + platform) / 'src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib'
if hashlib.sha256(driver.read_bytes()).hexdigest() != manifest['driver_sha256']:
    p.error('Driver differs from its audited input manifest')
framework = root / 'KosmicKrisp.framework'
if framework.exists():
    previous = root / 'framework-backups' / hashlib.sha256((framework / 'KosmicKrisp').read_bytes()).hexdigest()[:16]
    previous.parent.mkdir(exist_ok=True)
    if not previous.exists():
        shutil.copytree(framework, previous)
    shutil.rmtree(framework)
framework.mkdir()
binary = framework / 'KosmicKrisp'
shutil.copy2(driver, binary)
subprocess.run(['install_name_tool', '-id', '@rpath/KosmicKrisp.framework/KosmicKrisp', str(binary)], check=True)
info = {'CFBundleIdentifier': 'com.wadscorp.odinsphere.kosmickrisp', 'CFBundleName': 'KosmicKrisp',
        'CFBundleExecutable': 'KosmicKrisp', 'CFBundlePackageType': 'FMWK', 'CFBundleVersion': '1',
        'CFBundleShortVersionString': '0.1', 'MinimumOSVersion': '27.2',
        'CFBundleSupportedPlatforms': ['iPhoneSimulator' if platform == 'iphonesimulator' else 'iPhoneOS'], 'OSLDriverSHA256': manifest['driver_sha256']}
(framework / 'Info.plist').write_bytes(plistlib.dumps(info))
(framework / 'driver-inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
shutil.copy2(root / 'mesa/docs/license.rst', framework / 'Mesa-license.txt')
headers = root / 'Headers'
headers.mkdir(exist_ok=True)
for name in ('vulkan', 'vk_video'):
    shutil.copytree(a.headers / name, headers / name, dirs_exist_ok=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(framework)], check=True)
print(framework)
