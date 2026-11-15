#!/usr/bin/env python3
"""Compare lifted startup SIMD to native x86-64 execution under Rosetta.

Use an audited, built backend in --backends and an isolated external --output.
The source, lifter and semantics must remain unchanged during the check. This
checks instruction semantics using Odin's checked-memory runtime; it does not
establish guest reachability, visible frames, gameplay performance or device
execution. No game files are needed. An optional private 84-byte matrix-extract
span can add the exact observed twelve-store case without publishing game bytes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--backends', type=Path, required=True)
parser.add_argument('--observed-matrix-span', type=Path)
parser.add_argument('--llvm', type=Path, default=Path('/opt/homebrew/opt/llvm'))
args = parser.parse_args()
out = args.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    parser.error('Output must be on a mounted external build disk')
out.mkdir(parents=True,exist_ok=True)
remill=args.backends/'remill'
build=args.backends/'remill-build'
llvm=args.llvm/'bin'
cpu=Path(__file__).resolve().parent
patches=[cpu.parent/('remill-'+name+'.patch') for name in ('startup-lane-insert','avx-extract','startup-variable-blend','startup-permute128','startup-dword-shifts')]
files=['SSE.cpp','MMX.cpp','AVX.cpp']
shared=[*[remill/('lib/Arch/X86/Semantics/'+name) for name in files],
        build/'lib/Arch/X86/Runtime/amd64_avx.bc',build/'bin/lift/remill-lift-23']
hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in shared}


def run(command,log,cwd=None,env=None):
    with (out/log).open('wb') as stream:
        subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,
                       cwd=cwd,env=env,check=True)



# Each output contains complete YMM0+YMM1, the full R11 GPR and 32 bytes
# covering memory stores and guards. EXTRACTF128 stores all sixteen bytes in
# that region, followed by sixteen bytes that must remain untouched. This catches VEX upper-zeroing, source destruction, GPR truncation
# and out-of-width EXTRACTPS stores independently of selected payload values.
specs=[]
for kind in ['ps','pd']:
    for width,reg,mem in [(128,'xmm','xmmword'),(256,'ymm','ymmword')]:
        specs.extend([
            ('blend',f'blendv{kind}{width}_reg',f'vblendv{kind} {reg}0,{reg}1,{reg}2,{reg}3'),
            ('blend',f'blendv{kind}{width}_mem',f'vblendv{kind} {reg}0,{reg}1,{mem} ptr [rdi+64],{reg}3'),
            ('blend',f'blendv{kind}{width}_mask_alias',f'vblendv{kind} {reg}0,{reg}1,{reg}2,{reg}0'),
            ('blend',f'blendv{kind}{width}_source_alias',f'vblendv{kind} {reg}0,{reg}0,{reg}2,{reg}3'),
        ])
for mnemonic,controls,mem in [
        ('vpinsrb',list(range(16))+[16,127,128,255],'byte'),
        ('vpinsrd',[0,1,2,3,4,15,128,255],'dword')]:
    for control in controls:
        specs.extend([
            ('lanes',f'{mnemonic}_reg_{control}',f'{mnemonic} xmm0,xmm1,r11d,{control}'),
            ('lanes',f'{mnemonic}_mem_{control}',f'{mnemonic} xmm0,xmm1,{mem} ptr [rdi+129],{control}'),
        ])
    specs.append(('lanes',mnemonic+'_source_alias',f'{mnemonic} xmm0,xmm0,r11d,2'))
for control in [0,1,2,3,4,15,128,255]:
    specs.extend([
        ('lanes',f'extractps_reg_{control}',f'vextractps r11d,xmm1,{control}'),
        ('lanes',f'extractps_mem_{control}',f'vextractps dword ptr [rsi+76],xmm1,{control}'),
    ])
controls=[(high<<4)|low for high in range(4) for low in range(4)]
controls += [(high<<4)|8 for high in range(4)]+[0x80|low for low in range(4)]+[0x88,0x44,0x77,0xcc,0xff]
for control in controls:
    specs.extend([
        ('permute',f'perm2f128_reg_{control}',f'vperm2f128 ymm0,ymm1,ymm2,{control}'),
        ('permute',f'perm2f128_mem_{control}',f'vperm2f128 ymm0,ymm1,ymmword ptr [rdi+64],{control}'),
    ])
specs.extend([
    ('permute','perm2f128_first_alias','vperm2f128 ymm0,ymm0,ymm2,33'),
    ('permute','perm2f128_second_alias','vperm2f128 ymm0,ymm1,ymm0,50'),
])
for mnemonic in ['pslld','psrld','psrad']:
    for count in [0,1,31,32,255]:
        specs.extend([
            ('shifts',f'{mnemonic}_legacy_imm{count}',f'{mnemonic} xmm0,{count}'),
            ('shifts',f'{mnemonic}_vex128_imm{count}',f'v{mnemonic} xmm0,xmm1,{count}'),
            ('shifts',f'{mnemonic}_vex256_imm{count}',f'v{mnemonic} ymm0,ymm1,{count}'),
        ])
    specs.extend([
        ('shifts',f'{mnemonic}_legacy_count_reg',f'{mnemonic} xmm0,xmm2'),
        ('shifts',f'{mnemonic}_legacy_count_mem',f'{mnemonic} xmm0,xmmword ptr [rdi+64]'),
        ('shifts',f'{mnemonic}_vex128_count_reg',f'v{mnemonic} xmm0,xmm1,xmm2'),
        ('shifts',f'{mnemonic}_vex128_count_mem',f'v{mnemonic} xmm0,xmm1,xmmword ptr [rdi+64]'),
        ('shifts',f'{mnemonic}_vex256_count_reg',f'v{mnemonic} ymm0,ymm1,xmm2'),
        ('shifts',f'{mnemonic}_vex256_count_mem',f'v{mnemonic} ymm0,ymm1,xmmword ptr [rdi+64]'),
    ])
for control in [0,1,2,3,4,15,128,255]:
    specs.append(('lanes',f'extractps_unaligned_{control}',f'vextractps dword ptr [rsi+75],xmm1,{control}'))
    specs.append(('extract128',f'extract128_reg_{control}',f'vextractf128 xmm0,ymm1,{control}'))
    specs.append(('extract128',f'extract128_mem_{control}',f'vextractf128 xmmword ptr [rsi+72],ymm1,{control}'))
specs.append(('extract128','extract128_alias','vextractf128 xmm0,ymm0,1'))
if args.observed_matrix_span:
    observed=args.observed_matrix_span.read_bytes()
    if len(observed)!=84:
        parser.error('The private matrix-extract span must contain exactly 84 bytes')
    specs.append(('observed','matrix-extracts-original','mov rax,rsi\n.byte '+
        ','.join(map(str,observed))+'\nvmovdqu ymm0,ymmword ptr [rsi+16]'+
        '\nvmovdqu xmm1,xmmword ptr [rsi+48]'))

values=[0,1,0x7fffffff,0x80000000,0xffffffff,0x3f800000,0xbf800000,0x7f800000,
        0xff800000,0x7fc00001,0xffc00001,0x7f800001,0xff800001,0x00800000,0x80800000,0x13579bdf]
counts=[0,1,30,31,32,255,256,1<<32,1<<63,(1<<64)-1]
seeds=[]
for index in range(256):
    seed=bytearray()
    for reg in range(3):
        for lane in range(8):
            seed+=struct.pack('<I',values[(index+lane+reg*5)%len(values)])
    seed[64:72]=struct.pack('<Q',counts[index%len(counts)])
    seed[72:80]=struct.pack('<Q',0xfedcba9876543210^index)
    for lane in range(8):
        seed+=struct.pack('<I',(values[(index*3+lane)%16]&0x7fffffff)|((index>>lane&1)<<31))
    seed+=struct.pack('<Q',0xfedcba9881fe0000+index*0x010203)
    seed+=bytes([0x5a]*8)
    assert len(seed)==144
    seeds.append(seed)
(out/'seeds.h').write_text('#include <cstdint>\nalignas(32) static const uint8_t seeds[][144]={'+
    ','.join('{'+','.join(str(v) for v in seed)+'}' for seed in seeds)+'};\n')
assembly='.intel_syntax noprefix\n.text\n'
objects,declarations,calls=[],[],[]
for i,(group,name,instruction) in enumerate(specs):
    code=('vmovdqu ymm0,ymmword ptr [rdi]\nvmovdqu ymm1,ymmword ptr [rdi+32]\n'
          'vmovdqu ymm2,ymmword ptr [rdi+64]\nvmovdqu ymm3,ymmword ptr [rdi+96]\n'
          'mov r11,qword ptr [rdi+128]\nmovabs rax,0xa5a5a5a5a5a5a5a5\n'
          'mov qword ptr [rsi+72],rax\nmov qword ptr [rsi+80],rax\nmov qword ptr [rsi+88],rax\nmov qword ptr [rsi+96],rax\n'+instruction+
          '\nvmovdqu ymmword ptr [rsi],ymm0\nvmovdqu ymmword ptr [rsi+32],ymm1\n'
          'mov qword ptr [rsi+64],r11\nvzeroupper\nret\n')
    (out/f'{name}.s').write_text('.intel_syntax noprefix\n.text\n'+code)
    run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',out/f'{name}.s','-o',out/f'{name}.elf.o'],name+'-assemble.log')
    run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/f'{name}.elf.o',out/f'{name}.bin'],name+'-extract.log')
    pc=0xd00000+i*0x1000
    run([build/'bin/lift/remill-lift-23','--os','linux','--arch','amd64_avx','--address',str(pc),
         '--bytes',(out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'],name+'-lift.log')
    run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/f'{name}.ll','-o',out/f'{name}.o'],name+'-compile.log')
    objects.append(out/f'{name}.o')
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{group}","{name}",sub_{pc:x},{pc}}}')
    assembly+=f'.globl _native_{i}\n_native_{i}:\n'+code
    if i+1==len(specs) or specs[i+1][0]!=group:print(f'Prepared {group} lifted routines',flush=True)
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('#include <cstdio>\n#include "seeds.h"\n'+
    '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))+
    '\nusing Fn=void(*)(const void*,void*);\nFn calls[]={'+','.join(f'native_{i}' for i in range(len(specs)))+
    '};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[104];fn(seed,output);if(fwrite(output,1,104,stdout)!=104)return 1;}}\n')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
assert len(expected)==len(specs)*len(seeds)*104
(out/'expected.h').write_text('static const unsigned char expected[]={'+','.join(str(v) for v in expected)+'};\n')
program='''#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cstdio>
#include <cstring>
'''+ '\n'.join(declarations)+'''
struct Test{const char *group;const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={'''+','.join(calls)+'''};
int main(){unsigned index=0,checks=0,failures=0;unsigned group_checks=0,group_failures=0,group_routines=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){auto state_owner=std::make_unique<State>();State &state=*state_owner;auto memory_owner=std::make_unique<Memory>();Memory &memory=*memory_owner;std::vector<uint8_t> backing(65536);mapRegion(memory,0x100000,backing.data(),backing.size());
state.gpr.rdi.qword=0x100000+0x100;state.gpr.rsi.qword=0x100000+0x300;state.gpr.rsp.qword=0x100000+0xff00;
std::memcpy(backing.data()+0x100,seed,sizeof(seed));uint64_t sentinel=0xfeedface;std::memcpy(backing.data()+0xff00,&sentinel,8);
std::jmp_buf checkpoint;memory.recovery=&checkpoint;if(!setjmp(checkpoint))test.function(state,test.pc,&memory);memory.recovery=nullptr;
for(unsigned word=0;word<26;++word){uint32_t a,r;std::memcpy(&a,backing.data()+0x300+word*4,4);
std::memcpy(&r,expected+(index*'''+str(len(seeds))+'''+seed_index)*104+word*4,4);++checks;++group_checks;
if(a!=r){++failures;++group_failures;if(failures<=16)std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;++group_failures;if(seed_index==0)std::printf("FAULT %s %s\\n",test.name,std::string(memory.fault).c_str());}++seed_index;}
++index;++group_routines;
if(index==sizeof(tests)/sizeof(tests[0]) || std::strcmp(tests[index].group,test.group)){
std::printf("GROUP %s routines=%u checks=%u failures=%u\\n",test.group,group_routines,group_checks,group_failures);std::fflush(stdout);group_routines=group_checks=group_failures=0;}}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",index,checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++20','-O2','-I'+str(cpu),'-I'+str(remill/'include'),
     '-I'+str(out),out/'review.cpp',cpu/'runtime.cpp',*objects,'-o',out/'review'],'review-build.log')
with (out/'review-results.txt').open('w') as log:
    status=subprocess.run([str(out/'review')],stdout=log,stderr=subprocess.STDOUT).returncode
unchanged=all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in shared)
report={'routines':len(specs),'vectors':len(seeds),'dword_checks':len(specs)*len(seeds)*26,
        'status':status,'shared_backend_inputs_unchanged':unchanged,
        'oracle_sha256':hashlib.sha256(expected).hexdigest(),
        'backend_inputs_sha256':hashes,
        'runtime_sha256':hashlib.sha256((cpu/'runtime.cpp').read_bytes()).hexdigest(),
        'patch_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches}}
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'specs.json').write_text(json.dumps(specs,indent=2)+'\n')
print('\n'.join(line for line in (out/'review-results.txt').read_text().splitlines() if line.startswith(('GROUP ','SUMMARY '))),flush=True)
assert unchanged,'Shared backend changed during isolated test'
raise SystemExit(status)
