#!/usr/bin/env python3
"""Compare confirmed AVX.128 integer selectors with native x86-64 execution.

Adapted from OpenCrown fcd9579d7486a48ef23483cdf55b5bbf64302003,
ios/cpu/review/confirmed-packed.py. Uses Odin's actual runtime and an audited
isolated backend; no shared backend mutation, game data or device operations.
"""
import argparse,hashlib,json,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--remill',type=Path,required=True);a=p.parse_args();out=a.root.resolve();backend=a.remill.resolve();cpu=Path(__file__).resolve().parent;remill=backend/'remill';build=backend/'remill-build';llvm=Path('/opt/homebrew/opt/llvm/bin')
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new directory on mounted external storage')
out.mkdir(parents=True)
def run(command,log,cwd=None,env=None):
 with (out/log).open('wb') as stream:subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,cwd=cwd,env=env,check=True,timeout=180)
run(['python3',cpu.parent/'audit-backend.py','--backend',remill,'--legacy-packed-integer'],'backend-audit.log')
shared=[remill/'lib/Arch/X86/Semantics/MMX.cpp',remill/'include/remill/Arch/X86/Runtime/State.h',build/'lib/Arch/X86/Runtime/amd64_avx.bc',build/'bin/lift/remill-lift-23',cpu/'runtime.cpp',cpu/'runtime.h',Path(__file__)]
hashes={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in shared}
specs=[]
def binary(group, name, mnemonic, reg):
    for alias, a, b in [
        ("reg", reg + "1", reg + "2"),
        ("first_alias", reg + "0", reg + "2"),
        ("second_alias", reg + "1", reg + "0"),
    ]:
        specs.append((group, name + "_" + alias, f"{mnemonic} {reg}0,{a},{b}"))


for name in ["vpmulld", "vpunpckhdq", "vpunpckhwd", "vpunpcklwd", "vpunpckldq"]:
    binary("integer", name, name, "xmm")
for alias, source in [("mem", "xmm1"), ("mem_first_alias", "xmm0")]:
    specs.append(
        (
            "integer",
            "vpunpckldq_" + alias,
            f"vpunpckldq xmm0,{source},xmmword ptr [rdi+64]",
        )
    )
specs.append(
    ("integer", "vpunpckldq_mem_unaligned", "vpunpckldq xmm0,xmm1,xmmword ptr [rdi+65]")
)
# Integer bit patterns under every RC/DAZ/FTZ control combination. The
# operation must preserve all MXCSR bits and the six arithmetic flags.
values = [
    0,
    0x80000000,
    1,
    0x80000001,
    0x3F800000,
    0xBF800000,
    0x3F800001,
    0xBF800001,
    0x00800000,
    0x80800000,
    0x007FFFFF,
    0x807FFFFF,
    0x7F7FFFFF,
    0xFF7FFFFF,
    0x7F800000,
    0xFF800000,
    0x7FC12345,
    0xFFC12345,
    0x7F812345,
    0xFF812345,
    0x7FFFFFFF,
    0xFFFFFFFF,
    0x55555555,
    0xAAAAAAAA,
]
seeds = []
for left in range(len(values)):
    for right in range(len(values)):
        for rc in range(4):
            for daz in range(2):
                for ftz in range(2):
                    first = [
                        values[left],
                        values[right],
                        values[(left + 3) % 24],
                        values[(right + 5) % 24],
                        values[(left + 7) % 24],
                        values[(right + 11) % 24],
                        values[(left + 13) % 24],
                        values[(right + 17) % 24],
                    ]
                    second = [
                        values[right],
                        values[left],
                        values[(right + 9) % 24],
                        values[(left + 15) % 24],
                        values[(right + 4) % 24],
                        values[(left + 8) % 24],
                        values[(right + 12) % 24],
                        values[(left + 16) % 24],
                    ]
                    dest = [values[(left + lane * 5) % 24] for lane in range(8)]
                    seed = bytearray(struct.pack("<24I", *(dest + first + second)))
                    seed += struct.pack(
                        "<I", 0x1F80 | (rc << 13) | (daz << 6) | (ftz << 15)
                    ) + bytes(12)
                    seeds.append(seed)
