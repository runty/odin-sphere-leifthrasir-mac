#!/usr/bin/env python3
"""Verify metadata-cache invalidation against the actual guard transform."""
import argparse,hashlib,json
from pathlib import Path
from guard_ir import instrument
from trace_preparation import TracePreparationCache
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new mounted external output directory')
out.mkdir(parents=True);output=out/'guarded.ll';checks=0
def require(value):
 global checks
 assert value,'trace preparation check '+str(checks+1)
 checks+=1
raw='''define ptr @sub_1000(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {
entry:
  br label %loop
loop:
  br i1 true, label %loop, label %done
done:
  ret ptr %memory
}
declare ptr @sub_2000(ptr, i64, ptr)
'''
cache=TracePreparationCache();expected,count=instrument(raw)
analysis=cache.analyze(raw)
require(not analysis.extended_precision and not analysis.remill_error and analysis.definitions==('1000',))
require(cache.analyze(raw)==analysis and cache.analysis_hits==1 and cache.analysis_misses==1)
x87=raw.replace('  ret ptr %memory','  %value = load x86_fp80, ptr %state\n  ret ptr %memory')
fallback=raw.replace('  ret ptr %memory','  call void @__remill_error()\n  ret ptr %memory')
require(cache.analyze(x87).extended_precision and not cache.analyze(x87).remill_error)
require(cache.analyze(fallback).remill_error and not cache.analyze(fallback).extended_precision)
require(not cache.analyze(raw+'declare void @__remill_error()\n').remill_error)
# Keep lexical shortcuts from changing guard placement or malformed-IR refusal.
tabbed=raw.replace('  br label %loop','\tbr label %loop')
require(instrument(tabbed)[1]==2)
ordinary=raw.replace('entry:\n','entry:\n  %branch_value = add i32 1, 2\n  ; switch is a comment\n')
require(instrument(ordinary)[1]==2)
header='define ptr @sub_1000(ptr noalias %state, i64 %program_counter, ptr noalias %memory) {\n'
switch=header+"entry:\n  switch i32 0, label %done [\n    i32 1, label %entry\n  ]\ndone:\n  ret ptr %memory\n}\n"
indirect=header+"entry:\n  indirectbr ptr blockaddress(@sub_1000, %entry), [label %entry, label %done]\ndone:\n  ret ptr %memory\n}\n"
for text in [switch,indirect]:
 guarded,n=instrument(text)
 require(n==2 and guarded.count('call void @__osl_guest_tick(ptr %memory, ptr %state)')==2)
for bad_text in [raw.replace('label %loop','label %absent'),switch.replace('  ]\n',''),indirect.replace('label %entry','label %absent')]:
 try:instrument(bad_text)
 except ValueError:require(True)
 else:raise AssertionError('Malformed LLVM branch was accepted')
prepared=cache.prepare(raw,output)
require(output.read_text()==expected and prepared.guards_inserted==count and count==2)
require(prepared.definitions==('1000',) and prepared.declarations==('2000',))
require(prepared.raw_sha256==hashlib.sha256(raw.encode()).hexdigest() and prepared.guarded_sha256==hashlib.sha256(expected.encode()).hexdigest())
mtime=output.stat().st_mtime_ns
require(cache.prepare(raw,output)==prepared and cache.hits==1 and cache.misses==1 and output.stat().st_mtime_ns==mtime)
output.write_text('corrupted guarded output\n')
require(cache.prepare(raw,output)==prepared and output.read_text()==expected and cache.misses==2)
output.unlink();require(cache.prepare(raw,output)==prepared and output.read_text()==expected and cache.misses==3)
changed=raw.replace('sub_1000','sub_1001');new=cache.prepare(changed,output)
require(new.definitions==('1001',) and new.guarded_sha256!=prepared.guarded_sha256 and cache.misses==4)
require(output.read_text()==instrument(changed)[0])
before=output.read_bytes();bad=changed.replace('ptr noalias %state','ptr %state')
try:cache.prepare(bad,output)
except ValueError:pass
else:raise AssertionError('Malformed ABI was cached')
require(output.read_bytes()==before and cache.misses==4)
try:cache.prepare(instrument(changed)[0],output)
except ValueError:pass
else:raise AssertionError('Already guarded input was cached')
require(output.read_bytes()==before)
# A new process/cache never trusts an existing output without recomputation.
second=TracePreparationCache();mtime=output.stat().st_mtime_ns
require(second.prepare(changed,output)==new and second.misses==1 and second.hits==0 and output.stat().st_mtime_ns==mtime)
image='51a38d7dc7476f0163c27273eccd0c37b4861e53b17a5e48b7e1234749602f22'
second.libc_helpers=True;second.image_sha256=image
with_helpers=second.prepare(changed,output)
require(second.misses==2 and with_helpers.guarded_sha256!=new.guarded_sha256 and output.read_text()==instrument(changed,libc_helpers=True,image_sha256=image)[0])
second.image_sha256='0'*64;before=output.read_bytes()
try:second.prepare(changed,output)
except ValueError:pass
else:raise AssertionError('Wrong-image helper mode was reused')
require(output.read_bytes()==before and second.misses==2)
sources={name:hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest() for name in ['guard_ir.py','trace_preparation.py','test-trace-preparation.py']}
report={'trace_preparation_checks':checks,'raw_output_mode_invalidation':True,'metadata_only_in_process':True,'guard_transform_changed':False,'sources':sources,'game_frames':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
