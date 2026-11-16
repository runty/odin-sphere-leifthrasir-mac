#!/usr/bin/env python3
"""SSE binary NaN result bits: Rosetta x86-64 differential and guarded memory.

Adapted from OpenCrown a3e07c2fe69fcf2558a39748e3c2e693aebc6e4d
Private semantics/lifter builds read the shared backend without modifying it.
Uses Odin actual checked-memory runtime and setjmp recovery, not Crown memory.
Strict native NaN disagreements remain failures; width-only verdict is separate.
"""
import argparse, hashlib, json, os, re, shlex, shutil, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backends',type=Path,required=True)
p.add_argument('--reuse-backend',action='store_true')
p.add_argument('--width-backend',type=Path,required=True)
p.add_argument('--llvm',type=Path,default=Path('/opt/homebrew/opt/llvm'))
args=p.parse_args();out=args.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Output must be on a mounted external build disk')
out.mkdir(parents=True,exist_ok=True)
remill=args.backends/'remill';build=args.backends/'remill-build';llvm=args.llvm/'bin'
cpu=Path(__file__).resolve().parent;patch=cpu.parent/'remill-sse-nan-results.patch'
width_source=args.width_backend/'remill';width_bc=args.width_backend/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc'
semantics=out/'semantics';semantics.mkdir(exist_ok=True)
scratch=out/'source';data=scratch/'lib/Arch/X86/Semantics'
data.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(cpu.parent/'audit-backend.py'),'--backend',str(remill)],check=True)
shared=[cpu/'runtime.cpp',cpu/'runtime.h',Path(__file__),patch]+[remill/'lib/Arch/X86/Semantics'/n for n in ('BINARY.cpp','SSE.cpp')]+[width_source/'lib/Arch/X86/Semantics'/n for n in ('BINARY.cpp','SSE.cpp')]+[width_bc,build/'lib/Arch/X86/Runtime/amd64_avx.bc',build/'bin/lift/remill-lift-23']
hashes={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in shared}

def run(command,log,cwd=None,env=None):
    with (out/log).open('wb') as stream:
        subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,
                       cwd=cwd,env=env,check=True)


def ninja_command(target):
    text=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True)
    return shlex.split(text.splitlines()[-1])


def replace_argument(command,flag,value):
    command[command.index(flag)+1]=str(value)


if args.reuse_backend:
    meta=json.loads((out/'backend-preparation.json').read_text())
    assert meta['shared_hashes']==hashes
    assert meta['private_semantics_sha256']==hashlib.sha256((semantics/'amd64_avx.bc').read_bytes()).hexdigest()
    assert meta['private_lifter_sha256']==hashlib.sha256((out/'scalar-fp-lift').read_bytes()).hexdigest()
    check=out/'cache-source-check/lib/Arch/X86/Semantics';check.mkdir(parents=True,exist_ok=True)
    for name,digest in meta['source_files'].items():
        assert hashlib.sha256((data/name).read_bytes()).hexdigest()==digest
        shutil.copy2(data/name,check/name)
    run(['git','apply','--reverse',patch],'cache-patch-check.log',cwd=out/'cache-source-check')
    for name in meta['source_files']:
        assert (check/name).read_bytes()==(width_source/'lib/Arch/X86/Semantics'/name).read_bytes()
    print('Verified private backend cache reused',flush=True)
