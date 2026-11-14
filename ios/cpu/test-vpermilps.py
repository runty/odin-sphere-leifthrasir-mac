#!/usr/bin/env python3
"""Compare all immediate VPERMILPS forms against bounded native x86 execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--remill', type=Path, required=True)
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Use a mounted external work directory')
root.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
llvm = Path('/opt/homebrew/opt/llvm/bin')


def run(name, command, env=None):
    with (root / (name + '.log')).open('w') as out:
        subprocess.run(list(map(str, command)), stdout=out, stderr=subprocess.STDOUT,
                       env=env, check=True, timeout=120)


codes = []
declarations = []
for kind in range(4):
    # rdi=input, rsi=output, rdx=upper-lane canary. Each immediate is
    # independent: initialize both source and destination before executing it.
    code = bytearray()
    for immediate in range(256):
        code += bytes.fromhex('c5fe6f07 c57e6f0a')  # vmovdqu ymm0,[rdi]; ymm9,[rdx]
        code += bytes([0xc4, 0x63, 0x7d if kind & 1 else 0x79, 0x04,
                       0x0f if kind & 2 else 0xc8, immediate])
        code += bytes.fromhex('c57e7f8e') + struct.pack('<I', immediate * 32)
    code += b'\xc3'
    codes.append(code.hex())
    address = 0x100000 + kind * 0x10000
    ir = root / f'form-{kind}.ll'
    run(f'lift-{kind}', [a.remill / 'remill-build/bin/lift/remill-lift-23', '--os', 'linux',
        '--arch', 'amd64_avx', '--address', address, '--bytes', code.hex(), '--ir_out', ir])
    if '__remill_sync_hyper_call' in ir.read_text() or '__remill_error(' in ir.read_text():
        raise RuntimeError('Shuffle still requires an instruction fallback')
    declarations.append(f'extern "C" Memory *sub_{address:x}(State &,uint64_t,Memory *);')
    run(f'compile-{kind}', [llvm / 'clang', '-target', 'arm64-apple-macos15', '-O2', '-c', ir,
                            '-o', root / f'form-{kind}.o'])

(root / 'codes.h').write_text('#pragma once\nstatic const char *codes[]={' +
                              ','.join(json.dumps(x) for x in codes) + '};\n')
patterns = [0x7fc01234, 0x80000000, 0xffffffff, 0x00000001,
            0x7f800000, 0x00000000, 0x12345678, 0xff800000]
common = '#include <cstdlib>\n#include <cstdint>\n#include <cstring>\n#include <cstdio>\n#include <vector>\n'
common += 'static const uint32_t input[]={' + ','.join(map(str, patterns)) + '};\n'
common += r'''
static void verify(const uint8_t *output, unsigned kind) {
    for (unsigned imm=0;imm<256;++imm) for(unsigned lane=0;lane<8;++lane) {
        uint32_t value; memcpy(&value,output+imm*32+lane*4,4);
        uint32_t expected=(!(kind&1) && lane>=4) ? 0 : input[(lane/4)*4+((imm>>((lane%4)*2))&3)];
        if(value!=expected) { fprintf(stderr,"form %u imm %u lane %u: %x != %x\n",kind,imm,lane,value,expected); std::abort(); }
    }
}
'''
(root / 'invoke.s').write_text('''.text
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
(root / 'oracle.cpp').write_text(common + r'''
#include "codes.h"
#include <cstdlib>
#include <sys/mman.h>
extern "C" uint64_t invoke(void *,const void *,void *,const void *);
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 FILE *f=fopen(argv[1],"wb"); if(!f) return 3;
 for(unsigned kind=0;kind<4;++kind) {
  size_t n=strlen(codes[kind])/2;
  auto code=static_cast<uint8_t *>(mmap(nullptr,n,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
  if(code==MAP_FAILED) return 4;
  for(size_t i=0;i<n;++i) { unsigned b; sscanf(codes[kind]+i*2,"%2x",&b); code[i]=b; }
  std::vector<uint8_t> out(8192,0x5c),canary(32,0xa5);
  uint64_t flags=invoke(code,input,out.data(),canary.data());
  if((flags&0x8d5)!=0x8d5) return 5;
  verify(out.data(),kind);
  if(fwrite(out.data(),1,out.size(),f)!=out.size()) return 6;
  munmap(code,n);
 }
 if(fclose(f)) return 7;
 puts("Native x86: 1024 shuffles / 8192 lane values, flags and upper YMM clearing passed");
}
''')
run('oracle-build', ['xcrun', 'clang++', '-arch', 'x86_64', '-std=c++17', '-O2', root / 'oracle.cpp',
                     root / 'invoke.s', '-o', root / 'oracle'])
run('oracle', [root / 'oracle', root / 'reference.bin'], dict(os.environ, ROSETTA_ADVERTISE_AVX='1'))
(root / 'check.cpp').write_text(common + '#include "runtime.h"\n#include <cstdlib>\n' +
    '\n'.join(declarations) + '\nstatic Lifted functions[]={' +
    ','.join(f'sub_{0x100000+i*0x10000:x}' for i in range(4)) + '};\n' + r'''
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 FILE *f=fopen(argv[1],"rb"); if(!f) return 3;
 for(unsigned kind=0;kind<4;++kind) {
  State s{}; Memory m;
  std::vector<uint8_t> out(8192,0x5c),reference(8192),canary(32,0xa5),stack(32);
  mapRegion(m,0x200000,const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(input)),32,false);
  mapRegion(m,0x300000,out.data(),out.size()); mapRegion(m,0x400000,canary.data(),32,false);
  mapRegion(m,0x500000,stack.data(),32);
  uint64_t end=0xfeedface; memcpy(stack.data(),&end,8);
  s.gpr.rdi.qword=0x200000; s.gpr.rsi.qword=0x300000; s.gpr.rdx.qword=0x400000; s.gpr.rsp.qword=0x500000;
  s.aflag.cf=s.aflag.pf=s.aflag.af=s.aflag.zf=s.aflag.sf=s.aflag.of=1;
  functions[kind](s,0x100000+kind*0x10000,&m);
  if(!m.fault.empty() || m.returns!=1 || s.gpr.rip.qword!=end || s.gpr.rsp.qword!=0x500008) return 4;
  if(!(s.aflag.cf && s.aflag.pf && s.aflag.af && s.aflag.zf && s.aflag.sf && s.aflag.of)) return 5;
  if(fread(reference.data(),1,reference.size(),f)!=reference.size() || out!=reference) return 6;
  verify(out.data(),kind);
 }
 fclose(f);
 puts("arm64 AOT: 1024 shuffles / 8192 lanes match native x86, flags preserved, XMM upper YMM cleared");
}
''')
run('check-build', ['xcrun', 'clang++', '-arch', 'arm64', '-std=c++17', '-O2',
    '-I' + str(a.remill / 'remill/include'), '-I' + str(source), root / 'check.cpp', source / 'runtime.cpp',
    *[root / f'form-{i}.o' for i in range(4)], '-o', root / 'check'])
run('check', [root / 'check', root / 'reference.bin'])
result = {'passed': True, 'forms': 4, 'immediates_per_form': 256, 'lane_values': 8192,
          'native_oracle': 'x86-64 under Rosetta', 'aot_host': 'arm64 macOS',
          'reference_sha256': hashlib.sha256((root / 'reference.bin').read_bytes()).hexdigest(),
          'semantics_sha256': hashlib.sha256((a.remill / 'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc').read_bytes()).hexdigest(),
          'game_frames': False}
(root / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
