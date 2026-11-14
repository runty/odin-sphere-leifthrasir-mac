#!/usr/bin/env python3
"""Prove a register-only LLVM loop stops at the inserted guest budget guard."""
import argparse
from pathlib import Path
import subprocess
from guard_ir import instrument

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--remill', required=True, type=Path)
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
root.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
ir = '''define ptr @sub_1(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {
  br label %exit
exit:
  ret ptr %memory
}
define ptr @sub_2(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {
  br label %loop
loop:
  %counter = phi i64 [0, %0], [%next, %loop]
  %next = add i64 %counter, 1
  br label %loop
}
'''
guarded, count = instrument(ir)
assert count == 3
(root / 'loop.ll').write_text(guarded)
(root / 'check.cpp').write_text('''#include "runtime.h"
#include <cassert>
#include <memory>
#include <cstdio>
extern "C" Memory *sub_1(State &, uint64_t, Memory *);
extern "C" Memory *sub_2(State &, uint64_t, Memory *);
int main() {
 auto m=std::make_unique<Memory>(); State state{};
 sub_1(state,0,m.get()); assert(m->checkpoints==1 && m->fault.empty());
 m->checkpoints=0; m->checkpoint_limit=32;
 std::jmp_buf checkpoint; m->recovery=&checkpoint;
 if(setjmp(checkpoint)==0) { sub_2(state,0,m.get()); assert(false); }
 m->recovery=nullptr;
 assert(m->fault=="guest execution budget exhausted" && m->checkpoints==33);
 m->fault.clear();m->checkpoints=0;m->checkpoint_limit=1000000;
 m->deadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
 m->recovery=&checkpoint;
 if(setjmp(checkpoint)==0) { sub_2(state,0,m.get()); assert(false); }
 m->recovery=nullptr;
 assert(m->fault=="guest execution deadline exceeded" && m->checkpoints==1024);
 puts("Forward branch unchanged; register-only loop budget and deadline: PASS");
}
''')
subprocess.run(['/opt/homebrew/opt/llvm/bin/clang', '-target', 'arm64-apple-macos15', '-O2', '-c', str(root / 'loop.ll'), '-o', str(root / 'loop.o')], check=True)
subprocess.run(['xcrun', 'clang++', '-arch', 'arm64', '-std=c++17', '-O2', '-I'+str(a.remill / 'remill/include'), '-I'+str(source.parent / 'cpu'), str(source.parent / 'cpu/runtime.cpp'), str(root / 'check.cpp'), str(root / 'loop.o'), '-o', str(root / 'check')], check=True)
subprocess.run([str(root / 'check')], check=True, timeout=5)
