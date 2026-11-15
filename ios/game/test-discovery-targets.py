#!/usr/bin/env python3
"""Check bounded executed-target selection across parent and worker reports."""
from discovery_targets import deadline_only, missing_targets
checks=0
base=0x10000000;segments=[(0,0x1000,5),(0x2000,0x1000,2)]
def check(report,expected,known=()):
 global checks
 assert missing_targets(report,base,segments,known)==expected;checks+=1
def reject(report,known=()):
 global checks
 try:missing_targets(report,base,segments,known)
 except ValueError:checks+=1;return
 raise AssertionError('Malformed target accepted')
def fault(pc):return {'error':'missing translated block','missing_pc':pc}
check({},[]);check(fault(base+8),[base+8]);check({'error':'missing host service','missing_pc':0x60000000},[])
check({'worker_reports':[fault(base+4),fault(base+8)]},[base+4,base+8])
check(dict(fault(base+4),worker_reports=[fault(base+4)]),[base+4])
check({'error':'guest execution interrupted','worker_reports':[fault(base+8)]},[base+8])
check({'worker_reports':[{'error':'guest thread cancellation requested','missing_pc':base+4}]},[])
for pc in [base-1,base+0x1000,base+0x2000,1<<64,-1,True,'0x10000001',None]:reject(fault(pc))
reject(fault(base+4),{base+4});reject({'worker_reports':[fault(base+8)]},{base+8})
for worker in [None,{},'invalid',[1],[{}]*4097]:reject({'worker_reports':worker})
deadline = {"error": "guest mutex lock exceeded host execution deadline"}
interrupted = {"error": "guest execution interrupted"}
assert deadline_only(deadline)
assert deadline_only(dict(interrupted, worker_reports=[{}, deadline, interrupted]))
for other in [fault(base+8), {"error": "missing host service"}, {"error": "guest memory fault"}]:
    assert not deadline_only(dict(deadline, worker_reports=[other]))
assert not deadline_only(interrupted)
assert not deadline_only({})
print({'target_checks':checks,'deadline_checks':7,'failed':0,'game_frames':False})