else:
    for filename in ('BINARY.cpp','SSE.cpp'):
        shutil.copy2(width_source/'lib/Arch/X86/Semantics'/filename,data/filename)
    check=subprocess.run(['git','apply','--check',str(patch)],cwd=scratch,capture_output=True)
    if check.returncode:
        subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=scratch,check=True)
    else:run(['git','apply',patch],'apply-patch.log',cwd=scratch)
    instructions=(remill/'lib/Arch/X86/Runtime/Instructions.cpp').read_text()
    for filename in ('BINARY.cpp','SSE.cpp'):
        instructions=instructions.replace('#include "lib/Arch/X86/Semantics/'+filename+'"',
                                          '#include '+json.dumps(str(scratch/'lib/Arch/X86/Semantics'/filename)))
    (out/'Instructions.cpp').write_text(instructions)
    command=ninja_command('lib/Arch/X86/Runtime/amd64_avx_Instructions.cpp.bc')
    command=command[command.index('&&')+1:]
    replace_argument(command,'-c',out/'Instructions.cpp')
    replace_argument(command,'-o',out/'Instructions.bc')
    run(command,'private-semantics-build.log',cwd=out)
    run([llvm/'llvm-link',out/'Instructions.bc',
         *[build/f'lib/Arch/X86/Runtime/amd64_avx_{part}.cpp.bc'
           for part in ('BasicBlock','Intrinsics','HyperCall')],
         '-o',semantics/'amd64_avx.bc'],'private-semantics-link.log')
    # The public CLI has no semantics-directory argument. Compile only a private
    # driver whose LoadArchSemantics call prefers our isolated directory; link it
    # against read-only existing Remill/LLVM libraries.
    lift=(remill/'bin/lift/Lift.cpp').read_text()
    old='remill::LoadArchSemantics(arch.get())'
    assert old in lift
    lift=lift.replace(old,'remill::LoadArchSemantics(arch.get(), {'+json.dumps(str(semantics))+'})')
    (out/'Lift.cpp').write_text(lift)
    object_target='bin/lift/CMakeFiles/remill-lift-23.dir/Lift.cpp.o'
    command=ninja_command(object_target)
    if Path(command[0]).name=='ccache': command=command[1:]
    replace_argument(command,'-c',out/'Lift.cpp')
    replace_argument(command,'-o',out/'Lift.o')
    for flag in ('-MF','-MT'):
        if flag in command: replace_argument(command,flag,out/('Lift.o.d' if flag=='-MF' else 'Lift.o'))
    run(command,'private-lifter-build.log',cwd=build)
    command=ninja_command('bin/lift/remill-lift-23')
    command=command[command.index('&&')+1:]
    command=command[:command.index('&&')]
    command=[str(out/'Lift.o') if x==object_target else x for x in command]
    replace_argument(command,'-o',out/'scalar-fp-lift')
    run(command,'private-lifter-link.log',cwd=build)


    (out/'backend-preparation.json').write_text(json.dumps(dict(shared_hashes=hashes,
        private_semantics_sha256=hashlib.sha256((semantics/'amd64_avx.bc').read_bytes()).hexdigest(),
        private_lifter_sha256=hashlib.sha256((out/'scalar-fp-lift').read_bytes()).hexdigest(),
        source_files={n:hashlib.sha256((data/n).read_bytes()).hexdigest() for n in ('BINARY.cpp','SSE.cpp')},
        shared_backend_unchanged=all(hashlib.sha256(x.read_bytes()).hexdigest()==hashes[str(x)] for x in shared)),indent=2)+'\n')
    print('Private scalar-width semantics and lifter built; shared backend unchanged',flush=True)

