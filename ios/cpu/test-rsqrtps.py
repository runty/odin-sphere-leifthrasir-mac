#!/usr/bin/env python3
"""Qualify (V)RSQRTPS forms against native x86 and independent ISA contracts."""
from pathlib import Path
import subprocess,os,hashlib,json
import argparse
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();r=a.remill.resolve();out=a.root.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Use a mounted external output directory')
if out.exists():p.error('Use a new qualification directory')
out.mkdir(parents=True)
if subprocess.check_output(['git','-C',str(r/'remill'),'rev-parse','HEAD'],text=True).strip()!='66c488c9911fd9b368e4d0c9647d74239f8bba8b':p.error('Unexpected backend revision')
src=Path(__file__).resolve().parent;llvm=Path('/opt/homebrew/opt/llvm/bin')
def run(label,args,env=None):
 with (out/(label+'.log')).open('w') as log:
  subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,env=env,check=True,timeout=120)
codes=[]
for op in ['440f52c8','440f520f','c57852c8','c578520f','c57c52c8','c57c520f']:
 codes.append('c5fe6f07c57e6f0a'+op+'c57e7f0ec3')
decls=[]
for i,code in enumerate(codes):
 address=0x100000+i*0x10000;p=out/f'form-{i}.ll'
 run(f'lift-{i}',[r/'remill-build/bin/lift/remill-lift-23','--os','linux','--arch','amd64_avx','--address',address,'--bytes',code,'--ir_out',p])
 text=p.read_text()
 if '__remill_sync_hyper_call' in text or '__remill_error(' in text:raise RuntimeError(f'Unimplemented selector in form {i}')
 run(f'compile-{i}',[llvm/'clang','-target','arm64-apple-macos15','-O2','-c',p,'-o',out/f'form-{i}.o'])
 decls.append(f'extern "C" Memory *sub_{address:x}(State &,uint64_t,Memory *);')