for index in range(64):
    state = 0x9E3779B9 * (index + 1)
    words = []
    for _ in range(24):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        words.append(state)
    seeds.append(struct.pack("<24I", *words) + struct.pack("<I", 0x1F80) + bytes(12))
(out / "seeds.h").write_text(
    "#include <cstdint>\nalignas(32) static const uint8_t seeds[][112]={"
    + ",".join("{" + ",".join(str(v) for v in seed) + "}" for seed in seeds)
    + "};\n"
)
assembly = ".intel_syntax noprefix\n.text\n"
objects, declarations, calls = [], [], []
for i, (group, name, instruction) in enumerate(specs):
    code = (
        "sub rsp,16\nstmxcsr dword ptr [rsp]\nldmxcsr dword ptr [rdi+96]\n"
        "vmovdqu ymm0,ymmword ptr [rdi]\nvmovdqu ymm1,ymmword ptr [rdi+32]\n"
        "vmovdqu ymm2,ymmword ptr [rdi+64]\n"
        + "push 0xad7\npopfq\n" + instruction
        + "\npushfq\npop rax\nmov dword ptr [rsi+100],eax\nvmovdqu ymmword ptr [rsi],ymm0\nvmovdqu ymmword ptr [rsi+32],ymm1\n"
        "vmovdqu ymmword ptr [rsi+64],ymm2\nstmxcsr dword ptr [rsi+96]\n"
        "ldmxcsr dword ptr [rsp]\nadd rsp,16\nvzeroupper\nret\n"
    )
    (out / f"{name}.s").write_text(".intel_syntax noprefix\n.text\n" + code)
    run(
        [
            llvm / "clang",
            "-target",
            "x86_64-unknown-linux-gnu",
            "-c",
            out / f"{name}.s",
            "-o",
            out / f"{name}.elf.o",
        ],
        name + "-assemble.log",
    )
    run(
        [
            llvm / "llvm-objcopy",
            "-O",
            "binary",
            "--only-section=.text",
            out / f"{name}.elf.o",
            out / f"{name}.bin",
        ],
        name + "-extract.log",
    )
    pc = 0x3000000 + i * 0x1000
    run(
        [
            build / "bin/lift/remill-lift-23",
            "--os",
            "linux",
            "--arch",
            "amd64_avx",
            "--address",
            str(pc),
            "--bytes",
            (out / f"{name}.bin").read_bytes().hex(),
            "--ir_out",
            out / f"{name}.ll",
        ],
        name + "-lift.log",
    )
    run(
        [
            llvm / "clang",
            "-target",
            "arm64-apple-macos15",
            "-O2",
            "-c",
            out / f"{name}.ll",
            "-o",
            out / f"{name}.o",
        ],
        name + "-compile.log",
    )
    objects.append(out / f"{name}.o")
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{group}","{name}",sub_{pc:x},{pc}}}')
    assembly += f".globl _native_{i}\n_native_{i}:\n" + code