# The native wrappers and lifted wrappers execute identical instruction bytes.
# MOVDQU seed/capture traffic is integer traffic, distinct from counted F32/F64
# operand reads. Register/packed/previously narrow forms are positive controls.
forms=[]
for op in ('add','sub','mul','div'):
    for bits,suffix in [(32,'ss'),(64,'sd')]:
        for vex in (False,True):
            for register in (False,True):
                forms.append(dict(name=('v' if vex else '')+op+suffix+('_reg' if register else ''),
                    bits=bits,vex=vex,op=op+suffix,width=0 if register else bits//8,fixed=True,vector_bits=128))
    for bits,suffix in [(32,'ps'),(64,'pd')]:
        for vex,vector_bits in [(False,128),(True,128),(True,256)]:
            for register in (False,True):
                forms.append(dict(name=('v' if vex else '')+op+suffix+('256' if vector_bits==256 else '')+('_reg' if register else ''),
                    bits=bits,vex=vex,op=op+suffix,width=0 if register else vector_bits//8,fixed=True,vector_bits=vector_bits))
for op in ('min','max'):
    for bits,suffix in [(32,'ss'),(64,'sd')]:
        for vex in (False,True):
            for register in (False,True):
                forms.append(dict(name=('v' if vex else '')+op+suffix+('_reg' if register else ''),
                    bits=bits,vex=vex,op=op+suffix,width=0 if register else bits//8,fixed=False,vector_bits=128))
assert len(forms)==96 and len({x['name'] for x in forms})==96
values={
32:[0,0x80000000,1,0x80000001,0x00800000,0x3f800000,0x3fa00000,0xc0600000,
    0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0xff800001],
64:[0,0x8000000000000000,1,0x8000000000000001,0x0010000000000000,
    0x3ff0000000000000,0x3ff4000000000000,0xc00e000000000000,
    0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000,
    0x7ff8000000001234,0xfff8000000005678,0x7ff0000000001234,0xfff0000000000001]}
cases={}
for bits in (32,64):
    width=bits//8;vectors=[];values_for_bits=values[bits]
    for rounding in range(4):
        for offset in (0,3):
            for left in range(16):
                for right in range(16):
                    data=bytearray(128)
                    # Each register and packed-memory lane has an independent
                    # class/payload permutation. Low lanes enumerate all 16x16
                    # source pairs; upper lanes include NaNs, infinities and zeros.
                    for lane in range(32//width):
                        for base,index in [(offset,(right+lane*3)%16),
                                           (32,(left+lane*5)%16),
                                           (64,(left+lane*5)%16)]:
                            data[base+lane*width:base+(lane+1)*width]=values_for_bits[index].to_bytes(width,'little')
                    # Both legacy and VEX use the destination as source1.
                    # Register wrappers load the independent right operand
                    # into xmm0/ymm0; memory wrappers read it directly.
                    flags=0x8d5 if (left+right)&1 else 0x45
                    data[96:104]=flags.to_bytes(8,'little')
                    vectors.append(dict(rounding=rounding,offset=offset,data=data))
    cases[bits]=vectors
(out/'cases.h').write_text('#include <cstdint>\nstruct Case{unsigned rounding,offset;uint8_t data[128];};\n'+
    '\n'.join('static const Case cases'+str(bits)+'[]={'+','.join('{'+str(v['rounding'])+','+
    str(v['offset'])+',{'+','.join(map(str,v['data']))+'}}' for v in vectors)+'};'
    for bits,vectors in cases.items())+'\n')

def operation(form,offset=0):
    reg='ymm' if form['vector_bits']==256 else 'xmm'
    # For register tests xmm2/ymm2 contains lhs, xmm0/ymm0 rhs.
    # VEX aliases dst with src1, exercising preserved src2 and zero-extension.
    operand=reg+'0' if not form['width'] else (
        {4:'dword',8:'qword',16:'xmmword',32:'ymmword'}[form['width']]+f' ptr [rdi+{offset}]')
    if form['vex']:
        return 'v'+form['op']+' '+reg+'2,'+reg+'2,'+operand
    return form['op']+' '+reg+'2,'+operand

full_code={}
assembly='.intel_syntax noprefix\n.text\n'
for i,form in enumerate(forms):
    for offset in (0,3):
        register_seed='vmovdqu ymm0,ymmword ptr [rdi+'+str(offset if not form['width'] else 32)+']\n'
        code=(register_seed+'vmovdqu ymm2,ymmword ptr [rdi+64]\n'
              'mov rax,qword ptr [rdi+96]\npush rax\npopfq\n'+operation(form,offset)+
              '\npushfq\npop r8\nvmovdqu ymmword ptr [rsi],ymm0\n'
              'vmovdqu ymmword ptr [rsi+32],ymm2\nmov qword ptr [rsi+64],r8\nret\n')
        full_code[i,offset]=code
        assembly+=f'.globl _native_{i}_{offset}\n_native_{i}_{offset}:\n'+code
    assembly+=f'.globl _guard_{i}\n_guard_{i}:\nvmovdqu ymm0,ymmword ptr [rsi]\nvmovdqu ymm2,ymmword ptr [rdx]\n'+operation(form)+'\nret\n'
(out/'native.s').write_text(assembly)
native=r'''#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cfenv>
#include <iterator>
#include <csignal>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cases.h"
using Fn=void(*)(const void*,void*);
using Guard=void(*)(const void*,const void*,const void*);
'''+''.join(f'extern "C" void native_{i}_{o}(const void*,void*);\n' for i in range(len(forms)) for o in (0,3))+''.join(
    f'extern "C" void guard_{i}(const void*,const void*,const void*);\n' for i in range(len(forms)))+r'''
struct Form{Fn calls[2];Guard guard;unsigned bits,width;};
Form forms[]={'''+','.join('{{native_'+str(i)+'_0,native_'+str(i)+'_3},guard_'+str(i)+','+str(f['bits'])+','+str(f['width'])+'}' for i,f in enumerate(forms))+r'''};
const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
int main(){unsigned guards=0,negative=0;
for(auto &form:forms){
const Case *cases=form.bits==32?cases32:cases64;
for(unsigned j=0;j<std::size(cases32);++j){const auto &c=cases[j];
if(std::fesetround(rounds[c.rounding]))return 2;
uint64_t result[9]{};form.calls[c.offset?1:0](c.data,result);result[8]&=0x8d5;
if(std::fwrite(result,1,sizeof(result),stdout)!=sizeof(result))return 3;}
if(!form.width)continue;
const size_t page=size_t(sysconf(_SC_PAGESIZE));
auto *span=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));
if(span==MAP_FAILED||mprotect(span+page,page,PROT_NONE))return 4;
const auto &seed=cases[5];std::memcpy(span+page-form.width,seed.data,form.width);
form.guard(span+page-form.width,seed.data+32,seed.data+64);++guards;
const auto child=fork();if(child<0)return 5;
if(!child){signal(SIGSEGV,[](int){_exit(77);});signal(SIGBUS,[](int){_exit(77);});
form.guard(span+page-form.width+1,seed.data+32,seed.data+64);_exit(78);}
int status=0;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status)!=77)return 6;
++negative;munmap(span,page*2);}
FILE *report=std::fopen("native-guards.json","w");if(!report)return 7;
std::fprintf(report,"{\"valid_guards\":%u,\"one_byte_faults\":%u}\n",guards,negative);std::fclose(report);
}'''
(out/'native.cpp').write_text(native)
run(['xcrun','clang++','-arch','x86_64','-std=c++17',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',cwd=out,env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
assert len(expected)==len(forms)*len(cases[32])*72
(out/'expected.h').write_text('static const unsigned char expected[]={'+','.join(map(str,expected))+'};\n')
print('Native x86 vectors and real operand-end guards captured',flush=True)

# Use an explicitly selected baseline bitcode directory too: the shared CLI's
# compiled-in search path is not evidence that it loaded the intended baseline.
baseline_semantics=out/'baseline-semantics';baseline_semantics.mkdir(exist_ok=True)
shutil.copy2(width_bc,baseline_semantics/'amd64_avx.bc')
baseline_driver=(remill/'bin/lift/Lift.cpp').read_text().replace(
    'remill::LoadArchSemantics(arch.get())',
    'remill::LoadArchSemantics(arch.get(), {'+json.dumps(str(baseline_semantics))+'})')
(out/'BaselineLift.cpp').write_text(baseline_driver)
object_target='bin/lift/CMakeFiles/remill-lift-23.dir/Lift.cpp.o'
command=ninja_command(object_target)
if Path(command[0]).name=='ccache':command=command[1:]
replace_argument(command,'-c',out/'BaselineLift.cpp');replace_argument(command,'-o',out/'BaselineLift.o')
for flag in ('-MF','-MT'):
    if flag in command:replace_argument(command,flag,out/('BaselineLift.o.d' if flag=='-MF' else 'BaselineLift.o'))
run(command,'baseline-driver-build.log',cwd=build)
command=ninja_command('bin/lift/remill-lift-23')
command=command[command.index('&&')+1:];command=command[:command.index('&&')]
command=[str(out/'BaselineLift.o') if x==object_target else x for x in command]
replace_argument(command,'-o',out/'baseline-lift');run(command,'baseline-driver-link.log',cwd=build)

report=dict(runtime="Odin checked runtime with mapped fixture and real longjmp recovery",runtime_sha256=hashlib.sha256((cpu/"runtime.cpp").read_bytes()).hexdigest(),crown_reference="a3e07c2fe69fcf2558a39748e3c2e693aebc6e4d",forms=forms,vectors_per_form=len(cases[32]),native_guards=json.loads((out/'native-guards.json').read_text()),
    native_oracle_sha256=hashlib.sha256(expected).hexdigest(),patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
    shared_inputs=hashes,runs={})
for backend in ('baseline','fixed'):
    folder=out/backend;folder.mkdir(exist_ok=True)
    lifter=out/('baseline-lift' if backend=='baseline' else 'scalar-fp-lift')
    declarations=[];tests=[];raw=[];objects={m:[] for m in ('native','asan-ubsan')}
    def compile_trace(name,code,pc):
        source=folder/(name+'.s');source.write_text('.intel_syntax noprefix\n.text\n'+code)
        elf=folder/(name+'.elf.o');binary=folder/(name+'.bin');ir=folder/(name+'.ll')
        run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',source,'-o',elf],backend+'/'+name+'-assemble.log')
        run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',elf,binary],backend+'/'+name+'-extract.log')
        run([lifter,'--os','linux','--arch','amd64_avx','--address',str(pc),'--bytes',binary.read_bytes().hex(),'--ir_out',ir],backend+'/'+name+'-lift.log')
        # Use the real game builder's fault-unwind policy, including for checked
        # boundary probes with stopOnFault enabled.
        lifted=ir.read_text().replace(" nounwind", "")
        for group in re.findall(r"^attributes #(\d+) = \{\s*\}$",lifted,re.M):
            lifted=re.sub(r"^attributes #"+group+r" = \{\s*\}\n","",lifted,flags=re.M)
            lifted=re.sub(r" #"+group+r"\b","",lifted)
        lifted=re.sub(r"^(define .*?) \{$",r"\1 uwtable {",lifted,flags=re.M)
        ir.write_text(lifted)
        for mode in objects:
            flags=[] if mode=='native' else ['-fsanitize=address,undefined','-g']
            obj=folder/(name+'-'+mode+'.o')
            run([llvm/'clang','-target','arm64-apple-macos15','-O1','-funwind-tables',*flags,'-c',ir,'-o',obj],backend+'/'+name+'-'+mode+'-build.log')
            objects[mode].append(obj)
        declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
        return 'sub_'+format(pc,'x')
    for i,form in enumerate(forms):
        fns=[];pcs=[]
        for j,offset in enumerate((0,3)):
            pc=(0x10000000 if backend=='baseline' else 0x20000000)+i*0x10000+j*0x1000
            fns.append(compile_trace(form['name']+'_'+str(offset),full_code[i,offset],pc));pcs.append(pc)
        reads=form['width']*8//form['bits']
        tests.append('{"'+form['name']+'",{'+','.join(fns)+'},{'+','.join(map(str,pcs))+'},'+
            ','.join(map(str,[form['bits'],form['width'],reads,int(form['fixed'])]))+'}')
        if form['width']:
            pc=(0x30000000 if backend=='baseline' else 0x40000000)+i*0x1000
            fn=compile_trace('raw_'+form['name'],operation(form)+'\nret\n',pc)
            raw.append('{"'+form['name']+'",'+fn+','+','.join(map(str,[pc,form['bits'],form['width'],reads,int(form['fixed'])]))+'}')
    program=r'''#include "runtime.h"
#include "cases.h"
#include "expected.h"
#include <algorithm>
#include <cfenv>
#include <cstdio>
#include <cstring>
static unsigned reads32=0,reads64=0;
extern "C" float width_reference_read_f32(Memory*,uint64_t);
extern "C" double width_reference_read_f64(Memory*,uint64_t);
extern "C" float __remill_read_memory_f32(Memory *m,uint64_t a){++reads32;return width_reference_read_f32(m,a);}
extern "C" double __remill_read_memory_f64(Memory *m,uint64_t a){++reads64;return width_reference_read_f64(m,a);}
#include <array>
#include <csetjmp>
#include <memory>
#include <string>
extern "C" Memory *__remill_write_memory_64(Memory*,uint64_t,uint64_t);
inline void write64(Memory &m,uint64_t a,uint64_t v){__remill_write_memory_64(&m,a,v);}
struct FixtureMemory : Memory {
    static constexpr uint64_t base=0x70000000;
    std::array<uint8_t,65536> bytes{};
    FixtureMemory(){mapRegion(*this,base,bytes.data(),bytes.size());}
};
'''+ '\n'.join(declarations)+r'''
struct Test{const char *name;Lifted functions[2];uint64_t pcs[2];unsigned bits,width,reads,fixed;};
struct Raw{const char *name;Lifted function;uint64_t pc;unsigned bits,width,reads,fixed;};
Test tests[]={'''+','.join(tests)+r'''};
Raw raw[]={'''+','.join(raw)+r'''};
const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
int main(int argc,char **argv){if(argc!=2)return 2;FILE *actual=std::fopen(argv[1],"wb");if(!actual)return 3;
unsigned checks=0,failures=0,boundaries=0,negative=0,index=0;
for(auto &test:tests){const Case *cases=test.bits==32?cases32:cases64;
for(unsigned j=0;j<std::size(cases32);++j){const auto &c=cases[j];State state{};auto owner=std::make_unique<FixtureMemory>();auto &memory=*owner;
if(std::fesetround(rounds[c.rounding]))return 4;
state.x87.fxsave.cwd.flat=uint16_t(0x037f|(c.rounding<<10));state.x87.fxsave.mxcsr.flat=0x1f80|(c.rounding<<13);
state.gpr.rdi.qword=FixtureMemory::base+0x100;state.gpr.rsi.qword=FixtureMemory::base+0x300;
state.gpr.rsp.qword=FixtureMemory::base+0xff00;std::memcpy(memory.bytes.data()+0x100,c.data,128);
write64(memory,state.gpr.rsp.qword,0xfeedface);reads32=reads64=0;
test.functions[c.offset?1:0](state,test.pcs[c.offset?1:0],&memory);
uint64_t result[9];std::memcpy(result,memory.bytes.data()+0x300,72);result[8]&=0x8d5;
if(std::fwrite(result,1,sizeof(result),actual)!=sizeof(result))return 5;
const auto *wanted=expected+(index*std::size(cases32)+j)*72;
for(unsigned word=0;word<9;++word){uint64_t reference;std::memcpy(&reference,wanted+word*8,8);++checks;
if(result[word]!=reference){if(failures<20)std::printf("VALUE %s case%u word%u actual=%016llx native=%016llx\n",test.name,j,word,(unsigned long long)result[word],(unsigned long long)reference);++failures;}}
++checks;if(!memory.fault.empty()||reads32!=(test.bits==32?test.reads:0)||reads64!=(test.bits==64?test.reads:0)){
if(failures<20)std::printf("ACCESS %s case%u f32=%u f64=%u expected%u fault=%s\n",test.name,j,reads32,reads64,test.reads,std::string(memory.fault).c_str());++failures;}
++checks;if(std::memcmp(memory.bytes.data()+0x100,c.data,128)){++failures;std::puts("SOURCE MUTATION");}
}++index;}
for(auto &test:raw)for(unsigned throwing=0;throwing<2;++throwing)for(unsigned kind=0;kind<4;++kind){
State state{};auto owner=std::make_unique<FixtureMemory>();auto &memory=*owner;std::jmp_buf recovery;state.gpr.rsp.qword=FixtureMemory::base+0xff00;
write64(memory,state.gpr.rsp.qword,0xfeedface);std::memcpy(&state.vec[0],cases32[5].data+32,32);std::memcpy(&state.vec[2],cases32[5].data+64,32);
const uint64_t address=kind==0?FixtureMemory::base+memory.bytes.size()-test.width:
kind==1?FixtureMemory::base+memory.bytes.size()-test.width+1:kind==2?FixtureMemory::base-1:UINT64_MAX-1;
state.gpr.rdi.qword=address;const auto before=memory.bytes;bool thrown=false;reads32=reads64=0;
memory.recovery=throwing?&recovery:nullptr;
if(!throwing || setjmp(recovery)==0){test.function(state,test.pc,&memory);}else{thrown=true;}
memory.recovery=nullptr;
const bool should_fail=kind!=0;
++checks;++boundaries;if((!memory.fault.empty()||thrown)!=should_fail){++failures;std::printf("BOUNDARY %s throwing%u kind%u expected_fault%d got=%s\n",test.name,throwing,kind,should_fail,std::string(memory.fault).c_str());}
if(kind==0&&(!memory.fault.empty()||thrown))++negative;
++checks;if(memory.bytes!=before){++failures;std::puts("BOUNDARY SOURCE MUTATION");}
}
std::fclose(actual);std::printf("SUMMARY forms=%u vectors=%zu checks=%u boundaries=%u oversized_negative_controls=%u failures=%u\n",index,std::size(cases32),checks,boundaries,negative,failures);
return failures?1:0;}
'''
    (folder/'review.cpp').write_text(program)
    report['runs'][backend]={}
    for mode in objects:
        flags=[] if mode=='native' else ['-fsanitize=address,undefined','-g','-fno-omit-frame-pointer']
        runtime=folder/('runtime-'+mode+'.o')
        run([llvm/'clang++','-target','arm64-apple-macos15','-std=c++20','-O1',*flags,
            '-D__remill_read_memory_f32=width_reference_read_f32','-D__remill_read_memory_f64=width_reference_read_f64',
            '-I'+str(cpu),'-I'+str(remill/'include'),'-c',cpu/'runtime.cpp','-o',runtime],backend+'/'+mode+'-runtime-build.log')
        binary=folder/('review-'+mode)
        run([llvm/'clang++','-target','arm64-apple-macos15','-std=c++20','-O1',*flags,
            '-DDCP_WIDTH_BASELINE='+str(int(backend=='baseline')),'-I'+str(cpu),'-I'+str(remill/'include'),'-I'+str(out),
            folder/'review.cpp',runtime,*objects[mode],'-o',binary],backend+'/'+mode+'-review-build.log')
        result=subprocess.run([binary,folder/(mode+'-actual.bin')],capture_output=True,text=True,
            env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),timeout=120)
        (folder/(mode+'-results.txt')).write_text(result.stdout);(folder/(mode+'-stderr.txt')).write_text(result.stderr)
        report['runs'][backend][mode]=dict(exit=result.returncode,summary=result.stdout,stderr=result.stderr)
        (out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
        print(backend,mode,result.returncode,result.stdout[-500:],flush=True)
report['shared_backend_unchanged']=all(hashlib.sha256(x.read_bytes()).hexdigest()==hashes[str(x)] for x in shared)
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['shared_backend_unchanged']
# Baseline failures are negative controls, never normalized into passes.
# Only NaN result words may change; all result/flag/source/inactive bytes in the
# fixed implementation must exactly match the independently executed x86 binary.
qualification=dict(result_bits_pass=True,scope='96 binary/minmax control forms, all16x16 class pairs, four host rounding modes, two alignments. This is result bits/access/lanes/flags qualification, not guest MXCSR status/traps, DAZ/FTZ, x87, horizontal arithmetic, conversions or full CPU/game qualification.',runs={})
for mode in ('native','asan-ubsan'):
    baseline=(out/'baseline'/(mode+'-actual.bin')).read_bytes()
    fixed=(out/'fixed'/(mode+'-actual.bin')).read_bytes()
    assert len(baseline)==len(fixed)==len(expected)
    assert fixed==expected,'Fixed strict result mismatch: see full failing report'
    differences=0
    for pos in range(0,len(fixed),8):
        if baseline[pos:pos+8]==fixed[pos:pos+8]:continue
        row,word=divmod(pos//8,9);form,case=divmod(row,len(cases[32]));f=forms[form]
        assert f['fixed'] and 4<=word<8,'Changed input/flags or unchanged control'
        for offset in range(0,8,f['bits']//8):
            a=int.from_bytes(baseline[pos+offset:pos+offset+f['bits']//8],'little')
            b=int.from_bytes(fixed[pos+offset:pos+offset+f['bits']//8],'little')
            if a==b:continue
            mask=0x7fffffff if f['bits']==32 else 0x7fffffffffffffff
            exponent=0x7f800000 if f['bits']==32 else 0x7ff0000000000000
            assert a&mask>exponent and b&mask>exponent,'Non-NaN arithmetic changed'
        differences+=1
    assert differences>0,'Missing baseline negative controls'
    for backend in ('baseline','fixed'):
        row=report['runs'][backend][mode]
        assert row['exit']==(1 if backend=='baseline' else 0) and not row['stderr']
        match=re.search(r'SUMMARY forms=(\d+) vectors=(\d+) checks=(\d+) boundaries=(\d+) oversized_negative_controls=(\d+) failures=(\d+)',row['summary'])
        assert match
        wanted=len(forms)*len(cases[32])*11+len(raw)*16
        assert tuple(map(int,match.groups()))==(len(forms),len(cases[32]),wanted,len(raw)*8,0,differences if backend=='baseline' else 0)
    qualification['runs'][mode]=dict(strict_result_bytes=len(fixed),baseline_nan_negative_controls=differences,fixed_strict_result_mismatches=0)
(out/'result-qualification.json').write_text(json.dumps(qualification,indent=2)+'\n')
print(json.dumps(qualification),flush=True)
