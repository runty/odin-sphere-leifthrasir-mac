#!/usr/bin/env python3
"""Cross-build pinned renderer support libraries without using Mac arm64 archives."""
import argparse,fcntl,hashlib,json,re,subprocess,shutil,tarfile,urllib.request
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
p.add_argument('--jobs',type=int,default=6)
p.add_argument('--archive-cache',type=Path,help='Optional read-only cache containing the hash-pinned png.tar.gz')
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
  if name in lock:
   if git(path,'rev-parse','HEAD')!=lock[name]['commit'] or git(path,'status','--porcelain','--untracked-files=all'):
    raise RuntimeError('Dependency differs from its pin: '+name)
  elif inventory(path)!=archive_inventory[name]:raise RuntimeError('Archive source changed: '+name)
for name,item in lock.items():
 path=root/'src'/name
 if not path.exists():
  path.parent.mkdir(exist_ok=True)
  run('source-init-'+name,['git','init',path])
  run('source-fetch-'+name,['git','-C',path,'fetch','--depth','1',item['url'],item['commit']])
  run('checkout-'+name,['git','-C',path,'checkout','--detach','FETCH_HEAD'])
 sources[name]=path
 audit()
# Share the Mac lock's known libpng archive, but build completely separate objects.
png=json.loads((source.parents[1]/'macos/dependency-lock.json').read_text())['archives']['png']
archive=root/'png.tar.gz'
if not archive.exists():
 temporary=root/'png.download'
 cached=a.archive_cache/'png.tar.gz' if a.archive_cache else None
 if cached and cached.is_file():shutil.copyfile(cached,temporary)
 else:
  request=urllib.request.Request(png['url'],headers={'User-Agent':'Odin-iOS-dependency-builder'})
  with urllib.request.urlopen(request,timeout=60) as incoming,temporary.open('wb') as output:
   shutil.copyfileobj(incoming,output)
 if digest(temporary)!=png['sha256']:raise RuntimeError('PNG archive hash mismatch; retaining candidate')
 temporary.replace(archive)
if digest(archive)!=png['sha256']:raise RuntimeError('Cached PNG archive changed')
png_source=root/('libpng-'+png['version'])
if not png_source.exists():
 with tarfile.open(archive) as tar:tar.extractall(root,filter='data')
if not (png_source/'CMakeLists.txt').is_file():raise RuntimeError('Unexpected PNG source layout')
def inventory(path):return {str(p.relative_to(path)):digest(p) for p in sorted(path.rglob('*')) if p.is_file()}
archive_inventory={'png':inventory(png_source)};ledger=root/'source-png.json'
if ledger.exists():
 if json.loads(ledger.read_text())!=archive_inventory['png']:raise RuntimeError('PNG source tree changed; use a new root')
else:ledger.write_text(json.dumps(archive_inventory['png'],sort_keys=True,indent=2)+'\n')
sources['png']=png_source;audit()
boost=json.loads((source/'archive-lock.json').read_text())['boost']
boost_archive=root/'boost.tar.xz'
if not boost_archive.exists():
 temporary=root/'boost.download';request=urllib.request.Request(boost['url'],headers={'User-Agent':'Odin-iOS-dependency-builder'})
 with urllib.request.urlopen(request,timeout=60) as incoming,temporary.open('wb') as output:shutil.copyfileobj(incoming,output)
 if digest(temporary)!=boost['sha256']:raise RuntimeError('Boost archive hash mismatch')
 temporary.replace(boost_archive)
if digest(boost_archive)!=boost['sha256']:raise RuntimeError('Cached Boost archive changed')
boost_source=root/('boost-'+boost['version'])
if not boost_source.exists():
 with tarfile.open(boost_archive) as tar:
  members=[member for member in tar.getmembers() if len(Path(member.name).parts)>1 and Path(member.name).parts[1] in ('boost','LICENSE_1_0.txt')]
  if not members:raise RuntimeError('Unexpected Boost archive layout')
  tar.extractall(root,members=members,filter='data')
boost_input=inventory(boost_source);boost_ledger=root/'source-boost-headers.json'
if boost_ledger.exists():
 if json.loads(boost_ledger.read_text())!=boost_input:raise RuntimeError('Boost header source changed; use a new root')
