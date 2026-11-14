"""Shared, cached portable-service compilation for Mac and iOS startup runners."""
import hashlib
from pathlib import Path
import subprocess


def sources(repo):
    return [repo / 'src/trophies' / (name + '.cpp') for name in
            ('trophy_data', 'trophy_key', 'trophy_pack', 'trophy_service', 'trophy_store', 'trophy_view', 'trp_archive')] + [repo / 'ios/services' / name for name in ('crypto_commoncrypto.cpp', 'memory.cpp', 'memory_shim.c', 'pad.mm', 'storage.cpp')] + [repo / 'src' / name for name in ('runtime_memory.c', 'runtime_user.c', 'runtime_errno.c', 'runtime_file.c', 'runtime_savedata.c')]


def inputs(repo):
    return sources(repo) + list((repo / 'src/trophies').glob('*.h')) + [repo / name for name in ('launcher/crypto.h', 'src/runtime.h', 'src/runtime_pad_types.h', 'src/runtime_savedata_types.h', 'src/runtime_file_types.h', 'ios/services/memory.h', 'ios/services/user.h', 'ios/services/pad.h', 'ios/services/pad-apple.h', 'ios/services/storage.h', 'ios/services/guest_storage.h', 'ios/services/offline.h', 'ios/game/service_build.py')]


def link_options():
    return ['-framework', 'Foundation', '-framework', 'GameController', '-framework', 'CoreHaptics']


def compile_services(repo, out, target, sdk):
    out.mkdir(parents=True, exist_ok=True)
    headers = b''.join(p.read_bytes() for p in sorted(inputs(repo)))
    common = hashlib.sha256(headers + (target + sdk).encode()).digest()
    objects = []
    for path in sources(repo):
        obj = out / ('service-' + path.stem + '.o')
        key = hashlib.sha256(common + path.read_bytes()).hexdigest()
        stamp = obj.with_suffix('.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text() != key:
            is_c = path.suffix == '.c'
            command = ['xcrun', 'clang' if is_c else 'clang++', '-target', target, '-isysroot', sdk, '-std=gnu11' if is_c else '-std=c++17', '-O2', '-DOSL_AOT_CPU=1',
                       '-I' + str(repo / 'src'), '-I' + str(repo / 'launcher'), '-c', str(path), '-o', str(obj)]
            if path.suffix == '.mm': command.append('-fobjc-arc')
            with obj.with_suffix('.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            stamp.write_text(key)
        objects.append(obj)
    return objects
