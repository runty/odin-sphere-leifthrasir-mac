#!/usr/bin/env python3
"""Check exact lookup semantics for the private generated corpus, plus a microbenchmark."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();source=Path(__file__).resolve().parent
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new mounted external output directory')
header=a.root/'traces.h';data=header.read_bytes();rows=re.findall(r'\{([0-9]+),&sub_([0-9a-f]+)\}',data.decode())
if not rows or any(int(pc)!=int(symbol,16) for pc,symbol in rows):p.error('Malformed generated trace entry')
addresses=[int(pc) for pc,_ in rows]
if any(a>=b for a,b in zip(addresses,addresses[1:])):p.error('Generated trace table is not strictly increasing')
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):digest(path) for path in [header,source/'trace_lookup.h',source/'trace-lookup-test.cpp',Path(__file__)]}
out.mkdir(parents=True);(out/'addresses.txt').write_text(''.join(f'{pc:x}\n' for pc in addresses))
with (out/'compile.log').open('w') as log:subprocess.run(['xcrun','clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source/'trace-lookup-test.cpp'),'-o',str(out/'check')],stdout=log,stderr=subprocess.STDOUT,check=True)
r=subprocess.run([out/'check',out/'addresses.txt'],capture_output=True,text=True,check=True,timeout=20)
(out/'run.log').write_text(r.stdout+r.stderr);report=json.loads(r.stdout)
assert all(digest(Path(path))==wanted for path,wanted in inputs.items()),'Input changed during check'
report.update(passed=True,inputs=inputs,binary_sha256=digest(out/'check'),performance_scope='lookup-only microbenchmark; no gameplay or frame-rate claim')
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
