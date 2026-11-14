#!/usr/bin/env python3
"""Qualify a frozen native Mac AOT/GPU bridge; never a mobile-frame claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('root','build','driver-manifest','output'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
root=a.root.resolve();build=a.build.resolve();out=a.output.resolve()
for path in (root,build,out):
    if len(path.parts)<3 or path.parts[1]!='Volumes' or not Path('/Volumes',path.parts[2]).is_mount():
        p.error('Use mounted external build/output directories')
if out.exists():p.error('--output must be new')
repo=Path(__file__).resolve().parents[2]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
state_path=root/'build-state.json';state_bytes=state_path.read_bytes();state=json.loads(state_bytes)
if state.get('runner_mode')!='native-arm64-gpu':p.error('Expected an established GPU-backed checkpoint')
manifest=a.driver_manifest.resolve();driver=Path(json.loads(manifest.read_text())['ICD']['library_path'])
if not driver.is_absolute():driver=(manifest.parent/driver).resolve()
cache=(build/'CMakeCache.txt').read_text().splitlines()
loader=Path(next(x.split('=',1)[1] for x in cache if x.startswith('Vulkan_LIBRARY:FILEPATH=')))
binary=build/'bin/osl-aot-gpu-probe'
inputs={'binary_sha256':binary,'renderer_sha256':build/'gpu/liboslgpu.a','driver_sha256':driver,
        'manifest_sha256':manifest,'loader_sha256':loader}
def current():
    if state_path.read_bytes()!=state_bytes:raise RuntimeError('Discovery changed the checkpoint; stop the producer first')
    for key,path in inputs.items():
        if digest(path)!=state['native_gpu'][key]:raise RuntimeError('Native GPU input changed: '+key)
    for name,wanted in state['sources'].items():
        if digest(repo/name)!=wanted:raise RuntimeError('Source changed: '+name)
    for name,wanted in state['headers'].items():
        if digest(root/name)!=wanted:raise RuntimeError('Generated header changed: '+name)
    assets=state['runtime_inputs'];app0=(root/'App0').resolve();marker=app0/'eboot.bin'
    actual=digest(marker) if marker.is_file() else None
    if str(app0)!=assets['app0'] or actual!=assets['app0_eboot_sha256']:raise RuntimeError('App0 marker changed')
current();out.mkdir(parents=True)
env=dict(os.environ,VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),SDL_VULKAN_LIBRARY=str(loader))
reports={}
def run(label,extra,prefix):
    current()
    with (out/(label+'.log')).open('w') as log:
        result=subprocess.run([binary,root/'image.bin',build/'aot-profile',*extra],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
    current()
    if result.returncode:raise RuntimeError(label+' failed; inspect '+str(out/(label+'.log')))
    lines=(out/(label+'.log')).read_text().splitlines()
    records=[json.loads(x[len(prefix):]) for x in lines if x.startswith(prefix)]
    if len(records)!=1:raise RuntimeError('Expected one '+prefix+' report')
    reports[label]=records[0]
    return lines,records[0]
_,checks=run('bridge',['--check-bridge'],'OSL_GPU_BRIDGE_CHECKS:')
assert checks['checks']>=60 and checks['failed']==0 and checks['game_frames'] is False and checks['game_executed'] is False
lines,guest=run('guest',[],'OSL_AOT_GPU_GUEST:')
prefix='OSL_AOT_GPU_RESULT:'
cleanup=[json.loads(x[len(prefix):]) for x in lines if x.startswith(prefix)]
assert len(cleanup)==1
cleanup=cleanup[0];reports['cleanup']=cleanup
reference=state['reference']
for key in ('completed_modules','error','missing_pc','missing_service','gpu_service_calls'):
    assert guest[key]==reference[key],key
assert guest['completed_modules']==2 and guest['fault_address']==0 and guest['gpu_adapter_enabled']
assert cleanup['opened']==cleanup['closed']==1 and cleanup['queues_created']==cleanup['queues_deleted']==2
assert cleanup['events_added']==2 and cleanup['attributes_set']==1 and cleanup['buffers_registered']==3
assert cleanup['mapped_before_reset'] and cleanup['unmapped_after_reset'] and cleanup['guest_resources_released']
assert cleanup['game_frames'] is False and cleanup['presented_frames']==0
reports.update(corpus_sha256=state['corpus_sha256'],platform='native arm64 macOS',physical_device=False,
               mobile_gpu_frontend_linked=False,game_frames=False,asset_validation='App0 path and eboot marker only')
(out/'verification.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps({'checks':checks['checks'],'trace_count':guest['trace_count'],'stop':guest['missing_service'] or guest['error'],
                  'buffers_registered':cleanup['buffers_registered'],'game_frames':False}))
