#!/usr/bin/env python3
"""Run the same offscreen GPU adapter probe against an audited native arm64 Mac driver."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--headers', required=True, type=Path)
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Use a mounted external driver directory')
manifest = json.loads((root / 'inputs.json').read_text())
if manifest.get('platform') != 'macosx':
    p.error('Native CLI requires the macOS driver build, not an iOS dylib')
source = Path(__file__).resolve().parent
driver = root / 'build-macosx/src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib'
if hashlib.sha256(driver.read_bytes()).hexdigest() != manifest['driver_sha256']:
    p.error('Driver differs from audited inputs')
command = ['xcrun', 'clang++', '-std=c++17', '-target', 'arm64-apple-macos27.2', '-fobjc-arc',
           '-I' + str(a.headers), str(source / 'probe.mm'), str(source / 'native-main.mm'),
           '-L' + str(driver.parent), '-lvulkan_kosmickrisp', '-Wl,-rpath,' + str(driver.parent),
           '-framework', 'Foundation', '-framework', 'Metal', '-o', str(root / 'native-probe')]
with (root / 'native-probe-build.log').open('w') as log:
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
with (root / 'native-probe.log').open('w') as log:
    subprocess.run([str(root / 'native-probe'), str(root / 'native-probe-results.json')], stdout=log, stderr=subprocess.STDOUT, timeout=20, check=True)
result = json.loads((root / 'native-probe-results.json').read_text())
assert result['passed'] and result['matched_words'] == 64 and not result['game_frames']
result['driver_sha256'] = manifest['driver_sha256']
result['sources'] = {x.name: hashlib.sha256(x.read_bytes()).hexdigest() for x in (source / 'probe.mm', source / 'native-main.mm')}
(root / 'native-probe-verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
