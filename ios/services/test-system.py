#!/usr/bin/env python3
"""Exercise shared console settings and checked guest marshalling on both Mac CPUs."""
import argparse
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Use a mounted external output directory')
repo=Path(__file__).resolve().parents[2];out.mkdir(parents=True,exist_ok=True);results={}
for arch in ('arm64','x86_64'):
    root=out/arch;root.mkdir(exist_ok=True)
    with (root/'build.log').open('w') as log:
        objects=[]
        for name,path in [('system',repo/'src/runtime_system.c'),('shim',repo/'ios/services/memory_shim.c')]:
            obj=root/(name+'.o');objects.append(obj)
            subprocess.run(['xcrun','clang','-arch',arch,'-std=gnu11','-O2','-c',path,'-o',obj],stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(['xcrun','clang++','-arch',arch,'-std=c++17','-O2','-I'+str(a.remill/'remill/include'),repo/'ios/services/system-test.cpp',repo/'ios/cpu/runtime.cpp',*objects,'-o',root/'check'],stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([root/'check'],text=True,capture_output=True,timeout=15)
    (root/'results.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(str(root/'results.log'))
    results[arch]=json.loads(result.stdout.splitlines()[-1])
(out/'verification.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
