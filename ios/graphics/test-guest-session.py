#!/usr/bin/env python3
"""Run the actual shared guest boundary against an isolated native Mac renderer."""
import argparse,hashlib,json,os,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('root','build','output','driver-manifest'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitizer',choices=('address','thread'))
a=p.parse_args();repo=Path(__file__).resolve().parents[2];root=a.root.resolve();build=a.build.resolve();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a NEW mounted external test directory')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
state_bytes=(root/'build-state.json').read_bytes();state=json.loads(state_bytes)
sources={name:digest(repo/name) for name in ('ios/graphics/guest-session.cpp','ios/graphics/guest-session.h','ios/graphics/guest-session-test.cpp','ios/graphics/guest-gpu.h')}
def current():
 if (root/'build-state.json').read_bytes()!=state_bytes:raise RuntimeError('Discovery changed; stop it before testing')
 for inputs in (state['sources'],sources):
  for name,wanted in inputs.items():
   if digest(repo/name)!=wanted:raise RuntimeError('Source changed: '+name)
 for name,wanted in state['headers'].items():
  if digest(root/name)!=wanted:raise RuntimeError('Header changed: '+name)
current();out.mkdir(parents=True)
def command_for(target):
    commands=subprocess.check_output(['ninja','-C',build,'-t','commands',target],text=True).splitlines()
    return commands[-1]
compile=shlex.split(command_for('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o'))
start=compile.index('-c');compile=compile[:start];compile=compile[:compile.index('-o')] if '-o' in compile else compile
# Drop dependency-file flags: each fixture owns its output and log.
for flag in ('-MT','-MF'):
 if flag in compile:i=compile.index(flag);del compile[i:i+2]
compile=[x for x in compile if x!='-MD']
if a.sanitizer:compile+=['-O1','-fno-omit-frame-pointer','-fsanitize='+a.sanitizer]
objects=[]
for name in ('guest-session','guest-session-test'):
 current();obj=out/(name+'.o');objects.append(obj)
 with (out/(name+'-compile.log')).open('w') as log:subprocess.run([*compile,'-c',str(repo/'ios/graphics'/(name+'.cpp')),'-o',str(obj)],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
link=shlex.split(command_for('bin/osl-aot-gpu-probe').split(' && ')[1]);link.remove('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o')
link[link.index('-o')+1]=str(out/'check');link.extend(map(str,objects))
if a.sanitizer:link+=['-fsanitize='+a.sanitizer]
with (out/'link.log').open('w') as log:subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
manifest=a.driver_manifest.resolve();driver=Path(json.loads(manifest.read_text())['ICD']['library_path']);driver=driver if driver.is_absolute() else manifest.parent/driver
loader=next(line.split('=',1)[1] for line in (build/'CMakeCache.txt').read_text().splitlines() if line.startswith('Vulkan_LIBRARY:FILEPATH='))
env=dict(os.environ,VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),SDL_VULKAN_LIBRARY=loader)
if a.sanitizer=='address':env['ASAN_OPTIONS']='detect_leaks=0'
def run(label,args,prefix):
 current();r=subprocess.run([out/'check',*args],env=env,capture_output=True,text=True,timeout=30)
 (out/(label+'.log')).write_text(r.stdout+r.stderr);current()
 if r.returncode:raise RuntimeError(str(out/(label+'.log')))
 records=[json.loads(s[len(prefix):]) for s in r.stdout.splitlines() if s.startswith(prefix)]
 if len(records)!=1:raise RuntimeError('Missing '+prefix)
 return records[0],r.stdout
reports={}
for mode in ('normal','pre-stop','live-stop'):
 profile=out/(mode+'-profile');profile.mkdir()
 result,stdout=run(mode,[root/'image.bin',profile,mode],'OSL_GUEST_SESSION:')
 ownership=[json.loads(s.removeprefix('OSL_GUEST_OWNERSHIP:')) for s in stdout.splitlines() if s.startswith('OSL_GUEST_OWNERSHIP:')];assert len(ownership)==1
 guest=result['guest'];cleanup=result['cleanup'];assert cleanup['passed'] and cleanup['guest_resources_released']
 assert cleanup['opened']==cleanup['closed'] and cleanup['queues_created']==cleanup['queues_deleted']
 assert guest['fault_address']==0 and guest['workers_started']==guest['workers_finished']
 if mode=='pre-stop':assert guest['completed_modules']==0 and guest['error']=='guest execution interrupted' and cleanup['opened']==0
 else:
  assert guest['completed_modules']==2 and cleanup['opened']==1 and cleanup['buffers_registered']==3
  assert cleanup['mapped_before_reset'] and cleanup['unmapped_after_reset']
  if mode=='live-stop':assert result['test_gate_enabled'] and result['test_gate_reached']
  if mode=='live-stop' and guest['error']!='guest execution interrupted':
   # Main-thread polling can deliver cancellation after the CPU has already
   # stopped, while the native report/cleanup is still running. Preserve that
   # outcome rather than misreporting it as an instruction interruption.
   assert guest['error']=='missing translated block'
   offset=guest['missing_pc']-state['trace_inputs']['base']
   layout=json.loads((root/'image.json').read_text())
   assert any(flags&1 and start<=offset<start+size for start,size,flags in layout['segments'])
 assert result['presented_frames']==0 and result['game_frames'] is False
 reports[mode]={'guest':guest,'cleanup':cleanup,'ownership':ownership[0],
                'cpu_interruption_observed':guest['error']=='guest execution interrupted'}
fixtures=out/'images';fixtures.mkdir();image=fixtures/'image.bin';pointers=fixtures/'image-pointers.bin'
shutil.copy2(root/'image.bin',image);shutil.copy2(root/'image-pointers.bin',pointers)
validation={}
def validate(label,valid):validation[label]=run(label,['--validate',image,'valid' if valid else 'invalid'],'OSL_GUEST_VALIDATE:')[0]
validate('valid-image',True)
with image.open('r+b') as file:byte=file.read(1);file.seek(0);file.write(bytes([byte[0]^1]))
validate('corrupt-image',False);shutil.copy2(root/'image.bin',image)
with image.open('r+b') as file:file.truncate(image.stat().st_size-1)
validate('truncated-image',False);shutil.copy2(root/'image.bin',image)
with pointers.open('r+b') as file:byte=file.read(1);file.seek(0);file.write(bytes([byte[0]^1]))
validate('corrupt-pointers',False);pointers.rename(fixtures/'invalid-pointers.bin');validate('missing-pointers',False)
current();report={'passed':True,'corpus_sha256':state['corpus_sha256'],'sessions':reports,'validation':validation,'sources':sources,'binary_sha256':digest(out/'check'),'driver_sha256':digest(driver),'platform':'native arm64 macOS','sanitizer':a.sanitizer,'instrumented':'guest-session boundary and fixture only; renderer, game archive and system/driver libraries uninstrumented','physical_device':False,'game_frames':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'sessions':list(reports),'validation_cases':len(validation),'game_frames':False}))
