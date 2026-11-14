#!/usr/bin/env python3
"""Build the pinned SDL UIKit/Metal/audio/controller dependency in an isolated iOS tree."""
import argparse
import fcntl
import hashlib
import json
import re
from pathlib import Path
import subprocess
import tarfile
import urllib.request

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
p.add_argument('--jobs',type=int,default=6)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external build directory')
if not 1<=a.jobs<=16:p.error('Use 1 to 16 jobs')
root.mkdir(parents=True,exist_ok=True)
lease=(root/'.sdl-build.lock').open('a')
try:fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:p.error('Another SDL dependency build is active in this root')

item=json.loads((repo/'macos/dependency-lock.json').read_text())['archives']['sdl']
archive=root/('SDL3-'+item['version']+'.tar.gz')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(label,args):
 with (root/(label+'.log')).open('w') as log:
  subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True)
if not archive.exists():
 temporary=archive.with_suffix('.download')
 request=urllib.request.Request(item['url'],headers={'User-Agent':'Odin-iOS-dependency-builder'})
 with urllib.request.urlopen(request,timeout=60) as incoming,temporary.open('wb') as out:
  while chunk:=incoming.read(1024*1024):out.write(chunk)
 if digest(temporary)!=item['sha256']:raise RuntimeError('SDL archive hash mismatch; candidate left for inspection')
 temporary.replace(archive)
if digest(archive)!=item['sha256']:raise RuntimeError('Cached SDL archive changed')
source=root/('SDL3-'+item['version'])
if not source.exists():
 with tarfile.open(archive) as tar:tar.extractall(root,filter='data')
if not (source/'CMakeLists.txt').is_file():raise RuntimeError('Unexpected SDL source layout')
def inventory():
 return {str(path.relative_to(source)):digest(path) for path in sorted(source.rglob('*')) if path.is_file()}
current=inventory();ledger=root/'source-inputs.json'
if ledger.exists():
 if json.loads(ledger.read_text())!=current:raise RuntimeError('SDL source tree changed; keep it and use a new root')
else:ledger.write_text(json.dumps(current,sort_keys=True,indent=2)+'\n')
sdk=Path(subprocess.check_output(['xcrun','--sdk',a.sdk,'--show-sdk-path'],text=True).strip())
build=root/('build-'+a.sdk);prefix=root/('prefix-'+a.sdk)
options=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_SYSROOT='+str(sdk),'-DCMAKE_OSX_ARCHITECTURES=arm64',
 '-DCMAKE_OSX_DEPLOYMENT_TARGET=27.2','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_BUILD_TYPE=Release',
 '-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DSDL_SHARED=OFF','-DSDL_STATIC=ON','-DSDL_FRAMEWORK=OFF',
 '-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_EXAMPLES=OFF','-DSDL_INSTALL=ON',
 '-DSDL_METAL=ON','-DSDL_VULKAN=OFF','-DSDL_RENDER=OFF','-DSDL_GPU=OFF','-DSDL_OPENGL=OFF','-DSDL_OPENGLES=OFF']
run('configure-'+a.sdk,['cmake','-G','Ninja','-S',source,'-B',build,*options])
run('build-'+a.sdk,['cmake','--build',build,'--parallel',a.jobs])
run('install-'+a.sdk,['cmake','--install',build])
if inventory()!=current:raise RuntimeError('SDL inputs changed while building; no manifest published')
library=prefix/'lib/libSDL3.a'
if not library.is_file():raise RuntimeError('Expected static SDL library missing')
archs=subprocess.check_output(['lipo','-archs',library],text=True).strip()
if archs!='arm64':raise RuntimeError('Unexpected SDL architecture: '+archs)
members=subprocess.check_output(['ar','-t',library],text=True).splitlines()
member=next(name for name in members if name.endswith('.o'))
object_path=root/('platform-check-'+a.sdk+'.o')
with object_path.open('wb') as out:subprocess.run(['ar','-p',str(library),member],stdout=out,check=True)
platform=subprocess.check_output(['xcrun','vtool','-show-build',object_path],text=True)
(root/('platform-'+a.sdk+'.log')).write_text(platform)
expected='IOSSIMULATOR' if a.sdk=='iphonesimulator' else 'IOS'
lines=[line.strip().split() for line in platform.splitlines() if line.strip().startswith('platform ')]
if lines!=[['platform',expected]]:raise RuntimeError('SDL object is not built for the requested iOS platform')
all_objects=root/('all-objects-'+a.sdk+'.log')
with all_objects.open('w') as out:
 subprocess.run(['xcrun','llvm-objdump','--macho','--private-headers',str(library)],stdout=out,stderr=subprocess.STDOUT,check=True)
platforms=re.findall(r'^\s*platform\s+(\S+)',all_objects.read_text(),re.M)
if len(platforms)!=sum(name.endswith('.o') for name in members) or set(platforms)!={expected.lower()}:
 raise RuntimeError('Some SDL archive members lack the requested iOS platform')

manifest={'version':item['version'],'archive_sha256':item['sha256'],'source_inventory_sha256':digest(ledger),
 'sdk':str(sdk),'platform':a.sdk,'architectures':archs,'cmake_options':options,'library':str(library),
 'library_sha256':digest(library),'platform_object':member,'all_object_platforms_checked':len(platforms),'metal_view_enabled':True,'vulkan_loader_enabled':False,
 'gpu_frontend_linked':False,'device_executed':False,'game_frames':False}
(root/('inputs-'+a.sdk+'.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest))
