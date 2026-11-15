#!/usr/bin/env python3
"""Read-only qualification of original libc exports used by native bulk adapters."""
import argparse,hashlib,json,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];sys.path.insert(0,str(repo/'scripts'));from ps4_module import read_module,nid
root=a.root.resolve();sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
layout=json.loads((root/'image.json').read_text());assert sha(root/'image.bin')==layout['image_sha256']=='51a38d7dc7476f0163c27273eccd0c37b4861e53b17a5e48b7e1234749602f22'
path=root/'App0/sce_module/libc.prx';module=read_module(path);assert module['sha256']=='e436eb5bd6bc6dd706edea917da1e52460ce760971b8f067149f479fd3410bfe'
base=layout['base']+layout['modules'][0][0];assert base==0x10ca0000
image=(root/'image.bin').read_bytes();proof={}
for name,pc in [('memcpy',0x10cd0ba0),('memmove',0x10cd0c40),('memset',0x10cd0ce0)]:
    rows=[s for s in module['symbols'] if s['identity'] and s['identity'][0]==nid(name) and s['section'] and s['type']==2 and s['binding']==1]
    assert len(rows)==1;s=rows[0];assert base+s['value']==pc and s['size']>0
    segments=[p for p in module['ph'] if p['type']==1 and p['flags']&1 and p['vaddr']<=s['value'] and s['size']<=p['filesz']-(s['value']-p['vaddr'])]
    assert len(segments)==1;segment=segments[0];off=segment['offset']+s['value']-segment['vaddr'];raw=module['elf'][off:off+s['size']];assert len(raw)==s['size'] and raw==image[pc-layout['base']:pc-layout['base']+s['size']]
    proof[name]={'pc':hex(pc),'size':s['size'],'original_bytes_sha256':hashlib.sha256(raw).hexdigest(),'export_identity':s['identity']}
report={'passed':True,'scope':'original metadata, executable span and prepared byte identity; no dynamic reachability claim','module_sha256':module['sha256'],'prepared_image_sha256':layout['image_sha256'],'exports':proof,'sources':{str(x.relative_to(repo)):sha(x) for x in [Path(__file__),repo/'scripts/ps4_module.py']}}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
