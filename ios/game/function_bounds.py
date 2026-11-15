"""Limit a requested lift to a verified containing function, never guess an entry."""
from bisect import bisect_right
import hashlib
import json


class FunctionBounds:
    def __init__(self, path, root, layout, repo):
        data = path.read_bytes()
        value = json.loads(data)
        digest = lambda data: hashlib.sha256(data).hexdigest()
        if (value.get('schema') != 1 or value.get('image_sha256') != layout['image_sha256']
                or value.get('layout_sha256') != digest((root/'image.json').read_bytes())):
            raise ValueError('Function bounds image/layout mismatch')
        expected = {'ios/game/unwind_reader.py', 'ios/game/build-function-bounds.py', 'scripts/ps4_module.py'}
        if set(value['sources']) != expected:
            raise ValueError('Unexpected function-bounds source set')
        for name, wanted in value['sources'].items():
            if digest((repo/name).read_bytes()) != wanted:
                raise ValueError('Function-bounds reader/builder changed')
        required = ['eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx']
        if [m['path'] for m in value['modules']] != required:
            raise ValueError('Unexpected function-bounds module inventory')
        for module in value['modules']:
            if not module['unwind_bytes_match'] or digest((root/'App0'/module['path']).read_bytes()) != module['self_sha256']:
                raise ValueError('Function-bounds module changed')
        self._inputs = {path: digest(data)}
        self._inputs.update({root/'App0'/m['path']: m['self_sha256'] for m in value['modules']})
        self.entries = value['entries']
        if not isinstance(self.entries, list) or not 0 < len(self.entries) < 1000000:
            raise ValueError('Function-bounds entry count outside cap')
        previous = -1
        for row in self.entries:
            if (not isinstance(row, list) or len(row) != 2
                    or any(isinstance(n, bool) or not isinstance(n, int) for n in row)):
                raise ValueError('Malformed function range')
            start, end = row
            if (start < previous or not 0 <= start < end < 1<<64
                    or not any(flags&1 and layout['base']+at <= start < end <= layout['base']+at+size
                               for at, size, flags in layout['segments'])):
                raise ValueError('Unsorted, overlapping or non-executable function range')
            previous = end
        if sum(m['entries'] for m in value['modules']) != len(self.entries):
            raise ValueError('Function-bounds module/entry count mismatch')
        self.starts = [row[0] for row in self.entries]
        self.manifest = {'path': str(path), 'sha256': digest(data), 'entries': len(self.entries),
                         'sources': value['sources'], 'modules': value['modules'],
                         'classification': 'lift bounds only; no reachability/execution claim'}

    def verify_current(self):
        if any(hashlib.sha256(path.read_bytes()).hexdigest() != wanted for path, wanted in self._inputs.items()):
            raise ValueError('Function-bounds manifest/module input changed')

    def length(self, pc, maximum):
        index = bisect_right(self.starts, pc)-1
        if index >= 0 and pc < self.entries[index][1]:
            return min(maximum, self.entries[index][1]-pc)
        # Unindexed leaves, assembly and executed jump-table cases retain the
        # ordinary segment/window bound and exact-PC missing-block behavior.
        return maximum
