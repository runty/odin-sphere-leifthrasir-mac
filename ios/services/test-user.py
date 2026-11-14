#!/usr/bin/env python3
"""Compile/stress the shared user service independently of the guest/GPU."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.output.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
root.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[2]
with (root/'build.log').open('w') as log:
    subprocess.run(['xcrun','clang','-arch','arm64','-std=gnu11','-O2','-DOSL_AOT_CPU=1',
                    str(repo/'src/runtime_user.c'),str(repo/'ios/services/memory_shim.c'),
                    str(repo/'ios/services/user-test.c'),'-o',str(root/'user-check')],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
result=subprocess.run([str(root/'user-check')],text=True,capture_output=True,timeout=20)
(root/'results.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='');raise SystemExit(result.returncode)
