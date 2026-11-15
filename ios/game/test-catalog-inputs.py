#!/usr/bin/env python3
"""Check frozen-layout binding without mutating private game inputs."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
for name in ('root', 'checkpoint', 'output'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
root, checkpoint, out = (value.resolve() for value in (a.root, a.checkpoint, a.output))
for path in (root, checkpoint, out):
    if len(path.parts) < 3 or path.parts[1] != 'Volumes' or not Path('/Volumes', path.parts[2]).is_mount():
        p.error('Use mounted external paths')
if out.exists(): p.error('--output must be new')
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
inputs = [root/name for name in ('image.json', 'image.bin', 'image-pointers.bin')]
inputs += [checkpoint/name for name in ('build-state.json', 'image-layout.h', 'thunks.cpp')]
before = {str(path): digest(path) for path in inputs}
layout = json.loads((root/'image.json').read_text())
out.mkdir(parents=True)
results = {}
def run(label, input_root, frozen, destination, expected):
    result = subprocess.run([sys.executable, str(Path(__file__).with_name('catalog-traces.py')),
                             '--root', str(input_root), '--checkpoint', str(frozen),
                             '--output', str(destination)], capture_output=True, text=True, timeout=30)
    (out/(label+'.log')).write_text(result.stdout+result.stderr)
    assert (result.returncode == 0) == expected, label
    if not expected: assert not destination.exists(), label
    results[label] = {'passed': True, 'accepted': expected}
run('valid', root, checkpoint, out/'valid-catalog', True)
for label, field, value in [
    ('base', 'base', layout['base']+16),
    ('size', 'size', layout['size']-1),
    ('pointer-count', 'image_pointer_count', layout['image_pointer_count']-1),
    ('pointer-hash', 'image_pointer_sha256', '0'*64),
    ('segments', 'segments', [[s[0], s[1], s[2]^1] for s in layout['segments']]),
]:
    candidate = copy.deepcopy(layout); candidate[field] = value
    directory = out/(label+'-input'); directory.mkdir()
    (directory/'image.json').write_text(json.dumps(candidate))
    run(label, directory, checkpoint, out/(label+'-catalog'), False)
for label, name in [('header-hash', 'image-layout.h'), ('thunks-hash', 'thunks.cpp')]:
    directory = out/(label+'-checkpoint'); directory.mkdir()
    for leaf in ('build-state.json', 'image-layout.h', 'thunks.cpp'):
        data = (checkpoint/leaf).read_bytes()
        (directory/leaf).write_bytes(data+b'\n' if leaf == name else data)
    run(label, root, directory, out/(label+'-catalog'), False)
assert before == {str(path): digest(path) for path in inputs}, 'Original input changed'
report = {'checks': len(results), 'cases': results, 'original_inputs_unchanged': True,
          'game_executed': False, 'game_frames': False}
(out/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report))
