#!/usr/bin/env python3
"""Keep a real lifted trap branch while proving its normal branch still executes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from guard_ir import instrument
from trace_preparation import TracePreparationCache

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();root=a.root.resolve();source=Path(__file__).resolve().parent
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount() or root.exists():
    p.error('Use a NEW mounted external directory')
root.mkdir(parents=True)
# test edi,edi; jz trap; mov eax,0x1234; ret; trap: ud2
code=bytes.fromhex('85ff7406b834120000c30f0b')
ir=root/'owned.ll'
with (root/'lift.log').open('w') as log:
    subprocess.run([a.remill/'remill-build/bin/lift/remill-lift-23','--os','linux','--arch','amd64_avx',
                    '--address','65536','--bytes',code.hex(),'--ir_out',ir],stdout=log,stderr=subprocess.STDOUT,check=True)
raw=ir.read_text();analysis=TracePreparationCache().analyze(raw)
assert analysis.remill_error and not analysis.extended_precision
guarded,count=instrument(raw);assert '@__remill_error' in guarded and count>=1
(root/'guarded.ll').write_text(guarded)
(root/'check.cpp').write_text('''#include "runtime.h"
#include <cassert>
#include <array>
#include <memory>
#include <cstdio>
#include <cstring>
extern "C" Memory *sub_10000(State &,uint64_t,Memory *);
int main(){
 auto m=std::make_unique<Memory>();State state{};std::array<uint8_t,64> stack{};
 const uint64_t sentinel=0xfeedface;std::memcpy(stack.data()+32,&sentinel,8);
 const auto original=stack;mapRegion(*m,0x20000,stack.data(),stack.size());
 state.gpr.rdi.qword=1;state.gpr.rsp.qword=0x20020;state.gpr.rip.qword=0x10000;
 sub_10000(state,0x10000,m.get());
 assert(m->fault.empty() && m->returns==1 && state.gpr.rax.qword==0x1234);
 assert(state.gpr.rsp.qword==0x20028 && stack==original);
 state=State{};state.gpr.rax.qword=0x7777;state.gpr.rsp.qword=0x20020;state.gpr.rip.qword=0x10000;
 m->returns=0;m->checkpoints=0;std::jmp_buf recovery;m->recovery=&recovery;
 if(setjmp(recovery)==0){sub_10000(state,0x10000,m.get());assert(false);}
 m->recovery=nullptr;
 // Remill reports the post-UD2 PC. Preserve it explicitly; no exact-start-PC claim.
 assert(m->fault=="unsupported guest instruction" && m->missing_pc==0x1000c);
 assert(m->returns==0 && state.gpr.rax.qword==0x7777 && state.gpr.rsp.qword==0x20020 && stack==original);
 puts("Normal branch executes; retained UD2 stops through the enclosing checkpoint: PASS");
}
''')
for sanitizer in ('none','address','undefined'):
    flags=[] if sanitizer=='none' else ['-fsanitize='+sanitizer,'-fno-omit-frame-pointer']
    obj=root/('guarded-'+sanitizer+'.o');binary=root/('check-'+sanitizer)
    with (root/('build-'+sanitizer+'.log')).open('w') as log:
        subprocess.run(['/opt/homebrew/opt/llvm/bin/clang','-target','arm64-apple-macos15','-O2',*flags,'-c',root/'guarded.ll','-o',obj],stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(['/opt/homebrew/opt/llvm/bin/clang++','-arch','arm64','-std=c++17','-O2',*flags,
                        '-isysroot',subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip(),
                        '-I'+str(a.remill/'remill/include'),'-I'+str(source.parent/'cpu'),
                        source.parent/'cpu/runtime.cpp',root/'check.cpp',obj,'-o',binary],stdout=log,stderr=subprocess.STDOUT,check=True)
    with (root/('run-'+sanitizer+'.log')).open('w') as log:
        subprocess.run([binary],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
report={'owned_x86_bytes_sha256':hashlib.sha256(code).hexdigest(),'guards':count,
        'native_arm64_and_asan_ubsan':True,'normal_branch_returns':True,'trap_branch_stops':True,
        'backend_reported_trap_pc':'0x1000c (post-UD2)','game_executed':False,'game_frames':False}
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
