#!/usr/bin/env python3
"""Sanitize actual concurrent HLE/exception fixtures against a frozen AOT corpus."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from service_build import sources,link_options

p=argparse.ArgumentParser(description=__doc__)
for name in ('root','output','remill'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'),required=True)
a=p.parse_args();root=a.root.resolve();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
for path in (root,out):
 if len(path.parts)<3 or path.parts[1]!='Volumes' or not Path('/Volumes',path.parts[2]).is_mount():p.error('Use mounted external build/output directories')
if out.exists():p.error('--output must be new')
state_file=root/'build-state.json';snapshot=state_file.read_bytes();state=json.loads(snapshot)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def current():
 if state_file.read_bytes()!=snapshot:raise RuntimeError('Discovery changed the checkpoint; stop it before qualification')
 for name,wanted in state['sources'].items():
  if digest(repo/name)!=wanted:raise RuntimeError('Source changed: '+name)
 for name,wanted in state['headers'].items():
  if digest(root/name)!=wanted:raise RuntimeError('Generated header changed: '+name)
 for name,wanted in state['ir'].items():
  if digest(root/'traces'/name)!=wanted:raise RuntimeError('IR changed: '+name)
current();out.mkdir(parents=True);(out/'checkpoint.json').write_bytes(snapshot)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
objects=[];inputs={}
for source in sources(repo):
 current();obj=out/('service-'+source.stem+'.o');is_c=source.suffix=='.c'
 command=['xcrun','clang' if is_c else 'clang++','-target','arm64-apple-macos15','-isysroot',sdk,
          '-std=gnu11' if is_c else '-std=c++17','-O1','-g','-fno-omit-frame-pointer','-fsanitize='+a.sanitizer,
          '-DOSL_AOT_CPU=1','-I'+str(repo/'src'),'-I'+str(repo/'launcher'),'-c',source,'-o',obj]
 if source.suffix=='.mm':command.append('-fobjc-arc')
 with obj.with_suffix('.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
 objects.append(obj);inputs[str(source.relative_to(repo))]=digest(source)
current()
# Link the exact frozen production trace/thunk objects. They are uninstrumented
# guest IR; all C/C++ HLE providers and checked CPU transfers are instrumented.
traces=[root/'objects'/Path(name).with_suffix('.o') for name in state['ir']]
traces.append(root/'mac/thunks.o')
trace_inputs={str(path.relative_to(root)):digest(path) for path in traces}
command=['xcrun','clang++','-target','arm64-apple-macos15','-isysroot',sdk,'-std=c++17','-O1','-g',
         '-fno-omit-frame-pointer','-fsanitize='+a.sanitizer,'-DOSL_GAME_CLI=1',
         '-I'+str(a.remill/'remill/include'),'-I'+str(repo/'ios/cpu'),'-I'+str(root),'-I'+str(repo/'src'),'-I'+str(repo/'launcher'),
         repo/'ios/game/runner.cpp',repo/'ios/cpu/runtime.cpp',*objects,*traces,*link_options(),'-o',out/'check']
with (out/'link.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
current()
for name,wanted in trace_inputs.items():
 if digest(root/name)!=wanted:raise RuntimeError('Trace object changed during link: '+name)
reports={}
for mode,tag in [('--service-check','service_checks'),('--worker-cleanup-check','worker_cleanup_checks')]:
 current();label=mode.removeprefix('--');result=subprocess.run([out/'check',mode],capture_output=True,text=True,timeout=30)
 (out/(label+'.log')).write_text(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(str(out/(label+'.log')))
 records=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
 if len(records)!=1 or records[0].get('failed')!=0 or tag not in records[0]:raise RuntimeError('Invalid '+label+' report')
 reports[label]=records[0]
current()
reports.update(sanitizer=a.sanitizer,corpus_sha256=state['corpus_sha256'],sources=inputs,trace_objects=trace_inputs,
               instrumented='all C/C++/Objective-C++ HLE providers and checked CPU runtime',
               guest_ir_instrumented=False,full_game_booted=False,game_frames=False)
(out/'verification.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps({'sanitizer':a.sanitizer,'service_checks':reports['service-check']['service_checks'],
                  'worker_exception_cleanup':reports['worker-cleanup-check']['failed']==0,'guest_ir_instrumented':False,'game_frames':False}))
