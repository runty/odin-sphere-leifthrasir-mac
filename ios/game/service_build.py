"""Shared, cached portable-service compilation for Mac and iOS startup runners."""
import hashlib
from pathlib import Path
import subprocess


def sources(repo):
    return [repo / 'src/trophies' / (name + '.cpp') for name in
            ('trophy_data', 'trophy_key', 'trophy_pack', 'trophy_service', 'trophy_store', 'trophy_view', 'trp_archive')] + [repo / 'ios/services/crypto_commoncrypto.cpp']


def inputs(repo):
    return sources(repo) + list((repo / 'src/trophies').glob('*.h')) + [repo / 'launcher/crypto.h', repo / 'ios/game/service_build.py']


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
            command = ['xcrun', 'clang++', '-target', target, '-isysroot', sdk, '-std=c++17', '-O2',
                       '-I' + str(repo / 'src'), '-I' + str(repo / 'launcher'), '-c', str(path), '-o', str(obj)]
            with obj.with_suffix('.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            stamp.write_text(key)
        objects.append(obj)
    return objects
