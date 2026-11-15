#!/usr/bin/env python3
"""Build/run a device-only touch check against an already installed disposable probe."""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--team',required=True)
p.add_argument('--device',required=True,help='Explicit authorized iPad UDID; no phone/default rotation')
a=p.parse_args();root=a.root.resolve();source=Path(__file__).resolve().parent
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use mounted external storage')
if not re.fullmatch('[A-Z0-9]{10}',a.team) or not re.fullmatch('[A-Fa-f0-9-]+',a.device):p.error('Invalid team/device identifier')
if root.exists():p.error('--root must be new to preserve prior results')
root.mkdir(parents=True)
subprocess.run(['xcrun','devicectl','device','info','details','--device',a.device,
                '--json-output',str(root/'device-details.json'),'--quiet'],check=True)
device=json.loads((root/'device-details.json').read_text())['result']
if device['hardwareProperties']['deviceType']!='iPad':p.error('This harness is iPad-only')
for name in ['Host.swift','SaveMessageTouchTests.swift']:shutil.copy2(source/name,root/name)
(root/'project.yml').write_text((source/'project.yml').read_text().replace('TEAM_ID',a.team))
(root/'inputs.json').write_text(json.dumps({'device':a.device,'team':a.team,'scope':'already installed disposable startup probe, actual device touch injection; no phone access','sources':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in source.iterdir() if path.is_file()}},indent=2)+'\n')
with (root/'generate.log').open('w') as log:subprocess.run(['xcodegen','generate','--spec',str(root/'project.yml')],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
command=['xcodebuild','test','-project',str(root/'OdinTouchCheck.xcodeproj'),'-scheme','OdinTouchCheck','-destination','id='+a.device,'-destination-timeout','60','-derivedDataPath',str(root/'DerivedData'),'-resultBundlePath',str(root/'Result.xcresult'),'-allowProvisioningUpdates','-allowProvisioningDeviceRegistration','-parallel-testing-enabled','NO','-only-testing:OdinTouchTests/SaveMessageTouchTests/testYesThroughDeviceTouch']
with (root/'test.log').open('w') as log:run=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT)
(root/'exit.json').write_text(json.dumps({'exit_code':run.returncode,'result_bundle':str(root/'Result.xcresult'),'phone_tested':False},indent=2)+'\n')
run.check_returncode();print(json.dumps({'test_completed':True,'result_bundle':str(root/'Result.xcresult'),'scope':'device UI test; inspect report, screenshots and saved profile before acceptance'}))
