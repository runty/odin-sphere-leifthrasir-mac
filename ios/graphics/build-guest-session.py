#!/usr/bin/env python3
"""Compile the checked UIKit/guest boundary against a frozen game and renderer."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('root','game','remill','renderer','dependencies','headers'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];root=a.root.resolve();game=a.game.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external build directory')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
state_path=game/'build-state.json';state_bytes=state_path.read_bytes();state=json.loads(state_bytes)
renderer_path=a.renderer/('inputs-'+a.sdk+'.json');renderer_bytes=renderer_path.read_bytes();renderer=json.loads(renderer_bytes)
deps_path=a.dependencies/('inputs-'+a.sdk+'.json');deps_bytes=deps_path.read_bytes();deps=json.loads(deps_bytes);prefix=Path(deps['prefix'])
if renderer['platform']!=a.sdk or deps['platform']!=a.sdk:p.error('SDK metadata mismatch')
if digest(deps_path)!=renderer['dependency_manifest_sha256']:p.error('Renderer dependency metadata mismatch')
backend=state['trace_inputs']
if subprocess.check_output(['git','-C',a.remill/'remill','rev-parse','HEAD'],text=True).strip()!=backend['remill_commit']:p.error('Backend revision mismatch')
if digest(a.remill/'remill-build/bin/lift/remill-lift-23')!=backend['lifter_sha256'] or digest(a.remill/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc')!=backend['semantics_sha256']:p.error('Backend binary mismatch')
sources={name:digest(repo/name) for name in ('ios/graphics/guest-session.cpp','ios/graphics/guest-session.h','ios/graphics/guest-gpu.h','ios/graphics/gnm-shader-packets.h','ios/graphics/gnm-submissions.h','ios/services/gpu.h','ios/cpu/runtime.h','src/host_darwin.h','src/oslgpu.h')}
layout_hash=digest(game/'image-layout.h');abi_hash=digest(a.remill/'remill/include/remill/Arch/X86/Runtime/State.h')
def current():
 if state_path.read_bytes()!=state_bytes or renderer_path.read_bytes()!=renderer_bytes or deps_path.read_bytes()!=deps_bytes:raise RuntimeError('Input checkpoint changed during build')
 for inputs in (state['sources'],renderer['sources'],sources):
  for name,wanted in inputs.items():
   if digest(repo/name)!=wanted:raise RuntimeError('Source changed: '+name)
 if digest(game/'image-layout.h')!=layout_hash or digest(a.remill/'remill/include/remill/Arch/X86/Runtime/State.h')!=abi_hash:raise RuntimeError('Layout/ABI headers changed')
 for name,wanted in state['headers'].items():
  if digest(game/name)!=wanted:raise RuntimeError('Frozen header changed: '+name)
current();root.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--sdk',a.sdk,'--show-sdk-path'],text=True).strip();target='arm64-apple-ios27.2'+('-simulator' if a.sdk=='iphonesimulator' else '')
obj=root/('guest-session-'+a.sdk+'.o')
command=['xcrun','clang++','-target',target,'-isysroot',sdk,'-std=c++23','-O2','-DOSL_AOT_CPU=1','-include',repo/'src/host_darwin.h']
for include in (repo/'src',repo/'gpu/shim',repo/'gpu/shadps4',a.remill/'remill/include',game,prefix/'include',a.headers):command+=['-I'+str(include)]
command+=['-c',repo/'ios/graphics/guest-session.cpp','-o',obj]
with (root/('compile-'+a.sdk+'.log')).open('w') as log:subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,check=True)
raw=subprocess.check_output(['xcrun','llvm-objdump','--macho','--private-headers',obj],text=True)
platforms=re.findall(r'^\s*platform\s+(\S+)',raw,re.M);wanted='iossimulator' if a.sdk=='iphonesimulator' else 'ios'
if platforms!=[wanted]:raise RuntimeError('Guest frontend object SDK mismatch')
archive=root/('libodin-gpu-guest-'+a.sdk+'.a');temporary=archive.with_suffix('.new.a');temporary.unlink(missing_ok=True)
subprocess.run(['xcrun','ar','rcs',str(temporary),str(obj)],check=True);current();temporary.replace(archive)
manifest={'platform':a.sdk,'corpus_sha256':state['corpus_sha256'],'trace_inputs':backend,'sources':sources,'image_layout_sha256':layout_hash,
 'state_header_sha256':abi_hash,'renderer_manifest_sha256':digest(renderer_path),'dependency_manifest_sha256':digest(deps_path),
 'archive':str(archive),'archive_sha256':digest(archive),'object_platform':wanted,'builder_sha256':digest(Path(__file__)),
 'device_executed':False,'game_frames':False}
(root/('inputs-'+a.sdk+'.json')).write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'archive':str(archive),'platform':wanted,'game_frames':False}))
