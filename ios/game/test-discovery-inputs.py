#!/usr/bin/env python3
"""Prove a real concurrent source edit cannot publish a mixed startup checkpoint."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True,help='Existing private prepared-image directory')
p.add_argument('--output',type=Path,required=True,help='New external fixture/worktree directory')
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if out.exists() or len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Use a new directory on a mounted external disk')
out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[2]
original_hash=hashlib.sha256((repo/'src/runtime_user.c').read_bytes()).hexdigest()
source=out/'source';run=out/'run';run.mkdir()
with (out/'checkout.log').open('w') as log:
    subprocess.run(['git','-C',repo,'worktree','add','--detach',source,'HEAD'],stdout=log,stderr=subprocess.STDOUT,check=True)
for name in ('image.bin','image.json','image-pointers.bin','trace-identity.json'):
    shutil.copy2(a.root/name,run/name)
with (out/'discovery.log').open('w') as log:
    process=subprocess.Popen(['python3',source/'ios/game/advance.py','--root',run,'--remill',a.remill,'--rounds','1'],stdout=log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+30
        # This stamp is written by the real compiler/cache after the script
        # snapshots source inputs; other portable services are still building.
        first=run/'objects/services/service-trophy_data.sha256'
        while not first.exists():
            if process.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError('Discovery did not reach service compilation')
            time.sleep(.002)
        edited=source/'src/runtime_user.c'
        with edited.open('a') as stream: stream.write('\n/* Isolated input-change rejection fixture. */\n')
        status=process.wait(timeout=30)
    finally:
        if process.poll() is None:
            process.terminate();process.wait(timeout=10)
text=(out/'discovery.log').read_text()
assert status!=0 and 'Startup sources changed during discovery' in text,text[-2000:]
assert not (run/'build-state.json').exists() and not (run/'latest.json').exists()
assert hashlib.sha256((repo/'src/runtime_user.c').read_bytes()).hexdigest()==original_hash
report={'concurrent_edit_rejected':True,'mixed_checkpoint_published':False,
        'original_runtime_user_unchanged':True,'fixture_source_retained':str(source),
        'game_executed':False,'exit_code':status}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
