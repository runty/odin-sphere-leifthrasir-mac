#!/usr/bin/env python3
"""Check the local RTC layout, timezone, guest bounds and state preservation."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if out.exists() or len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Use a new directory on mounted external storage')
out.mkdir();results=[]
for arch,mode,flags in [('arm64','native',[]),('x86_64','native',[]),('arm64','asan-ubsan',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    label=arch+'-'+mode;binary=out/label
    command=['xcrun','clang++','-arch',arch,'-std=c++17','-O1','-g',*flags,
             '-I'+str(a.remill/'remill/include'),str(repo/'ios/services/rtc-clock-test.cpp'),
             str(repo/'ios/cpu/runtime.cpp'),'-o',str(binary)]
    with (out/(label+'-build.log')).open('x') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
    (out/(label+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(str(out/(label+'.log')))
    results.append({'arch':arch,'mode':mode,**json.loads(result.stdout.splitlines()[-1])})
sources=[repo/'ios/services/guest_clock.h',repo/'ios/services/rtc-clock-test.cpp',Path(__file__),repo/'ios/cpu/runtime.cpp']
report={'passed':True,'scope':'real host local time, 16-byte layout, three timezone settings, misaligned/end-boundary buffers, protected/truncated/overflow/null pointers, register and neighbor preservation; no game execution',
        'results':results,'sources':{str(x.relative_to(repo)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},'phone_contacted':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
