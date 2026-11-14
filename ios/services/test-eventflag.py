#!/usr/bin/env python3
"""Check shared native event flags and the guest-pointer bridge on both Mac CPUs."""
import argparse
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
p.add_argument('--arch',action='append',choices=('arm64','x86_64'))
p.add_argument('--sanitize',choices=('address','thread'))
a=p.parse_args();root=a.output.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
root.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[2]
reports={}
for arch in a.arch or ('arm64','x86_64'):
    out=root/arch;out.mkdir(exist_ok=True)
    commands=[['xcrun','clang','-arch',arch,'-std=gnu11','-O2','-c',str(repo/'src/runtime_eventflag.c'),'-o',str(out/'eventflag.o')],
              ['xcrun','clang','-arch',arch,'-std=gnu11','-O2','-c',str(repo/'ios/services/memory_shim.c'),'-o',str(out/'shim.o')],
              ['xcrun','clang++','-arch',arch,'-std=c++17','-O2','-I'+str(a.remill/'remill/include'),str(repo/'ios/services/eventflag-test.cpp'),str(repo/'ios/cpu/runtime.cpp'),str(out/'eventflag.o'),str(out/'shim.o'),'-o',str(out/'check')]]
    if a.sanitize:
        for command in commands:
            command[2:2]=['-fsanitize='+a.sanitize,'-fno-omit-frame-pointer','-g']
    with (out/'build.log').open('w') as log:
        for command in commands: subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    run=subprocess.run([out/'check'],text=True,capture_output=True,timeout=30)
    (out/'results.log').write_text(run.stdout+run.stderr)
    if run.returncode: raise RuntimeError(f'{arch} event flag tests failed: {out / "results.log"}')
    reports[arch]=json.loads(run.stdout.strip())
(root/'verification.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps(reports,indent=2))
