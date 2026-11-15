#!/usr/bin/env python3
"""Compare real private-image serial/parallel prefetch output and observed lifter bounds."""
import argparse,hashlib,json,shutil,signal,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('root','output','remill','function-bounds'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();repo=Path(__file__).resolve().parents[2];origin=a.root.resolve();out=a.output.resolve();backend=a.remill.resolve()
for directory in (origin,out):
 if len(directory.parts)<3 or directory.parts[1]!='Volumes' or not Path('/Volumes',directory.parts[2]).is_mount():p.error('Use mounted external fixture/input directories')
if out.exists():p.error('--output must be new')
state_bytes=(origin/'build-state.json').read_bytes();state=json.loads(state_bytes);layout=json.loads((origin/'image.json').read_text())
seed=hex(layout['base']+layout['modules'][0][2])+'.ll';raw=(origin/'traces'/seed).read_bytes();digest=lambda data:hashlib.sha256(data).hexdigest()
if digest(raw)!=state['ir'][seed] or state['trace_inputs']!=json.loads((origin/'trace-identity.json').read_text()):p.error('Seed trace/backend identity differs from checkpoint')
out.mkdir(parents=True);reports={}
for jobs in (1,4):
 root=out/('jobs-'+str(jobs));root.mkdir()
 for name in ('image.bin','image-pointers.bin'):(root/name).hardlink_to(origin/name)
 for name in ('image.json','block-size','block-size.sha256','trace-identity.json'):shutil.copy2(origin/name,root/name)
 (root/'App0').symlink_to((origin/'App0').resolve());(root/'traces').mkdir();(root/'traces'/seed).write_bytes(raw)
 (root/'seed-provenance.json').write_text(json.dumps({'source_checkpoint_sha256':digest(state_bytes),'trace':seed,'raw_sha256':digest(raw),'identity':state['trace_inputs']},indent=2)+'\n')
 command=[sys.executable,str(repo/'ios/game/advance.py'),'--root',str(root),'--remill',str(backend),'--rounds','2','--prefetch','64','--prefetch-unwind','--retain-error-paths','--function-bounds',str(a.function_bounds.resolve()),'--jobs',str(jobs)]
 maximum=0;observed=[]
 with (root/'progress.log').open('w') as log:
  process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+300
   while process.poll() is None:
    if time.monotonic()>deadline:raise RuntimeError('Prefetch fixture exceeded its five-minute bound')
    processes=subprocess.check_output(['ps','-axo','pid=,ppid=,comm='],text=True)
    children=[int(fields[0]) for line in processes.splitlines() if len(fields:=line.split(None,2))==3 and int(fields[1])==process.pid and fields[2].endswith('/remill-lift-23')]
    if len(children)>maximum:maximum=len(children);observed.append({'time':time.time(),'lifters':children})
    time.sleep(.03)
   if process.returncode:raise RuntimeError(str(root/'progress.log'))
  finally:
   if process.poll() is None:
    # Let advance.py cancel queued futures and join active lifters before
    # the test returns an error; SIGTERM would bypass that Python cleanup.
    process.send_signal(signal.SIGINT)
    for _ in range(36):
     try:process.wait(timeout=5);break
     except subprocess.TimeoutExpired:pass
    else:
     process.kill();process.wait(timeout=10)
     raise RuntimeError('Fixture did not complete its interrupted cleanup')
 assert 1<=maximum<=jobs
 if jobs>1:assert maximum>=2,'No concurrent lifters observed; do not claim concurrency from configuration alone'
 ledger=json.loads((root/'static-traces.json').read_text());checkpoint=json.loads((root/'build-state.json').read_text());lines=(root/'progress.log').read_text().splitlines()
 ordered=[json.loads(line.split(':',1)[1]) for line in lines if line.startswith('STATIC_TRANSLATION:')];metrics=[json.loads(line.split(':',1)[1]) for line in lines if line.startswith('STATIC_PREFETCH:')]
 assert len(metrics)==2 and all(v['candidates']==64 and v['accepted']+v['skipped']==64 and v['workers']==jobs for v in metrics)
 for name,record in ledger['entries'].items():
  if record['status']=='accepted':assert digest((root/'traces'/name).read_bytes())==record['raw_sha256']
 reports[jobs]={'ledger':ledger,'ordered':ordered,'state':checkpoint,'observed':observed,'max_concurrent_lifters':maximum,'metrics':metrics}
assert reports[1]['ledger']==reports[4]['ledger'] and reports[1]['ordered']==reports[4]['ordered']
assert reports[1]['state']['ir']==reports[4]['state']['ir'] and reports[1]['state']['headers']==reports[4]['state']['headers']
assert reports[1]['state']['reference']['error']==reports[4]['state']['reference']['error']
report={'passed':True,'scope':'actual private-image staged traces, deterministic ledger/order/header equivalence and observed bounded lifters; not full-game boot or FPS','identical_ledgers':True,'identical_ordered_records':True,'identical_checkpoint_ir_and_headers':True,'runtime_sha256':digest((repo/'ios/cpu/runtime.cpp').read_bytes()),'builder_sha256':digest((repo/'ios/game/advance.py').read_bytes()),'cases':{str(jobs):{'ledger_entries':len(v['ledger']['entries']),'compiled_entries':len(v['state']['ir']),'corpus_sha256':v['state']['corpus_sha256'],'reference':v['state']['reference'],'max_concurrent_lifters':v['max_concurrent_lifters'],'observations':v['observed'],'staging_metrics':v['metrics']} for jobs,v in reports.items()}}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='cases'}))
