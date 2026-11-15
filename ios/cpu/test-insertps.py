#!/usr/bin/env python3
"""Compare INSERTPS and VINSERTPS on Rosetta and the checked arm64 runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--remill', type=Path, required=True)
args = p.parse_args()
root = args.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
root.mkdir(parents=True, exist_ok=False)
cpu = Path(__file__).resolve().parent
llvm = Path('/opt/homebrew/opt/llvm/bin')
backend = args.remill.resolve()
# (legacy SSE, destination, inserted register or memory). The first source
# is always xmm0. Case 4 contains the operand aliases seen in the game.
forms = [
    (True, 0, 3), (True, 0, 0), (True, 0, None),
    (False, 9, 3), (False, 0, 3), (False, 3, 3),
    (False, 9, 0), (False, 0, 0), (False, 9, None), (False, 0, None),
]


def run(label, command, env=None):
    with (root / (label + '.log')).open('w') as out:
        subprocess.run(list(map(str, command)), check=True, stdout=out, stderr=subprocess.STDOUT,
                       timeout=180, env=env)


assemblies, encodings, addresses = [], [], []
for i, (legacy, dest, inserted) in enumerate(forms):
    body = []
    source = '(%rdx)' if inserted is None else f'%xmm{inserted}'
    for immediate in range(256):
        for slot, reg in enumerate((0, 3, 9)):
            body.append(f'vmovdqu {32 * slot}(%rdi), %ymm{reg}')
        if legacy:
            body.append(f'insertps ${immediate}, {source}, %xmm{dest}')
        else:
            body.append(f'vinsertps ${immediate}, {source}, %xmm0, %xmm{dest}')
        for slot, reg in enumerate((0, 3, 9)):
            body.append(f'vmovdqu %ymm{reg}, {96 * immediate + slot * 32}(%rsi)')
    body.append('retq')
    assembly = '.text\n' + '\n'.join(body) + '\n'
    asmfile = root / f'form-{i}.s'
    asmfile.write_text(f'.globl _form{i}\n_form{i}:\n' + assembly)
    assemblies.append(asmfile)
    encoded = subprocess.run([llvm / 'llvm-mc', '-triple=x86_64', '-show-encoding'],
                             input=assembly, capture_output=True, text=True, check=True)
    instructions = re.findall(r'encoding: \[([^\]]+)\]', encoded.stdout)
    assert len(instructions) == len(body)
    encodings.append(bytes(int(b, 16) for insn in instructions for b in insn.split(',')))
    addresses.append(0x100000 + 0x10000 * i)

# Set flags immediately before executing the reference, record them before returning to C++.
wrapper = root / 'invoke.s'
wrapper.write_text('''.text
.globl _invoke
_invoke:
subq $8, %rsp
movq %rdi, %r11
movq %rsi, %rdi
movq %rdx, %rsi
movq %rcx, %rdx
pushq %r8
popfq
callq *%r11
pushfq
popq %rax
leaq 8(%rsp), %rsp
retq
''')
case_rows = [f'{{{int(legacy)},{dest},{-1 if inserted is None else inserted}}}'
             for legacy, dest, inserted in forms]
(root / 'forms.h').write_text('static constexpr Form forms[] = {' + ','.join(case_rows) + '};\n')
native_names = [f'form{i}' for i in range(len(forms))]
lifted_names = [f'sub_{pc:x}' for pc in addresses]
(root / 'functions.h').write_text(
    '#if defined(__x86_64__)\n' + ''.join(f'extern "C" void {n}();\n' for n in native_names) +
    'static auto native = std::array{' + ','.join(native_names) + '};\n#else\n' +
    ''.join(f'extern "C" Memory *{n}(State &,uint64_t,Memory *);\n' for n in lifted_names) +
    'static auto lifted = std::array{' + ','.join(lifted_names) + '};\n' +
    'static const uint64_t pcs[] = {' + ','.join(map(str, addresses)) + '};\n#endif\n')
common = ['xcrun', 'clang++', '-std=c++17', '-O2', '-I' + str(root), cpu / 'insertps-test.cpp']
run('oracle-build', [*common, '-arch', 'x86_64', wrapper, *assemblies, '-o', root / 'oracle'])
reference = root / 'reference.bin'
run('oracle', [root / 'oracle', reference], dict(os.environ, ROSETTA_ADVERTISE_AVX='1'))

objects = []
for i, (pc, code) in enumerate(zip(addresses, encodings)):
    ir = root / f'form-{i}.ll'
    obj = root / f'form-{i}.o'
    run(f'lift-{i}', [backend / 'remill-build/bin/lift/remill-lift-23', '--os', 'linux', '--arch',
                      'amd64_avx', '--address', pc, '--bytes', code.hex(), '--ir_out', ir])
    lifted = ir.read_text()
    if '__remill_sync_hyper_call' in lifted or '__remill_error(' in lifted:
        raise RuntimeError(f'Instruction fallback remains in form {i}')
    run(f'compile-{i}', [llvm / 'clang', '-target', 'arm64-apple-macos15', '-O2', '-c', ir, '-o', obj])
    objects.append(obj)
run('check-build', [*common, '-arch', 'arm64', '-I' + str(backend / 'remill/include'),
                    cpu / 'runtime.cpp', *objects, '-o', root / 'check'])
run('check', [root / 'check', reference])
result = {'forms':len(forms), 'operand_encodings':4, 'immediates':256, 'flag_patterns':2,
          'destination_lane_comparisons':len(forms)*256*2*8,
          'native_truncated_memory_faults':3, 'lifted_truncated_memory_faults':3,
          'native_oracle':'x86-64 under Rosetta', 'lifted_host':'arm64 macOS',
          'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),
          'semantics_sha256':hashlib.sha256((backend / 'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc').read_bytes()).hexdigest()}
(root / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
