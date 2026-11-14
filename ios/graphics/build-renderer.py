#!/usr/bin/env python3
"""Compile the full renderer archive for a mobile SDK; no driver/game execution claim."""
import argparse,fcntl,hashlib,json,os,plistlib,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--dependencies',type=Path,required=True)
p.add_argument('--sdl-prefix',type=Path,required=True)
p.add_argument('--headers',type=Path,required=True)
p.add_argument('--driver-root',type=Path,help='Optionally link a non-installed SDK consumer against the matching signed ICD framework')
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
p.add_argument('--jobs',type=int,default=6)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external build directory')
if not 1<=a.jobs<=16:p.error('Use 1 to 16 jobs')
root.mkdir(parents=True,exist_ok=True);lease=(root/'.renderer-build.lock').open('a')
try:fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:p.error('Another renderer build owns this root')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(label,args,env=None):
 with (root/(label+'.log')).open('w') as log:subprocess.run(list(map(str,args)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
dependency_path=a.dependencies/('inputs-'+a.sdk+'.json');dependency_bytes=dependency_path.read_bytes()
deps=json.loads(dependency_bytes);prefix=Path(deps['prefix'])
if deps['platform']!=a.sdk or deps['architecture']!='arm64':p.error('Dependency SDK mismatch')
for name,item in deps['libraries'].items():
 if digest(prefix/'lib'/name)!=item['sha256']:p.error('Dependency archive changed: '+name)
if digest(prefix/'include/vk_mem_alloc.h')!=deps['vma_header_sha256']:p.error('VMA header changed')
sdl_prefix=a.sdl_prefix.resolve();sdl=json.loads((sdl_prefix.parent/('inputs-'+a.sdk+'.json')).read_text());library=sdl_prefix/'lib/libSDL3.a'
if sdl['platform']!=a.sdk or sdl['architectures']!='arm64' or Path(sdl['library'])!=library or digest(library)!=sdl['library_sha256']:p.error('SDL SDK or archive mismatch')
sdl_manifest_path=sdl_prefix.parent/('inputs-'+a.sdk+'.json');sdl_manifest_bytes=sdl_manifest_path.read_bytes()
def audit_binary_inputs():
 if dependency_path.read_bytes()!=dependency_bytes or sdl_manifest_path.read_bytes()!=sdl_manifest_bytes:raise RuntimeError('Dependency metadata changed during renderer build')
 for name,item in deps['libraries'].items():
  if digest(prefix/'lib'/name)!=item['sha256']:raise RuntimeError('Dependency archive changed during build: '+name)
 if digest(library)!=sdl['library_sha256'] or digest(prefix/'include/vk_mem_alloc.h')!=deps['vma_header_sha256']:raise RuntimeError('SDL/VMA inputs changed during build')
headers=a.headers.resolve()
if not (headers/'vulkan/vulkan.hpp').is_file():p.error('Vulkan headers missing')
header_inputs={str(path.relative_to(headers)):digest(path) for path in sorted((headers/'vulkan').rglob('*')) if path.is_file()}
def sources():
 paths=[repo/'CMakeLists.txt',repo/'gpu/CMakeLists.txt',repo/'cmake/OslPinnedSources.cmake',repo/'src/host_darwin.h',repo/'src/runtime_memory.c',repo/'src/runtime_errno.c',repo/'ios/services/memory_shim.c',repo/'ios/services/clock.h',repo/'ios/graphics/frontend-probe.cpp']
 paths += [path for directory in (repo/'gpu/shim',repo/'gpu/shadps4') for path in sorted(directory.rglob('*')) if path.is_file() and path.suffix in ('.cpp','.h','.c','.cmake','.glsl','.vert','.frag','.comp','.hlsl')]
 return {str(path.relative_to(repo)):digest(path) for path in paths}
source_inputs=sources();sdk=Path(subprocess.check_output(['xcrun','--sdk',a.sdk,'--show-sdk-path'],text=True).strip());build=root/('build-'+a.sdk)
glslang=shutil.which('glslang') or shutil.which('glslangValidator')
if not glslang:p.error('Native glslang host shader tool is required')
options=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_SYSROOT='+str(sdk),'-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=27.2',
 '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DOSL_BUILD_IOS_GPU_LIBRARY=ON',
 '-DCMAKE_PREFIX_PATH='+str(prefix)+';'+str(sdl_prefix),'-DCMAKE_FIND_ROOT_PATH='+str(prefix)+';'+str(sdl_prefix),
 '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY','-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER',
 '-DOSL_IOS_VULKAN_HEADERS='+str(headers),'-DGLSLANG_COMPILER='+glslang,'-DZLIB_LIBRARY='+str(sdk/'usr/lib/libz.tbd'),'-DZLIB_INCLUDE_DIR='+str(sdk/'usr/include')]
env=dict(os.environ,PKG_CONFIG_LIBDIR=str(prefix/'lib/pkgconfig'),PKG_CONFIG_PATH='',PKG_CONFIG_SYSROOT_DIR='')
run('configure-'+a.sdk,['cmake','-G','Ninja','-S',repo,'-B',build,*options],env)
run('build-'+a.sdk,['cmake','--build',build,'--target','oslgpu','--parallel',a.jobs],env)
if sources()!=source_inputs:raise RuntimeError('Renderer source changed during compilation; no manifest published')
archives={};expected='iossimulator' if a.sdk=='iphonesimulator' else 'ios'
for path in sorted(build.rglob('*.a')):
 if subprocess.check_output(['lipo','-archs',path],text=True).strip()!='arm64':raise RuntimeError('Archive architecture mismatch')
 members=subprocess.check_output(['ar','-t',path],text=True).splitlines();output=root/(path.name+'-'+a.sdk+'-platforms.log')
 with output.open('w') as log:subprocess.run(['xcrun','llvm-objdump','--macho','--private-headers',str(path)],stdout=log,stderr=subprocess.STDOUT,check=True)
 platforms=re.findall(r'^\s*platform\s+(\S+)',output.read_text(),re.M)
 if not platforms or len(platforms)!=sum(name.endswith('.o') for name in members) or set(platforms)!={expected}:raise RuntimeError('Archive contains a wrong/missing SDK object: '+str(path))
 archives[str(path.relative_to(build))]={'sha256':digest(path),'objects_checked':len(platforms)}
if 'gpu/liboslgpu.a' not in archives:raise RuntimeError('Renderer archive missing')
link_check=None
if a.driver_root:
 framework=a.driver_root.resolve()/'KosmicKrisp.framework';binary=framework/'KosmicKrisp'
 info=plistlib.loads((framework/'Info.plist').read_bytes());wanted='iPhoneSimulator' if a.sdk=='iphonesimulator' else 'iPhoneOS'
 if info['CFBundleSupportedPlatforms']!=[wanted]:raise RuntimeError('Consumer driver framework SDK mismatch')
 run('verify-driver-'+a.sdk,['codesign','--verify','--deep','--strict',framework])
 target='arm64-apple-ios27.2'+('-simulator' if a.sdk=='iphonesimulator' else '')
 common=['-target',target,'-isysroot',str(sdk),'-O2','-DOSL_AOT_CPU=1','-I'+str(repo/'src'),'-include',str(repo/'src/host_darwin.h')]
 objects=[]
 for name,path in (('memory',repo/'src/runtime_memory.c'),('errno',repo/'src/runtime_errno.c'),('memory-shim',repo/'ios/services/memory_shim.c')):
  obj=root/(name+'-'+a.sdk+'.o');run('link-check-compile-'+name+'-'+a.sdk,['xcrun','clang',*common,'-std=c11','-c',path,'-o',obj]);objects.append(obj)
 output=root/('renderer-link-check-'+a.sdk)
 command=['xcrun','clang++',*common,'-std=c++23','-I'+str(sdl_prefix/'include'),repo/'ios/graphics/frontend-probe.cpp',*objects,
  *[build/name for name in archives],*[prefix/'lib'/name for name in deps['libraries']],library,'-lz','-F'+str(framework.parent),'-framework','KosmicKrisp','-Wl,-rpath,@executable_path/Frameworks','-o',output]
 for name in ('UIKit','Foundation','Metal','QuartzCore','GameController','CoreHaptics','CoreMedia','CoreVideo','CoreAudio','AudioToolbox','AVFoundation','CoreBluetooth','CoreGraphics','CoreMotion'):
  command.extend(['-framework',name])
 run('consumer-link-'+a.sdk,command)
 platform_log=subprocess.check_output(['xcrun','vtool','-show-build',output],text=True)
 (root/('consumer-platform-'+a.sdk+'.log')).write_text(platform_log)
 platforms=re.findall(r'^\s*platform\s+(\S+)',platform_log,re.M)
 if platforms!=['IOSSIMULATOR' if a.sdk=='iphonesimulator' else 'IOS']:raise RuntimeError('Consumer executable has the wrong platform')
 link_check={'binary_sha256':digest(output),'driver_sha256':digest(binary),'executed':False,'installed':False}
if sources()!=source_inputs:raise RuntimeError('Source changed during consumer link; no manifest published')
audit_binary_inputs()
if {str(path.relative_to(headers)):digest(path) for path in sorted((headers/'vulkan').rglob('*')) if path.is_file()}!=header_inputs:raise RuntimeError('Vulkan headers changed during build')
manifest={'platform':a.sdk,'sdk':str(sdk),'options':options,'sources':source_inputs,'vulkan_headers':header_inputs,
 'dependency_manifest_sha256':digest(a.dependencies/('inputs-'+a.sdk+'.json')),'sdl_manifest_sha256':digest(sdl_prefix.parent/('inputs-'+a.sdk+'.json')),
 'builder_sha256':digest(Path(__file__).resolve()),'host_glslang_sha256':digest(Path(glslang)),'archives':archives,'renderer_library_compiled':True,'consumer_link_check':link_check,'driver_linked':link_check is not None,'device_executed':False,'game_frames':False}
(root/('inputs-'+a.sdk+'.json')).write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'platform':a.sdk,'archives':archives,'game_frames':False}))
