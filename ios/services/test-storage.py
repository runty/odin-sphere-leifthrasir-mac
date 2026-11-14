#!/usr/bin/env python3
"""Check native/checked-guest storage across fresh processes and failed writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--remill',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Use a mounted external build directory')
out.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[2]
fixture=Path(tempfile.mkdtemp(prefix='profiles-',dir=out))
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
c_sources=[repo/name for name in ('src/runtime_file.c','src/runtime_savedata.c','src/runtime_errno.c','ios/services/memory_shim.c')]
def command(name,args):
    with (out/(name+'.log')).open('w') as log:
        subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
def hashes(root):
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
results={}
for arch in ('arm64','x86_64'):
    build=out/arch;build.mkdir(exist_ok=True);objects=[]
    for source in c_sources:
        obj=build/(source.stem+'.o')
        command(arch+'-'+source.stem,['xcrun','clang','-arch',arch,'-std=gnu11','-O2','-DOSL_AOT_CPU=1','-c',source,'-o',obj])
        objects.append(obj)
    native=build/'storage-check'
    command(arch+'-link',['xcrun','clang++','-arch',arch,'-std=c++17','-O2',repo/'ios/services/storage.cpp',repo/'ios/services/storage-test.cpp',*objects,'-o',native])
    profile=fixture/arch
    command(arch+'-create',[native,profile,'create'])
    saved=profile/'User/savedata/1/CUSA05083/TEST/save.dat'
    expected=bytearray((i*17+23)&255 for i in range(1156096));expected[12345:12350]=b'PATCH'
    assert saved.read_bytes()==expected
    assert (profile/'App0/asset.bin').read_bytes()==b'immutable asset'
    before=hashes(profile)
    for index,phase in enumerate(('verify','failure','verify')):
        command(arch+'-'+phase+'-'+str(index),[native,profile,phase]);assert hashes(profile)==before
    meta=profile/'User/savedata/1/CUSA05083/TEST.sce_sys'
    os.chmod(meta,0o500)
    try: command(arch+'-permission',[native,profile,'permission'])
    finally: os.chmod(meta,0o700)
    assert hashes(profile)==before and not list(profile.rglob('*.tmp-*'))
    command(arch+'-delete',[native,profile,'delete'])
    assert not saved.exists() and not meta.exists()
    results[arch]={'native_cross_process_persistence':True,'failed_write_preserves_files':True,'readonly_mount_refuses_writes':True,'permission_failure_reported':True,'fixture_save_sha256':hashlib.sha256(expected).hexdigest()}
    if arch=='arm64':
        guest=build/'guest-storage-check'
        command('guest-link',['xcrun','clang++','-arch',arch,'-std=c++17','-O2','-I'+str(a.remill/'remill/include'),repo/'ios/cpu/runtime.cpp',repo/'ios/services/storage.cpp',repo/'ios/services/guest-storage-test.cpp',*objects,'-o',guest])
        profile=fixture/'guest'
        command('guest-create',[guest,profile,'create']);before=hashes(profile)
        assert (profile/'User/savedata/1/CUSA05083/TEST/guest.bin').read_bytes()==b'guest storage bytes'
        command('guest-verify',[guest,profile,'verify']);assert hashes(profile)==before
        results['guest']={'fresh_process_and_changed_guest_addresses':True,'checked_nested_pointers_and_canaries':True,'readonly_input_protection':True,'serialized_guest_pointers_preserved':True}
results['game_save_verified']=False
(out/'verification.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
