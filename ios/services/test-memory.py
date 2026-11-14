#!/usr/bin/env python3
"""Exercise the production sparse allocator through the checked AOT CPU bridge."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1] / 'game'))
from service_build import compile_services
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args()
a.output=a.output.resolve()
if len(a.output.parts)<3 or a.output.parts[1]!='Volumes' or not Path('/Volumes',a.output.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
repo=Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
objects=compile_services(repo,a.output/'objects','arm64-apple-macos15',sdk)
exe=a.output/'memory-check'
with (a.output/'build.log').open('w') as log:
    subprocess.run(['xcrun','clang++','-arch','arm64','-std=c++17','-O2',
                    '-I'+str(a.remill/'remill/include'),str(repo/'ios/services/memory-test.cpp'),
                    str(repo/'ios/cpu/runtime.cpp'),*map(str,objects),'-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
(a.output/'results.log').write_text(result.stdout)
print(result.stdout,end='')
raise SystemExit(result.returncode)
