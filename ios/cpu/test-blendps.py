#!/usr/bin/env python3
"""Compare every immediate packed-single blend form with native x86 execution."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();root=a.root.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use a mounted external directory')
if root.exists():p.error('Use a new test directory')
root.mkdir(parents=True);source=Path(__file__).resolve().parent;llvm=Path('/opt/homebrew/opt/llvm/bin')
def run(label,args,env=None):
 with (root/(label+'.log')).open('w') as out:subprocess.run(list(map(str,args)),stdout=out,stderr=subprocess.STDOUT,env=env,check=True,timeout=180)
codes=[];declarations=[]
for kind in range(6):
 lines=['.text'];vector='ymm' if kind>=4 else 'xmm'
 for immediate in range(256):
  lines+=['vmovdqu (%rdi), %ymm0','vmovdqu (%rdx), %ymm8','vmovdqu (%rdi), %ymm9' if kind<2 else 'vmovdqu (%rcx), %ymm9']
  operand='(%rdx)' if kind&1 else '%'+vector+'8'
  lines+=[f'blendps ${immediate}, {operand}, %xmm9' if kind<2 else f'vblendps ${immediate}, {operand}, %{vector}0, %{vector}9',f'vmovdqu %ymm9, {immediate*32}(%rsi)']
 lines+=['retq'];assembly='\n'.join(lines)+'\n';(root/f'form-{kind}.s').write_text(assembly)
 result=subprocess.run([str(llvm/'llvm-mc'),'-triple=x86_64','-show-encoding'],input=assembly,capture_output=True,text=True,check=True)
 (root/f'encoding-{kind}.txt').write_text(result.stdout)
 encoded=re.findall(r'encoding: \[([^\]]+)\]',result.stdout);assert len(encoded)==256*5+1
 code=bytes(int(x.strip(),16) for item in encoded for x in item.split(','));codes.append(code.hex())
 address=0x100000+kind*0x10000;ir=root/f'form-{kind}.ll'
 run(f'lift-{kind}',[a.remill/'remill-build/bin/lift/remill-lift-23','--os','linux','--arch','amd64_avx','--address',address,'--bytes',code.hex(),'--ir_out',ir])
 text=ir.read_text()
 if '__remill_sync_hyper_call' in text or '__remill_error(' in text:raise RuntimeError('Blend still requires an instruction fallback')
 run(f'compile-{kind}',[llvm/'clang','-target','arm64-apple-macos15','-O2','-c',ir,'-o',root/f'form-{kind}.o'])
 declarations.append(f'extern "C" Memory *sub_{address:x}(State &,uint64_t,Memory *);')
(root/'codes.h').write_text('static const char *codes[]={'+','.join(json.dumps(x) for x in codes)+'};\n')
common=r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static const uint32_t first[]={0x7fc01234,0x80000000,0xffffffff,1,0x7f800000,0,0x12345678,0xff800000};
static const uint32_t second[]={0x80000001,0x7fa13579,0,0xffffffff,0xff800000,0x55aa55aa,0x7f800001,0x80000000};
static const uint32_t canary[]={0x13579bdf,0x2468ace0,0xabcdef01,0x76543210,0xa5a5a5a5,0x5a5a5a5a,0x10101010,0x02020202};
static void verify(const uint8_t *out,unsigned kind){
 for(unsigned mask=0;mask<256;++mask)for(unsigned lane=0;lane<8;++lane){
  uint32_t got;memcpy(&got,out+mask*32+lane*4,4);
  uint32_t expected=kind<4 && lane>=4 ? (kind<2?first[lane]:0) : ((mask&(1u<<lane))?second[lane]:first[lane]);
  if(got!=expected){fprintf(stderr,"kind %u mask %u lane %u: %08x != %08x\n",kind,mask,lane,got,expected);abort();}
 }
}
'''
(root/'invoke.s').write_text('''.text
.globl _invoke
_invoke:
subq $8,%rsp
movq %rdi,%r11
movq %rsi,%rdi
movq %rdx,%rsi
movq %rcx,%rdx
movq %r8,%rcx
pushq $0xad7
popfq
callq *%r11
pushfq
popq %rax
leaq 8(%rsp),%rsp
retq
''')
(root/'oracle.cpp').write_text(common+r'''
#include "codes.h"
#include <sys/mman.h>
#include <xmmintrin.h>
extern "C" uint64_t invoke(void *,const void *,void *,const void *,const void *);
int main(int argc,char **argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"wb");if(!f)return 3;
 for(unsigned kind=0;kind<6;++kind){size_t n=strlen(codes[kind])/2;
  auto code=(uint8_t*)mmap(nullptr,n,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);if(code==MAP_FAILED)return 4;
  for(size_t i=0;i<n;++i){unsigned b;sscanf(codes[kind]+i*2,"%2x",&b);code[i]=b;}
  std::vector<uint8_t> out(8192,0x5c);_mm_setcsr(0x1f95);auto flags=invoke(code,first,out.data(),second,canary);auto csr=_mm_getcsr();_mm_setcsr(0x1f80);
  if((flags&0x8d5)!=0x8d5 || csr!=0x1f95)return 5;
  verify(out.data(),kind);if(fwrite(out.data(),1,out.size(),f)!=out.size())return 6;munmap(code,n);
 }if(fclose(f))return 7;puts("native x86: all 6 forms / 256 immediates / 12288 lanes, flags and upper lanes pass");}
''')
run('oracle-build',['xcrun','clang++','-arch','x86_64','-std=c++17','-O2',root/'oracle.cpp',root/'invoke.s','-o',root/'oracle'])
run('oracle',[root/'oracle',root/'reference.bin'],dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
(root/'check.cpp').write_text(common+'\n#include "runtime.h"\n'+'\n'.join(declarations)+'\nstatic Lifted functions[]={'+','.join(f'sub_{0x100000+i*0x10000:x}' for i in range(6))+'};\n'+r'''
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 3;
 for(unsigned kind=0;kind<6;++kind){State s{};Memory m;std::vector<uint8_t> out(8192,0x5c),reference(8192),stack(32);
  mapRegion(m,0x200000,(uint8_t*)first,32,false);mapRegion(m,0x400000,(uint8_t*)second,32,false);mapRegion(m,0x600000,(uint8_t*)canary,32,false);
  mapRegion(m,0x300000,out.data(),out.size());mapRegion(m,0x500000,stack.data(),32);uint64_t end=0xfeedface;memcpy(stack.data(),&end,8);
  s.gpr.rdi.qword=0x200000;s.gpr.rsi.qword=0x300000;s.gpr.rdx.qword=0x400000;s.gpr.rcx.qword=0x600000;s.gpr.rsp.qword=0x500000;
  s.aflag.cf=s.aflag.pf=s.aflag.af=s.aflag.zf=s.aflag.sf=s.aflag.of=1;s.x87.fxsave64.mxcsr.flat=0x1f95;
  functions[kind](s,0x100000+kind*0x10000,&m);
  if(!m.fault.empty() || m.returns!=1 || s.gpr.rip.qword!=end || s.gpr.rsp.qword!=0x500008)return 4;
  if(!(s.aflag.cf && s.aflag.pf && s.aflag.af && s.aflag.zf && s.aflag.sf && s.aflag.of) || s.x87.fxsave64.mxcsr.flat!=0x1f95)return 5;
  if(memcmp(&s.vec[0],first,32) || memcmp(&s.vec[8],second,32))return 6;
  if(fread(reference.data(),1,reference.size(),f)!=reference.size() || out!=reference)return 7;
  verify(out.data(),kind);
 }fclose(f);puts("arm64 AOT: all 6 forms / 256 immediates / 12288 lanes match native x86; sources, flags and upper lanes pass");}
''')
run('check-build',['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-I'+str(a.remill/'remill/include'),'-I'+str(source),root/'check.cpp',source/'runtime.cpp',*[root/f'form-{i}.o' for i in range(6)],'-o',root/'check'])
run('check',[root/'check',root/'reference.bin'])
result={'passed':True,'forms':6,'immediates_per_form':256,'lane_values':12288,'native_oracle':'x86-64 under Rosetta','aot_host':'arm64 macOS',
 'reference_sha256':hashlib.sha256((root/'reference.bin').read_bytes()).hexdigest(),'semantics_sha256':hashlib.sha256((a.remill/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc').read_bytes()).hexdigest(),'game_frames':False}
(root/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
