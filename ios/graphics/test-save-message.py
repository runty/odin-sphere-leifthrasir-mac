#!/usr/bin/env python3
"""Exercise the actual native renderer message API, input edges and UI teardown."""
import argparse,hashlib,json,os,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ['root','build','driver-manifest']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();root=a.root.resolve();build=a.build.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use mounted external storage')
root.mkdir(parents=True,exist_ok=True);(root/'profile').mkdir(exist_ok=True)
def command(target):return subprocess.check_output(['ninja','-C',build,'-t','commands',target],text=True).splitlines()[-1]
compile=shlex.split(command('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o'));compile=compile[:compile.index('-c')];compile=compile[:compile.index('-o')] if '-o' in compile else compile
for flag in ['-MT','-MF']:
 if flag in compile:i=compile.index(flag);del compile[i:i+2]
compile=[x for x in compile if x!='-MD'];compile+=['-I'+str(repo/'gpu/third_party/imgui'),'-c',str(repo/'ios/graphics/save-message-test.cpp'),'-o',str(root/'check.o')]
with (root/'compile.log').open('w') as log:subprocess.run(compile,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
link=shlex.split(command('bin/osl-aot-gpu-probe').split(' && ')[1]);link.remove('CMakeFiles/osl-aot-gpu-probe.dir/ios/graphics/aot-frontend.cpp.o');link[link.index('-o')+1]=str(root/'check');link.append(str(root/'check.o'))
inputs={}
for i,value in enumerate(link):
 if value.endswith('.a'):
  path=(build/value).resolve();data=path.read_bytes();copy=root/('input-'+str(i)+'.a');copy.write_bytes(data);link[i]=str(copy);inputs[str(path)]=hashlib.sha256(data).hexdigest()
with (root/'link.log').open('w') as log:subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
manifest=a.driver_manifest.resolve();driver=Path(json.loads(manifest.read_text())['ICD']['library_path']);driver=driver if driver.is_absolute() else manifest.parent/driver
loader=next(x.split('=',1)[1] for x in (build/'CMakeCache.txt').read_text().splitlines() if x.startswith('Vulkan_LIBRARY:FILEPATH='))
env=dict(os.environ,VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),SDL_VULKAN_LIBRARY=loader)
run=subprocess.run([root/'check',root/'profile'],env=env,capture_output=True,text=True,timeout=45)
(root/'results.log').write_text(run.stdout+run.stderr);run.check_returncode()
report=json.loads(next(s for s in reversed(run.stdout.splitlines()) if s.startswith('{')));assert report['failed']==0
report.update(supporting_archives=inputs,scope='actual renderer UI state and injected touch events, not physical touches or visual pixel verification',sources={str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [repo/'ios/graphics/save-message-test.cpp',Path(__file__).resolve(),repo/'gpu/shim/osl_overlay.cpp',repo/'gpu/shim/oslgpu.cpp']})
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
