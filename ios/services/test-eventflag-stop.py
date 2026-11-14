#!/usr/bin/env python3
"""Test host-interruptible event waits with persistent native queue ownership."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('output','remill'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--experiment',type=Path,help='Explicit unpublished core/bridge candidate outside the source tree')
p.add_argument('--arch',choices=('arm64','x86_64'),default='arm64')
p.add_argument('--sanitize',choices=('address','thread'))
p.add_argument('--legacy',action='store_true',help='Run the existing full native/guest event contract suite')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a NEW mounted external output directory')
out.mkdir(parents=True);core=(a.experiment/'runtime_eventflag.c') if a.experiment else repo/'src/runtime_eventflag.c'
common=['-arch',a.arch,'-O2','-g','-fno-omit-frame-pointer']
if a.sanitize:common+=['-fsanitize='+a.sanitize]
objects=[]
with (out/'build.log').open('w') as log:
 for name,path in [('eventflag',core),('shim',repo/'ios/services/memory_shim.c')]:
  obj=out/(name+'.o');objects.append(obj);subprocess.run(['xcrun','clang',*common,'-std=gnu11','-I'+str(repo/'src'),'-c',path,'-o',obj],stdout=log,stderr=subprocess.STDOUT,check=True)
 command=['xcrun','clang++',*common,'-std=c++17','-I'+str(a.remill/'remill/include')]
 if a.experiment:command+=['-I'+str(a.experiment),'-DOSL_EVENTFLAG_TEST_HEADER="eventflag-guarded.h"']
 command+=[repo/'ios/services'/('eventflag-test.cpp' if a.legacy else 'eventflag-stop-test.cpp'),repo/'ios/cpu/runtime.cpp',*objects,'-o',out/'check']
 subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,check=True)
result=subprocess.run([out/'check'],capture_output=True,text=True,timeout=15);(out/'results.log').write_text(result.stdout+result.stderr)
if result.returncode:raise RuntimeError(str(out/'results.log'))
report=json.loads(result.stdout);report.update(architecture=a.arch,sanitizer=a.sanitize,legacy_contract_suite=a.legacy,experiment=str(a.experiment) if a.experiment else None)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
