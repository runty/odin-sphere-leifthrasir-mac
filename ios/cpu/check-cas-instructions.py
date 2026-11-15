#!/usr/bin/env python3
"""Compare actual lifted CMPXCHG instructions with native x86 under Rosetta.

No game bytes are needed. Valid readable/writable spans only: this is not an
x86 alignment-fault oracle. CMPXCHG16B uses aligned memory. Other widths include
unaligned spans. Direct intrinsic fault/concurrency tests are separate.
"""
import argparse
import hashlib
import json
import os
import platform
import struct
import sys
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backends',type=Path)
p.add_argument('--native-reference',type=Path,help='Use a source/hash-verified x86 hardware native-only output directory')
p.add_argument('--native-only',action='store_true',help='Build only the portable native x86 oracle, without Remill')
p.add_argument('--llvm',type=Path,default=Path('/opt/homebrew/opt/llvm'))
a=p.parse_args()
if a.native_only and a.native_reference:p.error('--native-only and --native-reference are mutually exclusive')
out=a.output.resolve();cpu=Path(__file__).resolve().parent
if sys.platform=='darwin' and (len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount()):p.error('Use a mounted external output directory')
if not a.native_only and not a.backends:p.error('--backends is required unless --native-only')
if sys.platform!='darwin' and not a.native_only:p.error('Lifted checks require the Apple toolchain')
if a.native_only and sys.platform!='darwin' and platform.machine() not in ('x86_64','AMD64'):p.error('Native oracle requires x86-64')
out.mkdir(parents=True,exist_ok=True)
llvm=a.llvm/'bin';lifter=a.backends/'remill-build/bin/lift/remill-lift-23' if a.backends else None
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
inputs=[Path(__file__)]
if not a.native_only:inputs += [cpu/'runtime.cpp',cpu/'runtime.h',lifter,a.backends/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc',a.backends/'remill/include/remill/Arch/X86/Runtime/State.h']
hashes={str(f):sha(f) for f in inputs}
def run(command,name,env=None):
 with (out/name).open('wb') as stream:subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,env=env,check=True,timeout=120)
values=[0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff,0x7fffffffffffffff,0x8000000000000000,0xffffffffffffffff,0x123456789abcdef0,0xfedcba9876543210]
seeds=[]
for i in range(256):
 lo=values[(i//2)%len(values)];hi=values[(i//32+5)%len(values)]
 seeds.append([lo,hi,lo^(i%2),hi,values[(i*3+7)%len(values)],values[(i*5+11)%len(values)]])
(out/'seeds.h').write_text('#include <cstdint>\nalignas(16) static const uint64_t seeds[][6]={'+','.join('{'+','.join(str(x)+'ULL' for x in seed)+'}' for seed in seeds)+'};\n')
specs=[]
for locked in (False,True):
 for bits,size,reg in ((8,'byte','bl'),(16,'word','bx'),(32,'dword','ebx'),(64,'qword','rbx'),(64,'cmpxchg8b',''),(128,'cmpxchg16b','')):
  for offset in ((0,) if bits==128 else (0,1,7)):
   name=('locked_' if locked else '')+(size if not reg else 'cmpxchg'+str(bits))+'_'+str(offset)
   instruction=(('lock ' if locked else '')+(f'cmpxchg {size} ptr [rsi+{16+offset}],{reg}' if reg else f'{size} '+('xmmword' if bits==128 else 'qword')+f' ptr [rsi+{16+offset}]'))
   specs.append((name,instruction,offset,not reg,bits))
objects=[];declarations=[];calls=[];assembly='.intel_syntax noprefix\n.text\n'
for index,(name,instruction,offset,wide,bits) in enumerate(specs):
 loads='mov rax,qword ptr [rdi+16]\nmov rdx,qword ptr [rdi+24]\nmov rbx,qword ptr [rdi+32]\nmov rcx,qword ptr [rdi+40]\n'
 if wide and bits==64:loads='mov rax,qword ptr [rdi+16]\nmov rdx,rax\nshr rdx,32\nmov ebx,dword ptr [rdi+32]\nmov rcx,qword ptr [rdi+32]\nshr rcx,32\n'
 code='push rbx\n'+loads+'xor r8,r8\nsub r8,1\n'+instruction+'\nmov qword ptr [rsi+64],rax\nmov qword ptr [rsi+72],rdx\nmov qword ptr [rsi+80],rbx\nmov qword ptr [rsi+88],rcx\npushfq\npop r8\nand r8,'+str(0x40 if wide else 0x8d5)+'\nmov qword ptr [rsi+96],r8\npop rbx\nret\n'
 asm=out/(name+'.s');asm.write_text('.intel_syntax noprefix\n.text\n'+code)
 if not a.native_only:
  run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',asm,'-o',out/(name+'.elf.o')],name+'-assemble.log')
  run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/(name+'.elf.o'),out/(name+'.bin')],name+'-extract.log')
  pc=0xd00000+index*0x1000
  run([lifter,'--os','linux','--arch','amd64_avx','--address',str(pc),'--bytes',(out/(name+'.bin')).read_bytes().hex(),'--ir_out',out/(name+'.ll')],name+'-lift.log')
  run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/(name+'.ll'),'-o',out/(name+'.o')],name+'-compile.log')
  objects.append(out/(name+'.o'));declarations.append(f'extern "C" Memory *sub_{pc:x}(State &,uint64_t,Memory *);');calls.append('{"'+name+'",sub_'+format(pc,'x')+','+str(pc)+','+str(offset)+'}')
 symbol=('_' if sys.platform=='darwin' else '')+'native_'+str(index)
 assembly+=f'.globl {symbol}\n{symbol}:\n'+code
