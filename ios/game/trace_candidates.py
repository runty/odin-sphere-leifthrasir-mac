"""Static translation candidates, never proof of reachability or execution.

Checked relocation fields, explicit lifted-call declarations and optional
verified unwind entries supply addresses. Arbitrary executable bytes are not
searched for guessed functions. Unwind metadata establishes bounds, not execution.
"""
import hashlib
import re
import struct

_THUNK = re.compile(r'extern "C" Memory \*sub_([0-9a-f]+)\(State &s,uint64_t pc,Memory \*m\) \{ return dispatch\(s,pc,m\); \}')


def thunk_targets(text):
    lines = text.splitlines()
    if not lines or lines[0] != '#include "runtime.h"':
        raise ValueError('Unexpected unresolved-thunk header')
    targets = set()
    for line in lines[1:]:
        match = _THUNK.fullmatch(line)
        if not match:
            raise ValueError('Unexpected unresolved-thunk declaration')
        pc = int(match[1], 16)
        if pc >= 1 << 64 or pc in targets:
            raise ValueError('Invalid or duplicate unresolved-thunk PC')
        targets.add(pc)
    return targets


def candidates(layout, image, pointers, declarations, *, function_entries=None, bounds_sha256=None):
    """Return deterministic candidate provenance from immutable prepared inputs."""
    size, base = layout['size'], layout['base']
    if (isinstance(size, bool) or not isinstance(size, int) or size < 8
            or len(image) != size or not isinstance(base, int) or isinstance(base, bool)
            or not 0 <= base < 1 << 64 or size > (1 << 64) - base):
        raise ValueError('Invalid prepared image extent')
    if hashlib.sha256(image).hexdigest() != layout['image_sha256']:
        raise ValueError('Prepared image hash mismatch')
    count = layout['image_pointer_count']
    if (isinstance(count, bool) or not isinstance(count, int) or not 0 <= count <= size // 8
            or len(pointers) != count * 8
            or hashlib.sha256(pointers).hexdigest() != layout['image_pointer_sha256']):
        raise ValueError('Prepared pointer ledger mismatch')
    executable = []
    for segment in layout['segments']:
        if (not isinstance(segment, (tuple, list)) or len(segment) != 3
                or any(isinstance(n, bool) or not isinstance(n, int) for n in segment)):
            raise ValueError('Malformed prepared segment')
        start, length, flags = segment
        if start < 0 or length <= 0 or start > size or length > size - start or not 0 <= flags <= 7:
            raise ValueError('Prepared segment exceeds image')
        if flags & 1:
            executable.append((base + start, base + start + length))
    if not executable:
        raise ValueError('No executable prepared image range')

    def is_executable(pc):
        return any(start <= pc < end for start, end in executable)

    result = {}
    for pc in sorted(declarations):
        if isinstance(pc, bool) or not isinstance(pc, int) or not 0 <= pc < 1 << 64:
            raise ValueError('Invalid direct-call declaration PC')
        if is_executable(pc):
            result[pc] = {'direct_reference': True, 'relocation_fields': 0}
    previous = -1
    for (offset,) in struct.iter_unpack('<Q', pointers):
        if offset <= previous or offset > size - 8:
            raise ValueError('Unsorted, duplicate or out-of-bounds pointer field')
        previous = offset
        (pc,) = struct.unpack_from('<Q', image, offset)
        # Vtables/callbacks can point into code. Alignment filters speculative
        # values only; actual executed unaligned demands are always retained.
        if pc % 16 or not is_executable(pc):
            continue
        record = result.setdefault(pc, {'direct_reference': False, 'relocation_fields': 0})
        record['relocation_fields'] += 1
        record.setdefault('first_relocation_field', offset)
    if function_entries is not None:
        if not isinstance(bounds_sha256,str) or not re.fullmatch(r'[0-9a-f]{64}',bounds_sha256):
            raise ValueError('Verified function entries require their bounds manifest hash')
        previous=-1
        for row in function_entries:
            if (not isinstance(row,(tuple,list)) or len(row)!=2
                    or any(isinstance(n,bool) or not isinstance(n,int) for n in row)):
                raise ValueError('Malformed verified function entry')
            start,end=row
            if (start<previous or not 0<=start<end<1<<64
                    or not any(low<=start<end<=high for low,high in executable)):
                raise ValueError('Unsorted, overlapping or non-executable function range')
            previous=end
            record=result.setdefault(start,{'direct_reference':False,'relocation_fields':0})
            record['verified_unwind_entry']=True
            record['unwind_bounds_sha256']=bounds_sha256
    elif bounds_sha256 is not None:
        raise ValueError('Bounds hash requires verified function entries')
    return result


def nearest(catalog, demanded, known, limit):
    """Select extra static candidates; mandatory executed demands stay separate."""
    if isinstance(limit, bool) or not isinstance(limit, int) or not 0 <= limit <= 4096:
        raise ValueError('Static candidate batch must be between 0 and 4096')
    if not demanded or not limit:
        return []
    demands = set(demanded)
    available = set(catalog) - set(known) - demands
    return sorted(available, key=lambda pc: (min(abs(pc - d) for d in demands), pc))[:limit]
