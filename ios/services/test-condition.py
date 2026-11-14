#!/usr/bin/env python3
"""Check condition ownership, real waits and checked ABI on both native Mac CPUs."""
import argparse
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True);p.add_argument('--remill',type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external directory')
out.mkdir(parents=True,exist_ok=True);results={}
for arch in (('arm64',) if a.sanitizer else ('arm64','x86_64')):
    directory=out/arch;directory.mkdir(exist_ok=True)
    command=['xcrun','clang++','-arch',arch,'-std=c++17','-O1' if a.sanitizer else '-O2','-g','-I'+str(a.remill/'remill/include'),repo/'ios/services/condition-test.cpp',repo/'ios/cpu/runtime.cpp','-o',directory/'check']
    if a.sanitizer:command+=['-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer']
    with (directory/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([directory/'check'],capture_output=True,text=True,timeout=30)
    (directory/'results.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(str(directory/'results.log'))
    results[arch]=json.loads(result.stdout.splitlines()[-1])
(out/'verification.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results))
