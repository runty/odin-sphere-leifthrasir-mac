#!/usr/bin/env python3
"""Check per-worker stack/TLS against the real private module templates."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('output','root','remill'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external directory')
layout=json.loads((a.root/'image.json').read_text());image=a.root/'image.bin'
if hashlib.sha256(image.read_bytes()).hexdigest()!=layout['image_sha256']:p.error('Private image mismatch')
if layout['main_tls']!=[0,0,0,1] or [(m[6],m[4],m[5]) for m in layout['modules']]!=[(2,1168,416),(3,64,0)]:p.error('Unexpected TLS profile')
out.mkdir(parents=True,exist_ok=True)
(out/'tls-fixture.h').write_text('#pragma once\nstruct TlsModule{unsigned long long id,start,filesz,memsz;};\nstatic const TlsModule tls_modules[]={'+','.join('{'+','.join(str(x) for x in (m[6],m[3],m[5],m[4]))+'}' for m in layout['modules'])+'};\n')
reports={}
for arch in (('arm64',) if a.sanitizer else ('arm64','x86_64')):
 directory=out/arch;directory.mkdir(exist_ok=True)
 command=['xcrun','clang++','-arch',arch,'-std=c++17','-O1' if a.sanitizer else '-O2','-g','-I'+str(out),'-I'+str(a.remill/'remill/include'),repo/'ios/services/thread-storage-test.cpp',repo/'ios/cpu/runtime.cpp','-o',directory/'check']
 if a.sanitizer:command+=['-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer']
 with (directory/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
 result=subprocess.run([directory/'check',image],capture_output=True,text=True,timeout=30)
 (directory/'results.log').write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(str(directory/'results.log'))
 reports[arch]=json.loads(result.stdout.splitlines()[-1])
reports.update(image_sha256=layout['image_sha256'],sanitizer=a.sanitizer)
(out/'verification.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps(reports))
