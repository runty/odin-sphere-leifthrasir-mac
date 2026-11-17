#!/usr/bin/env python3
"""Exercise actual MobilePad overlay merge with physical enumeration suppressed."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'game'))
from service_build import compile_services,link_options
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);a=p.parse_args()
root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external build directory')
root.mkdir(parents=True,exist_ok=False);sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();modes=[]
for name,sanitizer in [('native',None),('asan-ubsan','address,undefined')]:
 out=root/name;out.mkdir();objects=compile_services(repo,out/'objects','arm64-apple-macos15',sdk,sanitizer)
 cmd=['xcrun','clang++','-arch','arm64','-std=c++17','-O1','-fobjc-arc',str(repo/'ios/services/touch-link-test.mm'),*map(str,objects),*link_options(),'-o',str(out/'check')]
 if sanitizer:cmd+=['-fsanitize='+sanitizer,'-fno-omit-frame-pointer']
 with (out/'build.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 run=subprocess.run([str(out/'check')],capture_output=True,text=True,timeout=30);(out/'results.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'touch-only skips controller enumeration' in run.stdout;modes.append(name)
for sdk_name in ('iphoneos','iphonesimulator'):
 path=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();target='arm64-apple-ios18'+('-simulator' if sdk_name=='iphonesimulator' else '')
 with (root/(sdk_name+'.log')).open('w') as f:subprocess.run(['xcrun','--sdk',sdk_name,'clang++','-target',target,'-isysroot',path,'-std=c++17','-fobjc-arc','-O2','-c',str(repo/'ios/services/pad.mm'),'-o',str(root/(sdk_name+'.o'))],stdout=f,stderr=subprocess.STDOUT,check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();names=['ios/services/pad.h','ios/services/pad.mm','ios/services/scripted-pad.h','ios/services/touch-link-test.mm','ios/services/test-touch-only.py'];report={'passed':True,'scope':'actual MobilePad with strong overlay; swizzled enumeration checks normal and touch-only paths, not physical-device gameplay','modes':modes,'sdks_compiled':['iphoneos','iphonesimulator'],'sources':{n:sha(repo/n) for n in names},'device_contacted':False,'phone_contacted':False};(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
