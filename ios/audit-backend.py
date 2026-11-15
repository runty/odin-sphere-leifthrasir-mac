#!/usr/bin/env python3
"""Audit the complete pinned Remill patch set without changing its working tree."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
PIN='66c488c9911fd9b368e4d0c9647d74239f8bba8b'
PATCHES=('llvm23','lifter-lifetime','packed-simd','pcmpistrm','vpermilps','rcpps','blendps','shufps','vpermilpd','rsqrtps','double-select','insertps','movemask','sse41-rounding','startup-lane-insert','avx-extract','startup-variable-blend','startup-permute128','startup-dword-shifts','cmpxchg8b')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend',type=Path,required=True);a=p.parse_args();backend=a.backend.resolve();source=Path(__file__).resolve().parent
if len(backend.parts)<3 or backend.parts[1]!='Volumes' or not Path('/Volumes',backend.parts[2]).is_mount():p.error('Use an isolated backend on a mounted external disk')
def git(*args,env=None):return subprocess.check_output(['git','-C',str(backend),*args],env=env)
if git('rev-parse','HEAD').decode().strip()!=PIN:p.error('Unexpected backend revision; left untouched')
if git('ls-files','--others','--exclude-standard').strip():p.error('Unexpected untracked backend files; left untouched')
with tempfile.TemporaryDirectory(prefix='patch-audit-',dir=backend.parent) as directory:
 env=dict(os.environ,GIT_INDEX_FILE=str(Path(directory)/'index'))
 git('read-tree','HEAD',env=env)
 for name in PATCHES:git('apply','--cached',str(source/('remill-'+name+'.patch')),env=env)
 expected=git('diff','--cached','--binary','HEAD',env=env)
actual=git('diff','--binary','HEAD')
if actual!=expected:p.error('Backend edits differ from the complete patch set; left untouched')
print(json.dumps({'passed':True,'base_commit':PIN,'patches':list(PATCHES),'source_diff_sha256':hashlib.sha256(actual).hexdigest()}))
