#!/usr/bin/env python3
"""Verify immutable scripts and actual MobilePad mapping/primary precedence."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'game'))
from service_build import compile_services,link_options
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use external build storage')
root.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();reports=[]
expected=b'# comment\n2 cross+ls_up|l2\n0 0\n4 0\n4 SQUARE\n6 0x2000\n8 0 # release\n'
for name,sanitizer in [('native',None),('asan-ubsan','address,undefined')]:
 out=root/name;out.mkdir(exist_ok=True);objects=compile_services(repo,out/'objects','arm64-apple-macos15',sdk,sanitizer)
 command=['xcrun','clang++','-arch','arm64','-std=c++17','-O1','-fobjc-arc',str(repo/'ios/services/scripted-pad-test.mm'),*map(str,objects),*link_options(),'-o',str(out/'check')]
 if sanitizer:command+=['-fsanitize='+sanitizer,'-fno-omit-frame-pointer']
 with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
 run=subprocess.run([str(out/'check'),str(out/'fixtures')],capture_output=True,text=True,timeout=30)
 (out/'results.log').write_text(run.stdout+run.stderr);run.check_returncode()
 result=json.loads(run.stdout.splitlines()[-1]);assert result['failed']==0 and result['script_sha256']==hashlib.sha256(expected).hexdigest();reports.append(dict(mode=name,**result))
for target_sdk in ['iphoneos','iphonesimulator']:
 sdk_path=subprocess.check_output(['xcrun','--sdk',target_sdk,'--show-sdk-path'],text=True).strip()
 target='arm64-apple-ios18'+('-simulator' if target_sdk=='iphonesimulator' else '')
 with (root/(target_sdk+'-compile.log')).open('w') as log:
  subprocess.run(['xcrun','clang++','-target',target,'-isysroot',sdk_path,'-std=c++17','-fobjc-arc','-O2','-c',str(repo/'ios/services/scripted-pad-test.mm'),'-o',str(root/(target_sdk+'.o'))],stdout=log,stderr=subprocess.STDOUT,check=True)
report=dict(passed=True,results=reports,sdks_compiled=['iphoneos','iphonesimulator'],scope='parser/file snapshot, real pad ABI conversion, primary precedence and releases; real guest menu progress is separate')
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
