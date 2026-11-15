#!/usr/bin/env python3
"""Compile production libc IR shims and verify Odin's checked bulk/RET adapter."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from guard_ir import instrument

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();source=Path(__file__).resolve().parent
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Output requires a mounted external disk')
out.mkdir(parents=True,exist_ok=True)
image='51a38d7dc7476f0163c27273eccd0c37b4861e53b17a5e48b7e1234749602f22'
pcs=[0x10cd0ba0,0x10cd0c40,0x10cd0ce0]
for pc,name in zip(pcs,('memcpy','memmove','memset')):
    raw=f'define ptr @sub_{pc:x}(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {{\n  ret ptr %memory\n}}\n'
    guarded,count=instrument(raw,libc_helpers=True,image_sha256=image)
    assert count==1 and f'%native_result = call ptr @__osl_libc_{name}(' in guarded
    assert '%native_result =' not in instrument(raw,libc_helpers=False)[0]
    try:instrument(raw,libc_helpers=True,image_sha256='0'*64)
    except ValueError:pass
    else:raise AssertionError('Unverified image accepted native helper')
    (out/f'{pc:x}.ll').write_text(guarded)
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
includes=['-I'+str(a.backend.resolve()/'remill/include'),'-I'+str(source.parent/'cpu')]
def run(command,log):
    with (out/log).open('w') as f:subprocess.run(list(map(str,command)),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
results={}
for label,arch,sdk,sanitizer in [('arm64','arm64','macosx',None),('x86_64','x86_64','macosx',None),('address','arm64','macosx','address,undefined'),('thread','arm64','macosx','thread'),('iphoneos','arm64','iphoneos',None),('simulator','arm64','iphonesimulator',None)]:
    target=arch+'-apple-'+('macos15' if sdk=='macosx' else 'ios18'+('-simulator' if sdk=='iphonesimulator' else ''))
    sysroot=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
    common=['-target',target,'-isysroot',sysroot,'-O2'];extra=['-g','-fno-omit-frame-pointer','-fsanitize='+sanitizer,'-fno-sanitize-recover=all'] if sanitizer else []
    directory=out/label;directory.mkdir(exist_ok=True);objects=[]
    for pc in pcs:
        # Sanitized IR and C++ must use the same compiler-rt ABI. Normal runs
        # retain the exact production Homebrew LLVM compiler for the shims.
        ir_compiler=['xcrun','clang'] if sanitizer else [llvm]
        obj=directory/f'{pc:x}.o';run([*ir_compiler,*common,*extra,'-c',out/f'{pc:x}.ll','-o',obj],f'{label}-{pc:x}.log');objects.append(obj)
    if sdk=='macosx':
        binary=directory/'test'
        run(['xcrun','clang++',*common,*extra,'-std=c++17',*includes,source/'libc-bulk-test.cpp',source.parent/'cpu/runtime.cpp',*objects,'-o',binary],label+'-build.log')
        run([binary],label+'-run.log');report=json.loads((out/(label+'-run.log')).read_text());assert report['passed'] and report['checks']>600000;results[label]=report
    else:
        run(['xcrun','--sdk',sdk,'clang++',*common,'-std=c++17',*includes,'-c',source/'libc-bulk-test.cpp','-o',directory/'fixture.o'],label+'-fixture.log')
        run(['xcrun','--sdk',sdk,'clang++',*common,'-std=c++17',*includes,'-c',source.parent/'cpu/runtime.cpp','-o',directory/'runtime.o'],label+'-runtime.log')
        run(['xcrun','ar','rcs',directory/'fixture.a',*objects,directory/'fixture.o',directory/'runtime.o'],label+'-archive.log');results[label]={'compiled':True,'executed':False}
report={'passed':True,'scope':'production IR generator and checked helper ABI/bytes/faults; thin fixture exports mirror session wrappers; no gameplay or physical execution claim','results':results,'sources':{str(x.relative_to(source.parent.parent)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [Path(__file__),source/'guard_ir.py',source/'libc-bulk.h',source/'libc-bulk-test.cpp',source.parent/'cpu/runtime.cpp',source.parent/'cpu/runtime.h']}}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
