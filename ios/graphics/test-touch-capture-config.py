#!/usr/bin/env python3
"""Check the gameplay driver's capture request with the real renderer parser."""
import pathlib
import re
import subprocess
import tempfile
import sys

root = pathlib.Path(__file__).resolve().parents[2]
filename = sys.argv[1] if len(sys.argv) > 1 else 'touch-gameplay-ui-test.swift'
if filename not in ('touch-gameplay-ui-test.swift', 'touch-scene-gameplay-ui-test.swift'):
    raise SystemExit('unsupported gameplay driver')
source = (root / 'ios/graphics' / filename).read_text()
values = {}
for key in ('AT', 'EVERY', 'COUNT'):
    found = re.findall(r'app\.launchEnvironment\["OSL_FRAME_SNAPSHOT_' + key + r'"\] = "([0-9]+)"', source)
    if len(found) != 1:
        raise SystemExit('expected exactly one literal snapshot ' + key)
    values[key] = found[0]
program = r'''#include "gpu/shim/osl_frame_snapshot.h"
#include <cassert>
int main(int argc, char **argv) {
    assert(argc == 4);
    auto request = OslFrameSnapshot::Parse("/tmp/frame.png", argv[1], "display", argv[2], argv[3]);
    assert(request && request->count > 1 && request->Captures(request->frame));
    assert(request->Captures(request->frame + request->every * (request->count - 1)));
    assert(!request->Captures(request->frame + request->every * request->count));
}
'''
with tempfile.TemporaryDirectory(prefix='odin-capture-config-') as temp:
    src = pathlib.Path(temp) / 'check.cpp'
    out = pathlib.Path(temp) / 'check'
    src.write_text(program)
    subprocess.run(['clang++', '-std=c++17', '-I', str(root), str(src), '-o', str(out)], check=True)
    subprocess.run([str(out), values['AT'], values['EVERY'], values['COUNT']], check=True)
print('Game touch capture request accepted by renderer parser:', values)