if sys.platform!='darwin':assembly+='\n.section .note.GNU-stack,"",@progbits\n'
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('#include <cstdio>\n#include <cstring>\n#include "seeds.h"\n'+''.join(f'extern "C" void native_{i}(const void *,void *);\n' for i in range(len(specs)))+'using Fn=void(*)(const void*,void*);\nstruct Case{Fn fn;unsigned offset;};\nstatic Case tests[]={'+','.join('{native_'+str(i)+','+str(spec[2])+'}' for i,spec in enumerate(specs))+'};\nint main(){for(auto test:tests)for(auto &seed:seeds){alignas(16) unsigned char output[128];memset(output,0x5a,sizeof(output));memcpy(output+16+test.offset,seed,16);test.fn(seed,output);if(fwrite(output,1,sizeof(output),stdout)!=sizeof(output))return 1;}}\n')
compiler=['xcrun','clang++','-arch','x86_64'] if sys.platform=='darwin' else ['c++']
run([*compiler,'-O2',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
assert (out/'native-results.bin').stat().st_size==len(specs)*len(seeds)*128
if a.native_only:
 reference=(out/'native-results.bin').read_bytes();rows=[];flag_mismatches=0
 for index,(name,instruction,offset,wide,bits) in enumerate(specs):
  mismatches=0;samples=[]
  for number,seed in enumerate(seeds):
   fields=struct.unpack_from('<5Q',reference,(index*256+number)*128+64)
   if not wide:
    mask=(1<<bits)-1;lhs=seed[2]&mask;rhs=seed[0]&mask;value=(lhs-rhs)&mask
    flags=int(lhs<rhs) | ((bin(value&255).count('1')%2==0)<<2) | (((lhs^rhs^value)&16)) | ((value==0)<<6) | (((value>>(bits-1))&1)<<7) | ((((lhs^rhs)&(lhs^value))>>(bits-1)&1)<<11)
    mismatches+=fields[4]!=flags
   if number in (24,25,26,27):samples.append({'seed':number,'memory_low':hex(seed[0]),'expected_low':hex(seed[2]),'rax':hex(fields[0]),'rdx':hex(fields[1]),'flags':hex(fields[4])})
  flag_mismatches+=mismatches;rows.append({'name':name,'cmp_subtraction_flag_mismatches':mismatches,'samples':samples})
 cpu_info=None
 if Path('/proc/cpuinfo').exists():
  cpu_info=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),None)
 report={'scope':'native x86 observation; flags checked against accumulator-minus-old-memory arithmetic; register policy samples retained','architecture':platform.machine(),'system':platform.system(),'cpu':cpu_info,'source_sha256':hashes[str(Path(__file__))],'reference_sha256':sha(out/'native-results.bin'),'routines':len(specs),'seeds':len(seeds),'cmp_subtraction_flag_mismatches':flag_mismatches,'cases':rows}
 assert all(sha(Path(name))==wanted for name,wanted in hashes.items())
 (out/'native-observations.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='cases'}));raise SystemExit(0)
reference_path=out/'native-results.bin'
if a.native_reference:
 observed=json.loads((a.native_reference/'native-observations.json').read_text());external=a.native_reference/'native-results.bin'
 if observed['architecture'] not in ('x86_64','AMD64') or observed['source_sha256']!=sha(Path(__file__)) or observed['reference_sha256']!=sha(external):
  p.error('Hardware oracle architecture/source/reference identity mismatch')
 if external.stat().st_size!=len(specs)*len(seeds)*128:p.error('Hardware oracle result length mismatch')
 reference_path=out/'hardware-reference.bin';reference_path.write_bytes(external.read_bytes())
program='''#include "runtime.h"
#include "seeds.h"
#include <cstdio>
#include <cstring>
'''+ '\n'.join(declarations)+'''
struct Test{const char *name;Lifted fn;uint64_t pc;unsigned offset;};
static const Test tests[]={'''+','.join(calls)+'''};
int main(int argc,char **argv){if(argc!=2)return 2;FILE *reference=fopen(argv[1],"rb");if(!reference)return 2;
unsigned checks=0,failed=0;for(auto test:tests){unsigned seedIndex=0;for(auto &seed:seeds){
auto owner=std::make_unique<State>();State &state=*owner;Memory memory;std::vector<uint8_t> backing(65536);mapRegion(memory,0x100000,backing.data(),backing.size());
state.gpr.rdi.qword=0x100100;state.gpr.rsi.qword=0x100300;state.gpr.rsp.qword=0x10ff00;state.gpr.rbx.qword=0x13579bdf2468ace0;
memcpy(backing.data()+0x100,seed,sizeof(seed));auto output=backing.data()+0x300;memset(output,0x5a,128);memcpy(output+16+test.offset,seed,16);
uint64_t sentinel=0xfeedface;memcpy(backing.data()+0xff00,&sentinel,8);std::jmp_buf recovery;memory.recovery=&recovery;
if(!setjmp(recovery))test.fn(state,test.pc,&memory);memory.recovery=nullptr;uint8_t expected[128];if(fread(expected,1,128,reference)!=128)return 2;
for(unsigned byte=0;byte<128;++byte){++checks;if(output[byte]!=expected[byte]){if(failed++<16)printf("FAIL %s seed%u byte%u actual=%02x reference=%02x\\n",test.name,seedIndex,byte,output[byte],expected[byte]);}}
++checks;if(!memory.fault.empty() || state.gpr.rbx.qword!=0x13579bdf2468ace0 || state.gpr.rsp.qword!=0x10ff08){if(failed++<16)printf("FAULT %s seed%u %s\\n",test.name,seedIndex,std::string(memory.fault).c_str());}
++seedIndex;}}if(fgetc(reference)!=EOF)return 2;fclose(reference);
printf("{\\"checks\\":%u,\\"failed\\":%u,\\"routines\\":%zu,\\"seeds\\":256,\\"game_executed\\":false}\\n",checks,failed,sizeof(tests)/sizeof(tests[0]));return failed?1:0;}
'''
(out/'check.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-I'+str(a.backends/'remill/include'),'-I'+str(cpu),'-I'+str(out),out/'check.cpp',cpu/'runtime.cpp',*objects,'-o',out/'check'],'check-build.log')
run([out/'check',reference_path],'results.log')
result=json.loads((out/'results.log').read_text().splitlines()[-1]);assert result['failed']==0
assert all(sha(Path(name))==wanted for name,wanted in hashes.items()),'Qualification inputs changed'
report={'passed':True,'scope':'valid-span locked/unlocked CMPXCHG8/16/32/64, CMPXCHG8B/16B versus native x86; no full alignment-fault or game claim','result':result,'inputs':hashes,'reference_sha256':sha(reference_path),'reference_kind':'hardware' if a.native_reference else 'Rosetta','routines':[x[0] for x in specs]}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(result))
