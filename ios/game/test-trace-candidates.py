#!/usr/bin/env python3
"""Reject malformed static inputs and keep speculative and executed targets distinct."""
import copy
import hashlib
import struct
from trace_candidates import candidates, nearest, thunk_targets

checks = 0
def digest(b): return hashlib.sha256(b).hexdigest()
base = 0x10000000
image = bytearray(256)
for offset, pc in [(0, base+64), (8, base+80), (16, base+65), (24, base+160)]:
    struct.pack_into('<Q', image, offset, pc)
pointers = struct.pack('<4Q', 0, 8, 16, 24)
layout = dict(size=len(image), base=base, image_sha256=digest(image),
              image_pointer_count=4, image_pointer_sha256=digest(pointers),
              segments=[[0, 128, 5], [128, 128, 6]])
def check(value):
    global checks
    assert value; checks += 1
def reject(function, *args):
    global checks
    try: function(*args)
    except (ValueError, TypeError): checks += 1; return
    raise AssertionError('Malformed candidate input accepted')

declarations = {base+65, base+80, base+200, 0x60000000}
c = candidates(layout, image, pointers, declarations)
check(set(c) == {base+64, base+65, base+80})
check(c[base+64] == {'direct_reference': False, 'relocation_fields': 1, 'first_relocation_field': 0})
check(c[base+65] == {'direct_reference': True, 'relocation_fields': 0})
check(c[base+80]['direct_reference'] and c[base+80]['relocation_fields'] == 1)
check(nearest(c, [base+66], set(), 2) == [base+65, base+64])
check(nearest(c, [base+65], {base+64}, 12) == [base+80])
check(nearest(c, [], set(), 12) == [])
check(nearest(c, [base+64], set(), 0) == [])
for limit in (-1, 4097, True, 1.5): reject(nearest, c, [base], set(), limit)
bounds_hash='1'*64
entries=[[base+32,base+48],[base+64,base+80],[base+81,base+89]]
catalog=candidates(layout,image,pointers,declarations,function_entries=entries,bounds_sha256=bounds_hash)
check(set(catalog)==set(c)|{base+32,base+81})
check(catalog[base+64]['relocation_fields']==1 and catalog[base+64]['verified_unwind_entry'])
check(catalog[base+81]['unwind_bounds_sha256']==bounds_hash and not catalog[base+81]['direct_reference'])
check(nearest(catalog,[base+65],{base+64},1024)==[base+80,base+81,base+32])
check(c==candidates(layout,image,pointers,declarations))
for rows in ([[base+32,base+32]],[[base+48,base+32]],[[base+32,base+129]],
             [[base+32,base+48],[base+40,base+64]],[[base+32,base+48],[base+32,base+48]],
             [[base+160,base+180]],[[True,base+48]],[[base+32]],[[base+32,'bad']]):
 reject(lambda: candidates(layout,image,pointers,declarations,function_entries=rows,bounds_sha256=bounds_hash))
for manifest_hash in (None,'','f'*63,'F'*64,1):
 reject(lambda: candidates(layout,image,pointers,declarations,function_entries=entries,bounds_sha256=manifest_hash))
reject(lambda: candidates(layout,image,pointers,declarations,bounds_sha256=bounds_hash))
for field, value in [('size', 255), ('size', True), ('base', -1), ('base', (1<<64)-128),
                     ('image_sha256', 'bad'), ('image_pointer_count', 3),
                     ('image_pointer_count', True), ('image_pointer_sha256', 'bad'),
                     ('segments', [[0, 257, 5]]), ('segments', [[-1, 128, 5]]),
                     ('segments', [[0, 128, 8]]), ('segments', [[0, 128, True]]),
                     ('segments', [[0, 128, 6]]), ('segments', [[0, 128]])]:
    bad = copy.deepcopy(layout); bad[field] = value
    reject(candidates, bad, image, pointers, declarations)
for offsets in [(0, 8, 8, 24), (8, 0, 16, 24), (0, 8, 16, 249)]:
    b = struct.pack('<4Q', *offsets); bad = dict(layout, image_pointer_sha256=digest(b))
    reject(candidates, bad, image, b, declarations)
for pc in [True, -1, 1<<64]: reject(candidates, layout, image, pointers, [pc])
header = '#include "runtime.h"\n'
line = 'extern "C" Memory *sub_10000041(State &s,uint64_t pc,Memory *m) { return dispatch(s,pc,m); }\n'
check(thunk_targets(header) == set())
check(thunk_targets(header+line) == {base+65})
for text in ['', line, header+line+line, header+'arbitrary C++\n', header+line.replace('10000041','10000000000000000')]:
    reject(thunk_targets, text)
print({'candidate_checks': checks, 'game_executed': False, 'game_frames': False})
