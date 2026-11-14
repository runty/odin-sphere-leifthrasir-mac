#!/usr/bin/env python3
"""Validate startup equivalence and private-image rejection using copied fixtures."""
import argparse
import hashlib
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
                            env=dict(os.environ, OSL_GUEST_IDENTITY='1' if identity else '0',
                                     OSL_STARTUP_DISPATCH_TRACE=str(output / (name + '.dispatch.txt'))), timeout=15, check=True)
    records = [line.removeprefix("OSL_STARTUP_RESULT:") for line in result.stdout.splitlines() if line.startswith("OSL_STARTUP_RESULT:")]
    if len(records) != 1: raise RuntimeError("Expected one startup report")
    report = json.loads(records[0])
    (output / (name + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    return report

fixed, first, second = run('fixed', False), run('identity-1'), run('identity-2')
keys = ('stage', 'completed_modules', 'trace_count', 'error', 'missing_pc',
        'fault_address', 'flexible_bytes', 'missing_service', 'heap_api_registered')
assert {k: fixed[k] for k in keys} == {k: first[k] for k in keys} == {k: second[k] for k in keys}
raw_traces = [(output / (name + '.dispatch.txt')).read_text().splitlines()
              for name in ('fixed', 'identity-1', 'identity-2')]
assert [len(t) for t in raw_traces] == [d['dispatches'] for d in (fixed, first, second)]
def canonical(trace):
    # This exact executed instruction range tests whether a source pointer is
    # inside an object's inline storage. Relative pointer ordering changes with
    # ASLR. The extra two blocks perform the second range comparison and then
    # rejoin 10750447; no other difference is accepted.
    pattern = ['10750418', '10750420', '1075042c', '10750447']
    result, normalized, i = [], 0, 0
    while i < len(trace):
        if trace[i:i+4] == pattern:
            code = image.read_bytes()[0x750418:0x750447]
            assert hashlib.sha256(code).hexdigest() == '1a7a2f73e96f059b427a81e9074d60c610bfcc12fa59fbdffaedfeec7b2f01cf'
            result.extend(['10750418', '10750447']); normalized += 1; i += 4
        else:
            result.append(trace[i]); i += 1
    return result, normalized
canonical_traces = [canonical(t) for t in raw_traces]
assert canonical_traces[0][0] == canonical_traces[1][0] == canonical_traces[2][0], 'Unknown dispatch divergence'
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
          'canonical_dispatch_traces_match': True,
          'raw_dispatch_counts': [d['dispatches'] for d in (fixed, first, second)],
          'byte_pinned_range_checks_normalized': [t[1] for t in canonical_traces],
          'corrupt_image_rejected': True, 'truncated_image_rejected': True,
          'corrupt_pointer_table_rejected': True, 'missing_pointer_table_rejected': True,
          'game_booted': False}
(output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
