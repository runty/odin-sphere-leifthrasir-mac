#!/usr/bin/env python3
"""Inventory speculative AOT targets from a frozen checkpoint; no guest execution."""
import argparse
import hashlib
import json
from pathlib import Path
from trace_candidates import candidates, thunk_targets

p = argparse.ArgumentParser(description=__doc__)
for name in ('root', 'checkpoint', 'output'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
root, checkpoint, output = (x.resolve() for x in (a.root, a.checkpoint, a.output))
for path in (root, checkpoint, output):
    if len(path.parts) < 3 or path.parts[1] != 'Volumes' or not Path('/Volumes', path.parts[2]).is_mount():
        p.error('Use mounted external input/output directories')
if output.exists():
    p.error('--output must be new')
layout = json.loads((root / 'image.json').read_text())
state_bytes = (checkpoint / 'build-state.json').read_bytes()
state = json.loads(state_bytes)
thunks = (checkpoint / 'thunks.cpp').read_bytes()
header = (checkpoint / 'image-layout.h').read_bytes()
if (state['image_sha256'] != layout['image_sha256']
        or hashlib.sha256(thunks).hexdigest() != state['headers']['thunks.cpp']
        or hashlib.sha256(header).hexdigest() != state['headers']['image-layout.h']):
    p.error('Frozen checkpoint input mismatch')
text = header.decode()
segments = 'static const Segment segments[]={' + ','.join(
    '{' + ','.join(str(n) for n in row) + '}' for row in layout['segments']) + '};'
required = [f"image_base={layout['base']}, image_size={layout['size']},",
            f"image_pointer_count={layout['image_pointer_count']};",
            f'image_sha256_hex="{layout["image_sha256"]}"',
            f'pointer_sha256_hex="{layout["image_pointer_sha256"]}"', segments]
if state['trace_inputs']['base'] != layout['base'] or any(value not in text for value in required):
    p.error('Prepared layout differs from the frozen generated header')
catalog = candidates(layout, (root / 'image.bin').read_bytes(),
                     (root / 'image-pointers.bin').read_bytes(), thunk_targets(thunks.decode()))
report = {'classification': 'static candidates; reachability and execution unverified',
          'image_sha256': layout['image_sha256'], 'pointer_sha256': layout['image_pointer_sha256'],
          'checkpoint_sha256': hashlib.sha256(state_bytes).hexdigest(),
          'checkpoint_corpus_sha256': state['corpus_sha256'], 'thunks_sha256': hashlib.sha256(thunks).hexdigest(),
          'candidate_count': len(catalog), 'game_executed': False, 'game_frames': False,
          'candidates': {hex(pc): value for pc, value in sorted(catalog.items())}}
output.mkdir(parents=True)
(output / 'catalog.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k != 'candidates'}))
