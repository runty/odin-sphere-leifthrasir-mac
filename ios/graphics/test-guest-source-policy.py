#!/usr/bin/env python3
"""Exercise the actual checkpoint/candidate source gate without building an app."""
import ast,hashlib,tempfile
from pathlib import Path

builder=Path(__file__).with_name('build-guest-session.py')
node=next(n for n in ast.parse(builder.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='check_checkpoint_sources')
ns={'digest':lambda p:hashlib.sha256(p.read_bytes()).hexdigest()}
exec(compile(ast.Module(body=[node],type_ignores=[]),str(builder),'exec'),ns)
check=ns['check_checkpoint_sources'];sha=ns['digest']
with tempfile.TemporaryDirectory(prefix='odin-source-policy-') as d:
    root=Path(d);old=root/'old';new=root/'new'
    for base in (old,new):
        (base/'src').mkdir(parents=True);(base/'src/runtime_audio.c').write_text('old audio');(base/'src/other.c').write_text('other')
    inventory={str(p.relative_to(old)):sha(p) for p in old.rglob('*.c')}
    assert check(inventory,old,new,False)=={}
    (new/'src/runtime_audio.c').write_text('new audio')
    try:check(inventory,old,new,False)
    except RuntimeError as e:assert 'outside audio override' in str(e)
    else:raise AssertionError('Unrequested override accepted')
    override=check(inventory,old,new,True)
    assert list(override)==['src/runtime_audio.c'] and override['src/runtime_audio.c']['checkpoint_sha256']==inventory['src/runtime_audio.c'] and override['src/runtime_audio.c']['candidate_sha256']==sha(new/'src/runtime_audio.c')
    (new/'src/other.c').write_text('changed')
    try:check(inventory,old,new,True)
    except RuntimeError as e:assert 'outside audio override' in str(e)
    else:raise AssertionError('Unrelated source change accepted')
    (new/'src/other.c').write_text('other');(old/'src/other.c').write_text('tampered')
    try:check(inventory,old,new,True)
    except RuntimeError as e:assert 'Checkpoint source changed' in str(e)
    else:raise AssertionError('Modified checkpoint source accepted')
    (old/'src/other.c').write_text('other');(new/'src/other.c').unlink()
    try:check(inventory,old,new,True)
    except RuntimeError as e:assert 'Candidate source missing' in str(e)
    else:raise AssertionError('Missing candidate source accepted')
print('GUEST_SOURCE_POLICY: matching, opt-in audio override, and four rejection cases passed')
