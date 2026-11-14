#!/usr/bin/env python3
"""Run AOT routines on owned host workers and test lifecycle/stop/cleanup."""
import argparse,json,subprocess,hashlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('output','fixtures','remill'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external output directory')
manifest=json.loads((a.fixtures/'manifest.json').read_text())
core=hashlib.sha256((repo/'ios/cpu/runtime.h').read_bytes()+(repo/'ios/cpu/runtime.cpp').read_bytes()).hexdigest()
if manifest['runtime_sha256']!=core:p.error('Rebuild CPU fixtures with the current checked runtime first')
out.mkdir(parents=True,exist_ok=True)
command=['xcrun','clang++','-arch','arm64','-std=c++17','-O1' if a.sanitizer else '-O2','-g','-I'+str(a.fixtures),'-I'+str(repo/'ios/cpu'),'-I'+str(a.remill/'remill/include'),repo/'ios/services/worker-test.cpp',repo/'ios/cpu/runtime.cpp',a.fixtures/'mac/libodin-cpu.a','-o',out/'check']
if a.sanitizer:command+=['-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer']
with (out/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
result=subprocess.run([out/'check'],capture_output=True,text=True,timeout=30)
(out/'results.log').write_text(result.stdout+result.stderr)
if result.returncode:raise RuntimeError(str(out/'results.log'))
report=json.loads(result.stdout.splitlines()[-1]);report.update(runtime_sha256=core,fixture_corpus=manifest['corpus_sha256'],sanitizer=a.sanitizer)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
