#!/usr/bin/env python3
"""Verify Remill exception-bit mapping against host fenv and thread-local lifecycle."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True);p.add_argument('--remill',type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();repo=Path(__file__).resolve().parents[2];out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external output directory')
out.mkdir(parents=True,exist_ok=True);reports={}
for arch in (('arm64',) if a.sanitizer else ('arm64','x86_64')):
 target=out/arch;target.mkdir(exist_ok=True)
 common=['xcrun','clang++','-arch',arch,'-std=c++17','-O1' if a.sanitizer else '-O2','-g','-I'+str(a.remill/'remill/include')]
 instrumentation=['-fsanitize='+('address,undefined' if a.sanitizer=='address' else 'thread'),'-fno-omit-frame-pointer'] if a.sanitizer else []
 # Compile the runtime with its production optimization/default FP policy;
 # only the independent host-arithmetic fixture requests strict fenv access.
 commands=[[*common,*instrumentation,'-c',str(repo/'ios/cpu/runtime.cpp'),'-o',str(target/'runtime.o')],
           [*common,*instrumentation,'-frounding-math','-ffp-exception-behavior=strict','-c',str(repo/'ios/cpu/fpu-exceptions-test.cpp'),'-o',str(target/'fixture.o')],
           ['xcrun','clang++','-arch',arch,*instrumentation,str(target/'runtime.o'),str(target/'fixture.o'),'-o',str(target/'check')]]
 with (target/'build.log').open('w') as log:
  for command in commands:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
 result=subprocess.run([target/'check'],text=True,capture_output=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1'))
 (target/'results.log').write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(str(target/'results.log'))
 d=json.loads(result.stdout.splitlines()[-1]);assert d['failed']==0;reports[arch]=d
report={'passed':True,'scope':'Remill/native exception-mask mapping, selective clear/raise, real host FP flags, rounding preservation and thread isolation; not FP80 arithmetic or full-game execution','cases':reports,'sanitizer':a.sanitizer,'sources':{str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in (repo/'ios/cpu/runtime.cpp',repo/'ios/cpu/fpu-exceptions-test.cpp')}}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
