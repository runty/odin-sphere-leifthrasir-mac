#!/usr/bin/env python3
"""Verify native command-processor retirement before shared mapping teardown.

Requires a frozen native checkpoint whose renderer matches these processor
sources. Supporting native archives are snapshotted before linking. This fixture
submits fixed WriteData packets; it never executes game code or presents a frame.
"""
import argparse,hashlib,json,os,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('root','build','driver-manifest','output'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','undefined'),default='address')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];root=a.root.resolve();build=a.build.resolve();out=a.output.resolve()
for path in (root,build,out):
 if len(path.parts)<3 or path.parts[1]!='Volumes' or not Path('/Volumes',path.parts[2]).is_mount():p.error('Use mounted external paths')
if out.exists():p.error('--output must be new')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
sources={name:digest(repo/name) for name in ('gpu/shadps4/video_core/amdgpu/liverpool.h','gpu/shadps4/video_core/amdgpu/liverpool.cpp','ios/graphics/gnm-shader-packets.h','ios/graphics/gnm-submissions.h','ios/graphics/guest-presentation.h','ios/graphics/native-wait.h','ios/graphics/guest-gpu.h','ios/graphics/gpu-retirement-test.cpp','ios/graphics/test-gpu-retirement.py','ios/cpu/runtime.h','ios/services/gpu.h','ios/services/clock.h','src/oslgpu.h')}
state_path=root/'build-state.json';state_bytes=state_path.read_bytes();state=json.loads(state_bytes)
for name in ('gpu/shadps4/video_core/amdgpu/liverpool.h','gpu/shadps4/video_core/amdgpu/liverpool.cpp','ios/graphics/guest-gpu.h'):
 if state['sources'][name]!=sources[name]:p.error('Checkpoint has different renderer/adapter source: '+name)
layout=root/'image-layout.h';layout_bytes=layout.read_bytes();out.mkdir(parents=True);(out/'image-layout.h').write_bytes(layout_bytes);(out/'profile').mkdir()
def current():
 if state_path.read_bytes()!=state_bytes:raise RuntimeError('Discovery changed; stop the producer before qualification')
 if layout.read_bytes()!=layout_bytes:raise RuntimeError('Immutable image layout changed')
 for name,wanted in sources.items():
  if digest(repo/name)!=wanted:raise RuntimeError('Source changed: '+name)
def command(target):return subprocess.check_output(['ninja','-C',build,'-t','commands',target],text=True).splitlines()[-1]
compile=shlex.split(command('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o'));compile=compile[:compile.index('-c')];compile=compile[:compile.index('-o')] if '-o' in compile else compile
for flag in ('-MT','-MF'):
 if flag in compile:i=compile.index(flag);del compile[i:i+2]
compile=[x for x in compile if x!='-MD']
compile[1:1]=['-I'+str(out)]
compile+=['-O1','-fsanitize='+a.sanitizer,'-fno-omit-frame-pointer','-c',str(repo/'ios/graphics/gpu-retirement-test.cpp'),'-o',str(out/'check.o')]
current()
with (out/'build.log').open('w') as log:subprocess.run(compile,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
link=shlex.split(command('bin/osl-aot-gpu-probe').split(' && ')[1]);link.remove('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o');link[link.index('-o')+1]=str(out/'check');link+=[str(out/'check.o'),'-fsanitize='+a.sanitizer]
snapshots={}
for i,value in enumerate(link):
 if value.endswith('.a'):
  path=(build/value).resolve();data=path.read_bytes();copied=out/('input-'+str(i)+'.a');copied.write_bytes(data);link[i]=str(copied);snapshots[str(path)]=hashlib.sha256(data).hexdigest()
  if path==(build/'gpu/liboslgpu.a').resolve() and snapshots[str(path)]!=state['native_gpu']['renderer_sha256']:raise RuntimeError('Renderer archive differs from the frozen processor checkpoint')
with (out/'link.log').open('w') as log:subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
manifest=a.driver_manifest.resolve();driver=Path(json.loads(manifest.read_text())['ICD']['library_path']);driver=driver if driver.is_absolute() else manifest.parent/driver
loader=next(x.split('=',1)[1] for x in (build/'CMakeCache.txt').read_text().splitlines() if x.startswith('Vulkan_LIBRARY:FILEPATH='))
native_inputs={str(path):digest(path) for path in (manifest,driver,Path(loader))}
env=dict(os.environ,VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),SDL_VULKAN_LIBRARY=loader,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
r=subprocess.run([out/'check',out/'profile'],capture_output=True,text=True,env=env,timeout=40);(out/'results.log').write_text(r.stdout+r.stderr)
if r.returncode:raise RuntimeError(str(out/'results.log'))
records=[json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')];assert len(records)==1
report=records[0];assert report['failed']==0 and not report['game_executed'] and not report['game_frames']
current()
for name,wanted in native_inputs.items():
 if digest(Path(name))!=wanted:raise RuntimeError('Driver/loader changed: '+name)
report.update(sanitizer=a.sanitizer,instrumented='host retirement barrier and fixture only; supporting HLE archive, CPU runtime, renderer, driver and system libraries uninstrumented',corpus_sha256=state['corpus_sha256'],supporting_archive_snapshots=snapshots,sources=sources,native_inputs=native_inputs,image_layout_sha256=hashlib.sha256(layout_bytes).hexdigest(),physical_device=False,binary_sha256=digest(out/'check'))
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['gpu_retirement_checks','failed','game_executed','game_frames']}))
