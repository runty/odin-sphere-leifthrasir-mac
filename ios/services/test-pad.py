#!/usr/bin/env python3
"""Exercise console pad state and real GameController snapshot mapping, without UI."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'game'))
from service_build import compile_services, link_options
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.output.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
root.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[2]
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
objects=compile_services(repo,root/'objects','arm64-apple-macos15',sdk)
with (root/'build.log').open('w') as log:
    subprocess.run(['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-fobjc-arc',
                    str(repo/'ios/services/pad-test.mm'),*map(str,objects),*link_options(),'-o',str(root/'pad-check')],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
result=subprocess.run([str(root/'pad-check')],text=True,capture_output=True,timeout=20)
(root/'results.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='');raise SystemExit(result.returncode)
