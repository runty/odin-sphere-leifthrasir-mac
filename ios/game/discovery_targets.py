"""Select only reported, executed missing-block PCs from all guest CPUs."""

def missing_targets(report,base,segments,known):
    records=[report]
    workers=report.get('worker_reports',[])
    if not isinstance(workers,list) or len(workers)>4096:raise ValueError('Invalid worker report list')
    records+=workers
    targets=set()
    for record in records:
        if not isinstance(record,dict):raise ValueError('Invalid worker report')
        if record.get('error')!='missing translated block':continue
        pc=record.get('missing_pc')
        if isinstance(pc,bool) or not isinstance(pc,int) or not 0<=pc<1<<64:raise ValueError('Invalid missing PC')
        offset=pc-base
        if not any(flags&1 and start<=offset<start+size for start,size,flags in segments):
            raise ValueError('Requested trace is outside executable guest memory')
        if pc in known:raise ValueError('A known trace was not dispatched')
        targets.add(pc)
    return sorted(targets)
