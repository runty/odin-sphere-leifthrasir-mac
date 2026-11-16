#!/usr/bin/env python3
"""Build the isolated touch UI fixture; never launches or contacts devices."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True,help='New directory on a mounted external build volume')
parser.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),required=True)
parser.add_argument('--team',help='Development team, required only for iPhoneOS signing')
group=parser.add_mutually_exclusive_group()
group.add_argument('--continuous-game',action='store_true',help='Build the sandbox background/foreground driver instead of fixture gestures')
group.add_argument('--gameplay-test',action='store_true',help='Build the sandbox touch-gameplay driver; never launches it')
args=parser.parse_args()
root=args.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():parser.error('Use a mounted external build volume')
if root.exists():parser.error('Output directory must not already exist')
if args.sdk=='iphoneos' and not args.team:parser.error('--team is required for device signing')
files={'ios/graphics/touch-ui-test-fixture.mm':'Source/touch-ui-test-fixture.mm',
       'ios/graphics/touch-overlay.mm':'Source/touch-overlay.mm',
       'ios/services/touch-controls.h':'Source/ios/services/touch-controls.h',
       'ios/services/pad.h':'Source/ios/services/pad.h',
       'src/runtime_pad_types.h':'Source/src/runtime_pad_types.h',
       'ios/graphics/touch-ui-test.swift':'UITests/TouchUITests.swift',
       'ios/graphics/touch-ui-test-project.yml':'project.yml'}
if args.continuous_game:
 del files['ios/graphics/touch-ui-test.swift']
 files['ios/graphics/continuous-game-ui-test.swift']='UITests/ContinuousGameUITests.swift'
if args.gameplay_test:
 del files['ios/graphics/touch-ui-test.swift']
 files['ios/graphics/touch-gameplay-ui-test.swift']='UITests/TouchGameplayUITests.swift'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
root.mkdir(parents=True)
for src,dst in files.items():
 out=root/dst;out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(repo/src,out)
with (root/'generate.log').open('w') as log:subprocess.run(['xcodegen','generate','--spec',str(root/'project.yml')],stdout=log,stderr=subprocess.STDOUT,check=True)
destination='generic/platform=iOS' if args.sdk=='iphoneos' else 'generic/platform=iOS Simulator'
command=['xcodebuild','-project',str(root/'OdinTouchUITest.xcodeproj'),'-scheme','OdinTouchUITest','-configuration','Release','-destination',destination,'-sdk',args.sdk,'-derivedDataPath',str(root/'DerivedData')]
if args.sdk=='iphoneos':command+=['-allowProvisioningUpdates','-allowProvisioningDeviceRegistration','DEVELOPMENT_TEAM='+args.team]
else:command+=['CODE_SIGNING_ALLOWED=NO']
command+=['build-for-testing']
with (root/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
tests=list((root/'DerivedData/Build/Products').glob('*.xctestrun'));assert len(tests)==1
assert all(sha(repo/src)==sha(root/dst) for src,dst in files.items())
result={'built':True,'scope':'standalone UI fixture build only; no device contact or gameplay','sdk':args.sdk,'xctestrun':str(tests[0]),'sources':{src:sha(repo/src) for src in files},'build_log_sha256':sha(root/'build.log'),'phone_contacted':False,'device_contacted':False}
(root/'build-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
