#!/usr/bin/env python3
"""Extract a hash-pinned Homebrew translator bottle into a private external prefix."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

SHA = 'd3b90cbd19c5683973cfb07a885eb6b597da8f504814b016e9514175496a2678'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bottle', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--llvm', type=Path, default=Path('/opt/homebrew/opt/llvm'))
a = p.parse_args()
root = a.output.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Runtime prefix requires a mounted external disk')
if root.exists():
    p.error('Use a new runtime prefix; previous runtime files are preserved')
if hashlib.sha256(a.bottle.read_bytes()).hexdigest() != SHA:
    p.error('Expected spirv-llvm-translator 23.1.3 arm64_golden_gate bottle')
root.mkdir(parents=True)
with tarfile.open(a.bottle) as archive:
    archive.extractall(root, filter='data')
lib = root / 'spirv-llvm-translator/23.1.3/lib/libLLVMSPIRVLib.23.1.dylib'
llvm = a.llvm.resolve() / 'lib/libLLVM.23.1.dylib'
if not llvm.is_file():
    p.error('Matching LLVM 23.1 runtime is unavailable')
subprocess.run(['install_name_tool', '-id', '@rpath/' + lib.name, '-change',
                '@@HOMEBREW_PREFIX@@/opt/llvm/lib/libLLVM.23.1.dylib', str(llvm), str(lib)], check=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(lib)], check=True)
subprocess.run(['codesign', '--verify', '--strict', str(lib)], check=True)
manifest = {'version': '23.1.3', 'bottle_sha256': SHA,
            'bottle_url': 'https://ghcr.io/v2/homebrew/core/spirv-llvm-translator/blobs/sha256:' + SHA,
            'llvm_runtime': str(llvm), 'llvm_runtime_sha256': hashlib.sha256(llvm.read_bytes()).hexdigest(),
            'library': str(lib), 'library_sha256': hashlib.sha256(lib.read_bytes()).hexdigest()}
(root / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