else:boost_ledger.write_text(json.dumps(boost_input,sort_keys=True,indent=2)+'\n')
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
zlib=sdk/'usr/lib/libz.tbd'
if not zlib.is_file() or not (sdk/'usr/include/zlib.h').is_file():raise RuntimeError('Target SDK zlib is unavailable')
build('png',extra=['-DPNG_SHARED=OFF','-DPNG_STATIC=ON','-DPNG_TESTS=OFF','-DPNG_TOOLS=OFF','-DPNG_EXECUTABLES=OFF',
 '-DZLIB_LIBRARY='+str(zlib),'-DZLIB_INCLUDE_DIR='+str(sdk/'usr/include')])
# Only architecture-independent headers are shared between the two target SDKs.
if inventory(boost_source)!=boost_input:raise RuntimeError('Boost headers changed during build')
boost_headers=boost_source/'boost';installed_boost=prefix/'include/boost'
if installed_boost.exists():
 if inventory(installed_boost)!=inventory(boost_headers):raise RuntimeError('Installed Boost headers changed')
else:shutil.copytree(boost_headers,installed_boost)
boost_package=prefix/'lib/cmake/Boost';boost_package.mkdir(parents=True,exist_ok=True)
(boost_package/'BoostConfig.cmake').write_text('get_filename_component(_boost_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\nif(NOT TARGET Boost::headers)\n add_library(Boost::headers INTERFACE IMPORTED)\n set_target_properties(Boost::headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_boost_prefix}/include")\nendif()\nset(Boost_FOUND TRUE)\nset(Boost_VERSION 109200)\nset(Boost_VERSION_STRING "1.92.0")\n')
vma_header=sources['vma']/'include/vk_mem_alloc.h';installed_vma=prefix/'include/vk_mem_alloc.h'
if installed_vma.exists() and digest(installed_vma)!=digest(vma_header):raise RuntimeError('Installed VMA header changed')
shutil.copy2(vma_header,installed_vma)
notices=prefix/'share/odin-renderer-notices';notices.mkdir(parents=True,exist_ok=True)
shutil.copy2(boost_source/'LICENSE_1_0.txt',notices/'Boost-LICENSE.txt')
shutil.copy2(sources['vma']/'LICENSE.txt',notices/'VMA-LICENSE.txt')
audit()
libraries={};expected='iossimulator' if a.sdk=='iphonesimulator' else 'ios'
for name in ('libfmt.a','libxxhash.a','libZycore.a','libZydis.a','libpng16.a'):
 path=prefix/'lib'/name
 if not path.is_file():raise RuntimeError('Missing static archive: '+name)
 if subprocess.check_output(['lipo','-archs',path],text=True).strip()!='arm64':raise RuntimeError('Wrong archive architecture: '+name)
 members=subprocess.check_output(['ar','-t',path],text=True).splitlines();out=root/(name+'-'+a.sdk+'-platforms.log')
 with out.open('w') as log:subprocess.run(['xcrun','llvm-objdump','--macho','--private-headers',str(path)],stdout=log,stderr=subprocess.STDOUT,check=True)
 platforms=re.findall(r'^\s*platform\s+(\S+)',out.read_text(),re.M)
 if len(platforms)!=sum(x.endswith('.o') for x in members) or set(platforms)!={expected}:raise RuntimeError('Wrong/missing member platform in '+name)
 libraries[name]={'sha256':digest(path),'objects_checked':len(platforms)}
manifest={'platform':a.sdk,'sdk':str(sdk),'architecture':'arm64','minimum_version':'27.2','sources':lock,'archives':{'png':png,'boost':boost},'boost_source_inventory_sha256':digest(boost_ledger),'vma_header_sha256':digest(installed_vma),'archive_source_inventory_sha256':digest(ledger),'target_zlib':str(zlib),
 'options':options,'per_library_options':per_library_options,'builder_sha256':digest(Path(__file__).resolve()),'prefix':str(prefix),'libraries':libraries,'header_only':['robin-map','boost','vma'],'gpu_frontend_linked':False,'device_executed':False,'game_frames':False}
(root/('inputs-'+a.sdk+'.json')).write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest))
