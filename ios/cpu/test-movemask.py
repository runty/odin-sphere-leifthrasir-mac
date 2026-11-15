#!/usr/bin/env python3
"""Exercise the integrated Odin MOVMSK backend against x86-64 under Rosetta.

The instruction matrix, seed vectors and native oracle originate in OpenCrown-PC
68cb2a7b, ios/cpu/review/movemask.py. This variant uses Odin's linked runtime,
checked guest mappings and the existing production lifter.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--remill', type=Path, required=True)
args = parser.parse_args()
out = args.root.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    parser.error('Output must be on a mounted external build disk')
out.mkdir(parents=True, exist_ok=False)
cpu = Path(__file__).resolve().parent
backend = args.remill.resolve()
remill = backend / 'remill'
llvm = Path('/opt/homebrew/opt/llvm/bin')
lifter = backend / 'remill-build/bin/lift/remill-lift-23'
semantics = backend / 'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc'

def run(command, log, env=None):
    with (out / log).open('wb') as stream:
        subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT,
                       env=env, check=True, timeout=180)

specs=[('legacy_ps','movmskps ecx,xmm3',32,4,'ecx'),
       ('legacy_pd','movmskpd ecx,xmm3',64,2,'ecx')]
for kind,bits,width128,width256 in [('ps',32,4,8),('pd',64,2,4)]:
    for width,lanes in [(128,width128),(256,width256)]:
        for dest in ['ecx','r11d']:
            specs.append((f'vex_{kind}{width}_{dest}',
                          f'vmovmsk{kind} {dest},{"xmm" if width==128 else "ymm"}3',bits,lanes,dest))
seeds=[]
special32=[0,0x80000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,
           0x7fc00001,0xffc00001,0x7f800001,0xff800001,1,0x80000001,
           0x7fffffff,0xffffffff,0x00800000,0x80800000]
special64=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,
           0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0xfff8000000000001,
           0x7ff0000000000001,0xfff0000000000001,1,0x8000000000000001,
           0x7fffffffffffffff,0xffffffffffffffff,0x0010000000000000,0x8010000000000000]
for mask in range(256):
    seeds.append(b''.join(struct.pack('<I',(special32[(mask+lane)%16]&0x7fffffff)|((mask>>lane&1)<<31)) for lane in range(8)))
for rotation in range(4):
    for mask in range(16):
        seeds.append(b''.join(struct.pack('<Q',(special64[(mask+lane+rotation*4)%16]&0x7fffffffffffffff)|((mask>>lane&1)<<63)) for lane in range(4)))
(out/'seeds.h').write_text('#include <cstdint>\nalignas(32) static const uint8_t seeds[][32]={' +
                         ','.join('{'+','.join(str(v) for v in seed)+'}' for seed in seeds)+'};\n')
assembly='.intel_syntax noprefix\n.text\n'
objects,declarations,calls=[],[],[]
for i,(name,instruction,_,_,_) in enumerate(specs):
    code=('vmovdqu ymm3,ymmword ptr [rdi]\nmovabs rcx,0xfedcba9876543210\n'
          'movabs r11,0xabcdef0198765432\n'+instruction+
          '\nmov qword ptr [rsi],rcx\nmov qword ptr [rsi+8],r11\n'
          'vmovdqu ymmword ptr [rsi+16],ymm3\nvzeroupper\nret\n')
    (out/f'{name}.s').write_text('.intel_syntax noprefix\n.text\n'+code)
    run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',out/f'{name}.s',
         '-o',out/f'{name}.elf.o'],name+'-assemble.log')
    run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/f'{name}.elf.o',
         out/f'{name}.bin'],name+'-extract.log')
    pc=0xc00000+i*0x1000
    run([lifter,'--os','linux','--arch','amd64_avx','--address',str(pc),
         '--bytes',(out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'],name+'-lift.log')
    lifted = (out/f'{name}.ll').read_text()
    if '__remill_sync_hyper_call' in lifted or '__remill_error(' in lifted:
        raise RuntimeError(f'{name} still requires an instruction fallback')
    run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/f'{name}.ll',
         '-o',out/f'{name}.o'],name+'-compile.log')
    objects.append(out/f'{name}.o')
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{name}",sub_{pc:x},{pc}}}')
    assembly+=f'.globl _native_{i}\n_native_{i}:\n'+code
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('#include <cstdio>\n#include "seeds.h"\n'+
    '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))+
    '\nusing Fn=void(*)(const void*,void*);\nFn calls[]={'+','.join(f'native_{i}' for i in range(len(specs)))+
    '};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[48];fn(seed,output);if(fwrite(output,1,48,stdout)!=48)return 1;}}\n')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
assert len(expected)==len(specs)*len(seeds)*48
# Verify native output layout and all possible sign masks independently, including
# full-width zero-extension and both preserved source halves.
for i,(_,_,bits,lanes,dest) in enumerate(specs):
    for j,seed in enumerate(seeds):
        start=(i*len(seeds)+j)*48
        actual=expected[start:start+48]
        mask=sum(((int.from_bytes(seed[lane*(bits//8):(lane+1)*(bits//8)],'little')>>(bits-1))&1)<<lane for lane in range(lanes))
        assert struct.unpack('<QQ',actual[:16])==((mask,0xabcdef0198765432) if dest=='ecx' else (0xfedcba9876543210,mask))
        assert actual[16:]==seed
(out/'expected.h').write_text('static const unsigned char expected[]={'+','.join(str(v) for v in expected)+'};\n')
program='''#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cstdio>
#include <cstring>
'''+ '\n'.join(declarations)+'''
struct Test{const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={'''+','.join(calls)+'''};
int main(){unsigned index=0,checks=0,failures=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;
uint8_t bytes[256]{};
mapRegion(memory, 0x1000, bytes, sizeof(bytes));
state.gpr.rdi.qword=0x1000;state.gpr.rsi.qword=0x1040;state.gpr.rsp.qword=0x10e0;
std::memcpy(bytes,seed,32);uint64_t ret=0xfeedface;std::memcpy(bytes+0xe0,&ret,sizeof(ret));
test.function(state,test.pc,&memory);
for(unsigned word=0;word<12;++word){uint32_t a,r;std::memcpy(&a,bytes+0x40+word*4,4);
std::memcpy(&r,expected+(index*'''+str(len(seeds))+'''+seed_index)*48+word*4,4);++checks;
if(a!=r){++failures;std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.data());}++seed_index;}++index;}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",index,checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++20','-O2','-I'+str(cpu),'-I'+str(remill/'include'),
     '-I'+str(out),out/'review.cpp',cpu/'runtime.cpp',*objects,'-o',out/'review'],'review-build.log')
with (out/'review-results.txt').open('w') as log:
    status=subprocess.run([str(out/'review')],stdout=log,stderr=subprocess.STDOUT).returncode
report = {
    'routines': len(specs), 'vectors': len(seeds), 'dword_checks': len(specs) * len(seeds) * 12,
    'status': status, 'oracle_sha256': hashlib.sha256(expected).hexdigest(),
    'lifter_sha256': hashlib.sha256(lifter.read_bytes()).hexdigest(),
    'semantics_sha256': hashlib.sha256(semantics.read_bytes()).hexdigest(),
}
(out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
lines = (out / 'review-results.txt').read_text().splitlines()
print(lines[-1] if lines else f'Runner exited with status {status}')
raise SystemExit(status)