(out/'codes.h').write_text('static const char*codes[]={'+','.join(json.dumps(c) for c in codes)+'};\n')
common=r'''
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <limits>
static std::vector<uint32_t> inputs(){
 std::vector<uint32_t> v;
 for(unsigned e=0;e<256;++e)for(uint32_t f:{0u,1u,0x3fffffu,0x400000u,0x7ffffeu,0x7fffffu})for(unsigned sign=0;sign<2;++sign)v.push_back((sign<<31)|(e<<23)|f);
 uint32_t rng=0x83195371u;
 while(v.size()<8192){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;v.push_back(rng);}
 return v;
}
static void verify(const uint32_t *in,const uint32_t *out,unsigned kind){
 unsigned lanes=kind<4?4:8;
 for(unsigned i=0;i<8;++i){
  uint32_t a=in[i],b=out[i],sign=a&0x80000000u,exp=(a>>23)&255u,frac=a&0x7fffffu;
  bool ok=true;
  if(i>=lanes)ok=b==(kind<2?0xa5a5a5a5u:0u);
  else if(!exp)ok=b==(sign|0x7f800000u);
  else if(exp==255u && frac)ok=b==(a|0x400000u);
  else if(sign)ok=b==0xffc00000u;
  else if(exp==255u)ok=b==0;
  else {
   float x,y;memcpy(&x,&a,4);memcpy(&y,&b,4);double expected=1.0/std::sqrt(double(x));
   const double bound=1.5/4096.0;
   ok=!(b&0x80000000u) && std::isnormal(y) && std::abs((double(y)-expected)/expected)<=bound;
  }
  if(!ok){fprintf(stderr,"kind %u lane %u in %08x out %08x violates ISA bound/special/lane contract\n",kind,i,a,b);abort();}
 }
}
static uint32_t control(unsigned mode){return 0x1f80u|((mode&3)<<13)|((mode&4)?0x40:0)|((mode&8)?0x8000:0)|((mode&16)?0x15:0);}
'''
(out/'invoke.s').write_text('''.text
.globl _invoke
_invoke:
subq $8,%rsp
movq %rdi,%r8
movq %rsi,%rdi
movq %rdx,%rsi
movq %rcx,%rdx
pushq $0xad7
popfq
callq *%r8
pushfq
popq %rax
leaq 8(%rsp),%rsp
retq
''')
(out/'oracle.cpp').write_text(common+r'''
#include "codes.h"
#include <sys/mman.h>
#include <xmmintrin.h>
extern "C" uint64_t invoke(void *,const void *,void *,const void *);
int main(){auto in=inputs();uint32_t canary[8];for(auto &x:canary)x=0xa5a5a5a5;
 for(unsigned kind=0;kind<6;++kind){
  auto n=strlen(codes[kind])/2;auto p=(uint8_t*)mmap(nullptr,n,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);if(p==MAP_FAILED)return 2;
  for(size_t i=0;i<n;++i){unsigned b;sscanf(codes[kind]+i*2,"%2x",&b);p[i]=b;}
  std::vector<uint32_t> baseline(in.size());
  for(unsigned mode=0;mode<32;++mode)for(size_t i=0;i<in.size();i+=8){uint32_t out[8];auto wanted=control(mode);_mm_setcsr(wanted);auto flags=invoke(p,in.data()+i,out,canary);auto csr=_mm_getcsr();_mm_setcsr(0x1f80);
   if(csr!=wanted || (flags&0x8d5)!=0x8d5)return 3;
   verify(in.data()+i,out,kind);
   if(!mode)memcpy(baseline.data()+i,out,32);else if(memcmp(baseline.data()+i,out,32))return 4;
  }munmap(p,n);
 }
 puts("native x86: 6 forms, 8192 input bits, 32 MXCSR modes/status seeds, ISA bounds and special cases; flags and upper lanes pass");}
''')
run('oracle-build',['xcrun','clang++','-arch','x86_64','-std=c++17','-O2',out/'oracle.cpp',out/'invoke.s','-o',out/'oracle'])
run('oracle',[out/'oracle'],dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
(out/'check.cpp').write_text(common+'\n#include "runtime.h"\n'+'\n'.join(decls)+'\nstatic Lifted functions[]={'+','.join(f'sub_{0x100000+i*0x10000:x}' for i in range(6))+'};\n'+r'''
int main(){auto in=inputs();uint32_t canary[8];for(auto &x:canary)x=0xa5a5a5a5;
 for(unsigned kind=0;kind<6;++kind){std::vector<uint32_t> baseline(in.size());
  for(unsigned mode=0;mode<32;++mode)for(size_t i=0;i<in.size();i+=8){uint32_t out[8]{};uint64_t stack[4]={0xfeedface};State s{};Memory m;
   mapRegion(m,0x200000,(uint8_t*)(in.data()+i),32,false);mapRegion(m,0x300000,(uint8_t*)out,32);mapRegion(m,0x400000,(uint8_t*)canary,32,false);mapRegion(m,0x500000,(uint8_t*)stack,sizeof(stack));
   s.gpr.rdi.qword=0x200000;s.gpr.rsi.qword=0x300000;s.gpr.rdx.qword=0x400000;s.gpr.rsp.qword=0x500000;
   s.aflag.cf=s.aflag.pf=s.aflag.af=s.aflag.zf=s.aflag.sf=s.aflag.of=1;s.x87.fxsave64.mxcsr.flat=control(mode);
   functions[kind](s,0x100000+kind*0x10000,&m);
   if(!m.fault.empty() || m.returns!=1 || s.gpr.rip.qword!=0xfeedface || s.gpr.rsp.qword!=0x500008)return 2;
   if(!(s.aflag.cf && s.aflag.pf && s.aflag.af && s.aflag.zf && s.aflag.sf && s.aflag.of) || s.x87.fxsave64.mxcsr.flat!=control(mode))return 3;
   verify(in.data()+i,out,kind);
   if(!mode)memcpy(baseline.data()+i,out,32);else if(memcmp(baseline.data()+i,out,32))return 4;
  }
 }
 puts("arm64 AOT: 6 forms, 8192 input bits, 32 MXCSR modes/status seeds, ISA bounds and special cases; flags and upper lanes pass");}
''')
run('check-build',['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-I'+str(r/'remill/include'),'-I'+str(src),out/'check.cpp',src/'runtime.cpp',*[out/f'form-{i}.o' for i in range(6)],'-o',out/'check'])
run('check',[out/'check'])
v={'passed':True,'forms':6,'input_bit_patterns':8192,'mxcsr_modes_and_status_seeds':32,'output_lanes':6*8192*32,'oracle':'x86-64 under Rosetta','comparison':'independent ISA relative-error and special/lane contracts, not bit-exact processor approximation equivalence','semantics_sha256':hashlib.sha256((r/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc').read_bytes()).hexdigest(),'game_frames':False}
(out/'verification.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps(v))
