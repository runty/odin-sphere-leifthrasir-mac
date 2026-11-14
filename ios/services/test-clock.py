#!/usr/bin/env python3
"""Check console clock units, real host brackets and guest output bounds."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True);p.add_argument('--remill',type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external output directory')
out.mkdir(parents=True,exist_ok=True);reports={}
for arch in (('arm64',) if a.sanitizer else ('arm64','x86_64')):
 directory=out/arch;directory.mkdir(exist_ok=True);flags=['-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer'] if a.sanitizer else []
 with (directory/'build.log').open('w') as log:
  subprocess.run(['xcrun','clang','-arch',arch,'-std=gnu11','-O1',*flags,'-c',repo/'src/runtime_errno.c','-o',directory/'errno.o'],stdout=log,stderr=subprocess.STDOUT,check=True)
  subprocess.run(['xcrun','clang++','-arch',arch,'-std=c++17','-O1','-g',*flags,'-I'+str(a.remill/'remill/include'),repo/'ios/services/clock-test.cpp',repo/'ios/cpu/runtime.cpp',directory/'errno.o','-o',directory/'check'],stdout=log,stderr=subprocess.STDOUT,check=True)
 result=subprocess.run([directory/'check'],capture_output=True,text=True,timeout=15)
 (directory/'results.log').write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(str(directory/'results.log'))
 reports[arch]=json.loads(result.stdout.splitlines()[-1])
(out/'verification.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps(reports))
