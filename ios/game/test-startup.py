#!/usr/bin/env python3
"""Validate startup equivalence and private-image rejection using copied fixtures."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
if a.output.exists():
    p.error('Use a new private external fixture directory')
output = a.output.resolve()
if len(output.parts) < 3 or output.parts[1] != 'Volumes' or not Path('/Volumes', output.parts[2]).is_mount():
    p.error('Fixture output requires a mounted external disk')
output.mkdir(parents=True)
image = output / 'image.bin'
pointers = output / 'image-pointers.bin'
shutil.copy2(a.root / 'image.bin', image)
shutil.copy2(a.root / 'image-pointers.bin', pointers)

def run(name, identity=True):
    result = subprocess.run([str(a.root / 'guest-startup'), str(image)], capture_output=True, text=True,
                            env=dict(os.environ, OSL_GUEST_IDENTITY='1' if identity else '0'), timeout=15, check=True)
    records = [line.removeprefix("OSL_STARTUP_RESULT:") for line in result.stdout.splitlines() if line.startswith("OSL_STARTUP_RESULT:")]
    if len(records) != 1: raise RuntimeError("Expected one startup report")
    report = json.loads(records[0])
    (output / (name + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    return report

fixed, first, second = run('fixed', False), run('identity-1'), run('identity-2')
keys = ('stage', 'completed_modules', 'trace_count', 'dispatches', 'error', 'missing_pc',
        'fault_address', 'flexible_bytes', 'missing_service', 'heap_api_registered')
assert {k: fixed[k] for k in keys} == {k: first[k] for k in keys} == {k: second[k] for k in keys}
assert first['completed_modules'] == 2 and first['fault_address'] == 0 and not first['game_booted']
with image.open('r+b') as stream:
    original = stream.read(1)
    stream.seek(0)
    stream.write(bytes([original[0] ^ 1]))
assert run('modified-image')['error'] == 'incorrect image hash'
with image.open('r+b') as stream:
    stream.write(original)
    stream.truncate(image.stat().st_size - 1)
assert run('truncated-image')['error'] == 'incorrect image size or unavailable file'
shutil.copy2(a.root / 'image.bin', image)
with pointers.open('r+b') as stream:
    byte = stream.read(1)
    stream.seek(0)
    stream.write(bytes([byte[0] ^ 1]))
assert run('modified-pointers')['error'] == 'incorrect image pointer table hash'
pointers.rename(output / 'invalid-pointers.bin')
assert run('missing-pointers')['error'] == 'incorrect image pointer table hash'
report = {'fixed_and_identity_checkpoint_match': True, 'repeated_identity_checkpoint_match': True,
          'corrupt_image_rejected': True, 'truncated_image_rejected': True,
          'corrupt_pointer_table_rejected': True, 'missing_pointer_table_rejected': True,
          'game_booted': False}
(output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
