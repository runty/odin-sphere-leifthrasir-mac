#!/usr/bin/env python3
"""Check real guest-memory dialog marshaling and deliberate completion semantics."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use mounted external storage')
root.mkdir(parents=True,exist_ok=True);results=[]
for mode,sanitizer in [('native',None),('asan-ubsan','address,undefined')]:
 out=root/mode;out.mkdir(exist_ok=True)
 command=['xcrun','clang++','-arch','arm64','-std=c++17','-O1','-g','-I'+str(a.remill/'remill/include'),repo/'ios/services/save-dialog-test.cpp',repo/'ios/cpu/runtime.cpp','-o',out/'check']
 if sanitizer:command+=['-fsanitize='+sanitizer,'-fno-omit-frame-pointer']
 with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
 run=subprocess.run([out/'check'],capture_output=True,text=True,timeout=30)
 (out/'results.log').write_text(run.stdout+run.stderr);run.check_returncode()
 result=json.loads(run.stdout.splitlines()[-1]);assert result['failed']==0
 results.append(dict(mode=mode,**result))
for sdk in ['iphoneos','iphonesimulator']:
 path=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
 command=['xcrun','clang++','-target','arm64-apple-ios27.2'+('-simulator' if sdk=='iphonesimulator' else ''),'-isysroot',path,'-std=c++17','-O2','-I'+str(a.remill/'remill/include'),'-c',repo/'ios/services/save-dialog-test.cpp','-o',root/(sdk+'.o')]
 with (root/(sdk+'.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
report=dict(passed=True,results=results,sdks_compiled=['iphoneos','iphonesimulator'],scope='actual checked guest memory/state/results; rendering and device progress are separate',sources={str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [repo/'ios/services/save-dialog.h',repo/'ios/services/save-dialog-test.cpp',Path(__file__).resolve()]})
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
