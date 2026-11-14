#!/usr/bin/env python3
"""Cross-build pinned renderer support libraries without using Mac arm64 archives."""
import argparse,fcntl,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
p.add_argument('--jobs',type=int,default=6)
a=p.parse_args();root=a.root.resolve();source=Path(__file__).resolve().parent
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external build directory')
if not 1<=a.jobs<=16:p.error('Use 1 to 16 jobs')
root.mkdir(parents=True,exist_ok=True);lease=(root/'.dependency-build.lock').open('a')
try:fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:p.error('Another dependency build owns this root')
lock=json.loads((source/'dependency-lock.json').read_text());sources={}
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(label,args):
 with (root/(label+'.log')).open('w') as log:subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True)
def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
def audit():
 for name,path in sources.items():
  if git(path,'rev-parse','HEAD')!=lock[name]['commit'] or git(path,'status','--porcelain','--untracked-files=all'):
   raise RuntimeError('Dependency differs from its pin: '+name)
for name,item in lock.items():
 path=root/'src'/name
 if not path.exists():
  path.parent.mkdir(exist_ok=True)
  run('clone-'+name,['git','clone','--no-checkout',item['url'],path])
  run('checkout-'+name,['git','-C',path,'checkout','--detach',item['commit']])
 sources[name]=path
 audit()
sdk=Path(subprocess.check_output(['xcrun','--sdk',a.sdk,'--show-sdk-path'],text=True).strip());prefix=root/('prefix-'+a.sdk)
options=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_SYSROOT='+str(sdk),'-DCMAKE_OSX_ARCHITECTURES=arm64',
 '-DCMAKE_OSX_DEPLOYMENT_TARGET=27.2','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
 '-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DCMAKE_INSTALL_PREFIX='+str(prefix),
 '-DCMAKE_PREFIX_PATH='+str(prefix),'-DCMAKE_FIND_ROOT_PATH='+str(prefix),'-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY',
 '-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER']
per_library_options={}
def build(name,subdir='',extra=()):
 audit();per_library_options[name]=list(extra);directory=root/('build-'+a.sdk)/name
 run('configure-'+a.sdk+'-'+name,['cmake','-G','Ninja','-S',sources[name]/subdir,'-B',directory,*options,*extra])
 run('build-'+a.sdk+'-'+name,['cmake','--build',directory,'--parallel',a.jobs])
 run('install-'+a.sdk+'-'+name,['cmake','--install',directory]);audit()
build('fmt',extra=['-DFMT_TEST=OFF','-DFMT_DOC=OFF','-DFMT_INSTALL=ON'])
build('xxhash','cmake_unofficial',['-DXXHASH_BUILD_XXHSUM=OFF'])
build('zycore',extra=['-DZYCORE_BUILD_SHARED_LIB=OFF','-DZYCORE_BUILD_EXAMPLES=OFF','-DZYCORE_BUILD_TESTS=OFF'])
zycore_alias=root/'zydis-system-zycore.cmake'
zycore_alias.write_text('find_package(Zycore CONFIG REQUIRED)\nif(NOT TARGET Zycore)\n  add_library(Zycore ALIAS Zycore::Zycore)\nendif()\n')
build('zydis',extra=['-DCMAKE_PROJECT_INCLUDE='+str(zycore_alias),'-DZYAN_SYSTEM_ZYCORE=ON','-DZYAN_ZYCORE_PATH='+str(sources['zydis']/'dependencies/zycore'),'-DZYDIS_BUILD_SHARED_LIB=OFF','-DZYDIS_BUILD_EXAMPLES=OFF','-DZYDIS_BUILD_TOOLS=OFF','-DZYDIS_BUILD_MAN=OFF','-DZYDIS_BUILD_DOXYGEN=OFF','-DZYDIS_BUILD_TESTS=OFF'])
build('robin-map',extra=['-DTSL_ROBIN_MAP_BUILD_TESTS=OFF'])
libraries={};expected='iossimulator' if a.sdk=='iphonesimulator' else 'ios'
for name in ('libfmt.a','libxxhash.a','libZycore.a','libZydis.a'):
 path=prefix/'lib'/name
 if not path.is_file():raise RuntimeError('Missing static archive: '+name)
 if subprocess.check_output(['lipo','-archs',path],text=True).strip()!='arm64':raise RuntimeError('Wrong archive architecture: '+name)
 members=subprocess.check_output(['ar','-t',path],text=True).splitlines();out=root/(name+'-'+a.sdk+'-platforms.log')
 with out.open('w') as log:subprocess.run(['xcrun','llvm-objdump','--macho','--private-headers',str(path)],stdout=log,stderr=subprocess.STDOUT,check=True)
 platforms=re.findall(r'^\s*platform\s+(\S+)',out.read_text(),re.M)
 if len(platforms)!=sum(x.endswith('.o') for x in members) or set(platforms)!={expected}:raise RuntimeError('Wrong/missing member platform in '+name)
 libraries[name]={'sha256':digest(path),'objects_checked':len(platforms)}
manifest={'platform':a.sdk,'sdk':str(sdk),'architecture':'arm64','minimum_version':'27.2','sources':lock,
 'options':options,'per_library_options':per_library_options,'builder_sha256':digest(Path(__file__).resolve()),'prefix':str(prefix),'libraries':libraries,'header_only':['robin-map'],'gpu_frontend_linked':False,'device_executed':False,'game_frames':False}
(root/('inputs-'+a.sdk+'.json')).write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest))
