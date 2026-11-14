#!/usr/bin/env python3
"""Check serialized WriteData layout and unaligned packet access on both Mac CPUs."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True);p.add_argument('--arch',choices=('arm64','x86_64'),required=True)
a=p.parse_args();repo=Path(__file__).resolve().parents[2];out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new mounted external output directory')
out.mkdir(parents=True);header=repo/'gpu/shadps4/video_core/amdgpu/pm4_cmds.h';source=repo/'ios/graphics/pm4-write-data-test.cpp'
sources={str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in (header,source)}
command=['xcrun','clang++','-arch',a.arch,'-std=c++23','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
for include in ('gpu/shim','gpu/shadps4'):command+=['-I'+str(repo/include)]
command+=['-I/opt/homebrew/include',source,'-o',out/'check']
with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
r=subprocess.run([out/'check'],capture_output=True,text=True,timeout=15);(out/'results.log').write_text(r.stdout+r.stderr)
if r.returncode:raise RuntimeError(str(out/'results.log'))
report=json.loads(r.stdout);assert report['failed']==0
for name,wanted in sources.items():
 if hashlib.sha256((repo/name).read_bytes()).hexdigest()!=wanted:raise RuntimeError('Source changed during layout test')
report.update(architecture=a.arch,sanitizer='undefined',sources=sources)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
