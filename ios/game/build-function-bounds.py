#!/usr/bin/env python3
"""Validate original unwind metadata against the immutable prepared Odin image."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from unwind_reader import Elf

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo/'scripts'))
from ps4_module import parse_self

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root, out = a.root.resolve(), a.output.resolve()
for path in (root, out):
    if len(path.parts) < 3 or path.parts[1] != 'Volumes' or not Path('/Volumes', path.parts[2]).is_mount():
        p.error('Use mounted external build paths')
if out.exists(): p.error('--output must be new')
def digest(data): return hashlib.sha256(data).hexdigest()
layout_bytes = (root/'image.json').read_bytes()
layout = json.loads(layout_bytes)
image = (root/'image.bin').read_bytes()
if digest(image) != layout['image_sha256'] or len(image) != layout['size']:
    p.error('Prepared image hash/size mismatch')
if len(layout['modules']) != 2:
    p.error('Expected the supported Odin eboot/libc/Fios2 layout')
app0 = (root/'App0').resolve()
sources = [('eboot.bin', layout['base']),
           ('sce_module/libc.prx', layout['base']+layout['modules'][0][0]),
           ('sce_module/libSceFios2.prx', layout['base']+layout['modules'][1][0])]
out.mkdir(parents=True)
entries, modules = [], []
for name, base in sources:
    raw = (app0/name).read_bytes()
    elf_bytes, _, _, _, _ = parse_self(raw)
    label = Path(name).stem
    path = out/(label+'.elf'); path.write_bytes(elf_bytes)
    elf = Elf(path, base, label)
    decoded, report = elf.decode()
    # Relocation/TLS patches can change code/data bytes elsewhere. The exact
    # original frame/index spans themselves must match the prepared image.
    frame_start, frame_end = (int(report[name], 16) for name in ('eh_frame_start', 'eh_frame_end'))
    spans = [(frame_start, frame_end-frame_start),
             (elf.eh['vaddr'], elf.eh['filesz'])]
    module_offset = base-layout['base']
    for address, size in spans:
        start = module_offset+address
        if not 0 <= start <= len(image) or size > len(image)-start:
            raise ValueError('Unwind span exceeds prepared image')
        original = elf_bytes[elf.file(address, size):elf.file(address, size)+size]
        if image[start:start+size] != original:
            raise ValueError('Original unwind bytes differ from prepared image: '+name)
    for row in decoded:
        start, end = row['pc'], row['canonical_end']
        if not any(flags&1 and layout['base']+at <= start < end <= layout['base']+at+size
                   for at, size, flags in layout['segments']):
            raise ValueError('Function bounds exceed prepared executable mapping')
    entries.extend([[row['pc'], row['canonical_end']] for row in decoded])
    (out/(label+'-report.json')).write_text(json.dumps(report, indent=2)+'\n')
    modules.append({'path': name, 'self_sha256': digest(raw), 'elf_sha256': digest(elf_bytes),
                    'canonical': base, 'entries': len(decoded), 'unwind_bytes_match': True})
entries.sort()
if any(entries[i][1] > entries[i+1][0] for i in range(len(entries)-1)):
    raise ValueError('Overlapping function bounds across modules')
files = ['ios/game/unwind_reader.py', 'ios/game/build-function-bounds.py', 'scripts/ps4_module.py']
report = {'schema': 1, 'classification': 'validated unwind ranges; reachability and execution unverified',
          'image_sha256': layout['image_sha256'], 'layout_sha256': digest(layout_bytes),
          'sources': {name: digest((repo/name).read_bytes()) for name in files},
          'modules': modules, 'entries': entries, 'game_executed': False, 'game_frames': False}
(out/'bounds.json').write_text(json.dumps(report, separators=(',', ':'))+'\n')
print(json.dumps({k: v if k != 'entries' else len(v) for k, v in report.items()}))
