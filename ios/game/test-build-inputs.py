#!/usr/bin/env python3
"""Check real LLVM archive/link consumers with ordered and quoted object paths."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from build_inputs import response_inputs

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();source=Path(__file__).resolve().parent
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new mounted external output directory')
out.mkdir(parents=True);llvm=Path('/opt/homebrew/opt/llvm/bin');objects=[]
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
sources={str(path):digest(path) for path in [source/'build_inputs.py',Path(__file__)]}
def run(label,args):
 with (out/(label+'.log')).open('w') as stream:subprocess.run(list(map(str,args)),stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=30)
for n,name in enumerate(['plain','space name',"quote'and$dollar"]):
 src=out/(name+'.c');obj=out/(name+'.o');src.write_text('int owned_'+str(n)+'(void){return '+str(n+1)+';}\n')
 run('compile-'+str(n),['xcrun','clang','-c',src,'-o',obj]);objects.append(obj)
response=response_inputs(out/'object inputs.rsp',objects)
run('inline-archive',[llvm/'llvm-ar','rcs',out/'inline.a',*objects])
run('response-archive',[llvm/'llvm-ar','rcs',out/'response.a',response])
assert (out/'inline.a').read_bytes()==(out/'response.a').read_bytes()
members=subprocess.check_output([llvm/'llvm-ar','t',out/'response.a'],text=True).splitlines()
# Darwin's symbol-table member may be listed before the actual objects.
assert [m for m in members if not m.startswith('__.SYMDEF')]==[obj.name for obj in objects]
main=out/'main.c';main.write_text('int owned_0(void),owned_1(void),owned_2(void);int main(void){return owned_0()+owned_1()+owned_2()!=6;}\n')
run('inline-link',['xcrun','clang',main,*objects,'-o',out/'inline-check'])
run('response-link',['xcrun','clang',main,response,'-o',out/'response-check'])
run('inline-execute',[out/'inline-check']);run('response-execute',[out/'response-check'])
assert all(digest(Path(path))==wanted for path,wanted in sources.items())
report=dict(passed=True,archive_bytes_identical=True,member_order_preserved=True,quoted_paths=True,both_executables_passed=True,sources=sources,archive_sha256=digest(out/'response.a'),scope='LLVM archive/link argument transport; no guest execution')
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