(out / "native.s").write_text(assembly)
(out / "native.cpp").write_text(
    '#include <cstdio>\n#include "seeds.h"\n'
    + "\n".join(
        f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs))
    )
    + "\nusing Fn=void(*)(const void*,void*);\nFn calls[]={"
    + ",".join(f"native_{i}" for i in range(len(specs)))
    + "};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[104];fn(seed,output);if(fwrite(output,1,104,stdout)!=104)return 1;}}\n"
)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        out / "native.cpp",
        out / "native.s",
        "-o",
        out / "native",
    ],
    "native-build.log",
)
run(
    [out / "native"],
    "native-results.bin",
    env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
)
expected = out / "native-results.bin"
assert expected.stat().st_size == len(specs) * len(seeds) * 104
(out / "expected.s").write_text(
    ".section __TEXT,__const\n.p2align 4\n.globl _packed_expected\n_packed_expected:\n.incbin "
    + json.dumps(str(expected))
    + "\n"
)
run(
    [
        "xcrun",
        "clang",
        "-arch",
        "arm64",
        "-c",
        out / "expected.s",
        "-o",
        out / "expected.o",
    ],
    "oracle-embed.log",
)
program = (
    """#include "runtime.h"
#include "seeds.h"
#include <cstdio>
#include <cstring>
#include <array>
#include <memory>
extern "C" const unsigned char packed_expected[];
"""
    + "\n".join(declarations)
    + """
struct Test{const char *group;const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={"""
    + ",".join(calls)
    + """};
int main(){uint64_t index=0,checks=0,failures=0,group_checks=0,group_failures=0,group_routines=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){auto state_ptr=std::make_unique<State>();State &state=*state_ptr;auto memory_ptr=std::make_unique<Memory>();Memory &memory=*memory_ptr;
std::array<uint8_t,112> input{};std::array<uint8_t,104> output{};std::array<uint8_t,64> stack{};output.fill(0xa7);
state.x87.fxsave.mxcsr.flat=0x1f80;
state.gpr.rdi.qword=0x200000;state.gpr.rsi.qword=0x300000;state.gpr.rsp.qword=0x400030;
std::memcpy(input.data(),seed,sizeof(seed));uint64_t end=0xfeedface;std::memcpy(stack.data()+48,&end,8);
mapRegion(memory,0x200000,input.data(),input.size(),false);mapRegion(memory,0x300000,output.data(),output.size());mapRegion(memory,0x400000,stack.data(),stack.size());
std::jmp_buf recovery;memory.recovery=&recovery;if(setjmp(recovery)==0)test.function(state,test.pc,&memory);memory.recovery=nullptr;
if(memory.returns!=1 || state.gpr.rip.qword!=end || state.gpr.rsp.qword!=0x400038 || std::memcmp(input.data(),seed,sizeof(seed))){++failures;if(failures<=100)std::printf("RETURN/INPUT %s seed%u\\n",test.name,seed_index);}
for(unsigned word=0;word<26;++word){uint32_t a,r;std::memcpy(&a,output.data()+word*4,4);
std::memcpy(&r,packed_expected+(index*"""
    + str(len(seeds))
    + """+seed_index)*104+word*4,4);
if(word==25){a&=0x8d5u;r&=0x8d5u;}++checks;++group_checks;
if(a!=r){++failures;++group_failures;if(failures<=100)std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;++group_failures;if(failures<=100)std::printf("FAULT %s %.*s\\n",test.name,int(memory.fault.size()),memory.fault.data());}++seed_index;}
++index;++group_routines;
if(index==sizeof(tests)/sizeof(tests[0]) || std::strcmp(tests[index].group,test.group)){
std::printf("GROUP %s routines=%llu checks=%llu failures=%llu\\n",test.group,group_routines,group_checks,group_failures);std::fflush(stdout);group_checks=group_failures=group_routines=0;}}
std::printf("SUMMARY routines=%llu vectors=%zu checks=%llu failures=%llu\\n",index,sizeof(seeds)/sizeof(seeds[0]),checks,failures);return failures?1:0;}
"""
)
(out / "review.cpp").write_text(program)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "arm64",
        "-std=c++20",
        "-O2",
        "-I" + str(cpu),
        "-I" + str(remill / "include"),
        "-I" + str(out),
        out / "review.cpp",
        cpu / "runtime.cpp",
        out / "expected.o",
        *objects,
        "-o",
        out / "review",
    ],
    "review-build.log",
)
with (out / "review-results.txt").open("w") as log:
    status = subprocess.run(
        [str(out / "review")], stdout=log, stderr=subprocess.STDOUT
    ).returncode
unchanged=all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in shared)
text=(out/'review-results.txt').read_text();print(text,flush=True)
match=re.search(r'SUMMARY routines=(\d+) vectors=(\d+) checks=(\d+) failures=(\d+)',text);assert match
routines,vectors,checks,failures=map(int,match.groups());assert routines==len(specs)==18 and vectors==len(seeds) and checks==routines*vectors*26
report={'passed':status==0 and failures==0 and unchanged,'routines':routines,'vectors':vectors,'word_checks':checks,'return_and_readonly_input_checks':routines*vectors,'failed':failures,'shared_backend_and_runtime_unchanged':unchanged,'oracle_sha256':hashlib.sha256(expected.read_bytes()).hexdigest(),'sources':hashes,'native_oracle':'x86-64 under Rosetta','aot_host':'arm64 macOS','coverage':'all six added selectors, register aliases, unaligned memory, three complete YMM registers, MXCSR and arithmetic flags','game_frames':False,'device_contacted':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');(out/'specs.json').write_text(json.dumps(specs,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='sources'}));assert unchanged
raise SystemExit(status)
