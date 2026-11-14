#!/usr/bin/env python3
"""Verify worker-shared mappings, host/CPU synchronization and interruption."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',required=True,type=Path);p.add_argument('--remill',required=True,type=Path)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();root=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external output directory')
root.mkdir(parents=True,exist_ok=True);reports={}
for arch in (('arm64',) if a.sanitizer else ('arm64','x86_64')):
 out=root/arch;out.mkdir(exist_ok=True)
 cmd=['xcrun','clang++','-arch',arch,'-std=c++17','-O1' if a.sanitizer else '-O2','-g','-I'+str(a.remill/'remill/include'),repo/'ios/cpu/shared-memory-test.cpp',repo/'ios/cpu/runtime.cpp','-o',out/'check']
 if a.sanitizer:cmd+=['-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer']
 with (out/'build.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 result=subprocess.run([out/'check'],capture_output=True,text=True,timeout=30)
 (out/'results.log').write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(str(out/'results.log'))
 reports[arch]=json.loads(result.stdout.splitlines()[-1])
(root/'verification.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps(reports))
