#!/usr/bin/env python3
"""Check exact range clipping, fallback, input hashes and malformed range rejection."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from function_bounds import FunctionBounds

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():
    p.error('Use a NEW mounted external directory')
out.mkdir(parents=True);root=out/'owned';(root/'App0/sce_module').mkdir(parents=True)
def digest(data):return hashlib.sha256(data).hexdigest()
base=0x10000000
layout={'image_sha256':digest(bytes(256)),'base':base,'segments':[[0,128,5],[128,64,5],[192,64,5]]}
(root/'image.json').write_text(json.dumps(layout))
modules=[]
for i,name in enumerate(['eboot.bin','sce_module/libc.prx','sce_module/libSceFios2.prx']):
    data=('owned module '+str(i)).encode();(root/'App0'/name).write_bytes(data)
    modules.append({'path':name,'self_sha256':digest(data),'unwind_bytes_match':True,'entries':1})
names=['ios/game/unwind_reader.py','ios/game/build-function-bounds.py','scripts/ps4_module.py']
value={'schema':1,'image_sha256':layout['image_sha256'],'layout_sha256':digest((root/'image.json').read_bytes()),
       'sources':{name:digest((repo/name).read_bytes()) for name in names},'modules':modules,
       'entries':[[base+16,base+32],[base+136,base+144],[base+200,base+208]]}
path=out/'bounds.json';path.write_text(json.dumps(value));b=FunctionBounds(path,root,layout,repo)
checks=0
def check(okay):
    global checks
    assert okay;checks+=1
for pc,maximum,expected in [(base+16,65536,16),(base+17,65536,15),(base+31,100,1),
                            (base+16,4,4),(base+32,99,99),(base,99,99),
                            (base+135,99,99),(base+136,99,8),(base+208,99,99)]:
    check(b.length(pc,maximum)==expected)
b.verify_current();check(True)
def reject(label,edit):
    global checks
    bad=copy.deepcopy(value);edit(bad);path.write_text(json.dumps(bad))
    try:FunctionBounds(path,root,layout,repo)
    except (ValueError,TypeError,KeyError):checks+=1;return
    raise AssertionError('Malformed bounds accepted: '+label)
for label,field,new in [('schema','schema',2),('image','image_sha256','bad'),('layout','layout_sha256','bad'),
                        ('empty','entries',[]),('overlap','entries',[[base+16,base+40],[base+32,base+48],[base+200,base+208]]),
                        ('unsorted','entries',list(reversed(value['entries']))),
                        ('bool','entries',[[True,base+32]]),('zero','entries',[[base+32,base+32]]),
                        ('outside','entries',[[base+255,base+257]]),('bad row','entries',[[base+32]])]:
    reject(label,lambda d,f=field,n=new:d.__setitem__(f,n))
reject('source hash',lambda d:d['sources'].__setitem__(names[0],'bad'))
reject('source inventory',lambda d:d['sources'].__setitem__('unexpected','bad'))
reject('module path',lambda d:d['modules'][0].__setitem__('path','../outside'))
reject('module hash',lambda d:d['modules'][0].__setitem__('self_sha256','bad'))
reject('module mismatch',lambda d:d['modules'][0].__setitem__('unwind_bytes_match',False))
reject('count mismatch',lambda d:d['modules'][0].__setitem__('entries',2))
path.write_text(json.dumps(value));b=FunctionBounds(path,root,layout,repo)
(root/'App0/eboot.bin').write_bytes(b'changed')
try:b.verify_current()
except ValueError:checks+=1
else:raise AssertionError('Changed module accepted')
(root/'App0/eboot.bin').write_bytes(b'owned module 0');path.write_text(json.dumps(value)+'\n')
try:b.verify_current()
except ValueError:checks+=1
else:raise AssertionError('Changed manifest accepted')
report={'checks':checks,'owned_fixture_only':True,'game_executed':False,'game_frames':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
