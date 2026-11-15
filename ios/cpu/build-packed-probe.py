#!/usr/bin/env python3
"""Package an already-passing synthetic SIMD fixture; never installs or launches."""
import argparse, hashlib, json, re, shutil, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--remill', type=Path, required=True)
p.add_argument('--team', required=True)
p.add_argument('--sdk', choices=['iphoneos', 'iphonesimulator'], default='iphoneos')
a = p.parse_args()
root, fixture, backend = a.root.resolve(), a.fixture.resolve(), a.remill.resolve()
cpu = Path(__file__).resolve().parent
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount() or root.exists():
    p.error('Use a new directory on a mounted external disk')
if not re.fullmatch(r'[A-Z0-9]{10}', a.team): p.error('Invalid development team')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = fixture / 'verification.json'
report = json.loads(manifest.read_text())
if not report['passed'] or report['failed'] or not report['shared_backend_and_runtime_unchanged']:
    p.error('Fixture has not passed host comparison')
if (report['routines'], report['vectors']) != (44, 9280): p.error('Unexpected fixture dimensions')
pins = {Path(name): digest for name, digest in report['sources'].items()}
for name, digest in report['generated_artifacts'].items():
    if Path(name).name != name: p.error('Invalid fixture artifact path')
    pins[fixture / name] = digest
def check():
    if any(sha(path) != digest for path, digest in pins.items()):
        raise RuntimeError('Fixture/backend/runtime sources changed')
check()
semantics = backend / 'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc'
lifter = backend / 'remill-build/bin/lift/remill-lift-23'
for path in [semantics, lifter, cpu / 'runtime.cpp', cpu / 'runtime.h']:
    if path not in pins: p.error('Backend or runtime differs from fixture')
root.mkdir(); objects = root / 'Objects'; objects.mkdir(); source = root / 'Source'; source.mkdir()
def run(name, command):
    with (root / (name + '.log')).open('w') as log:
        subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT, check=True)
sdk = subprocess.check_output(['xcrun', '--sdk', a.sdk, '--show-sdk-path'], text=True).strip()
target = 'arm64-apple-ios18.0' + ('-simulator' if a.sdk == 'iphonesimulator' else '')
common = ['-target', target, '-isysroot', sdk, '-O2']
archive_objects = []
for ll in sorted(fixture.glob('*.ll')):
    if ll not in pins: p.error('Unqualified LLVM file in fixture')
    obj = objects / (ll.stem + '.o'); archive_objects.append(obj)
    run(ll.stem, ['/opt/homebrew/opt/llvm/bin/clang', *common, '-Wno-override-module', '-c', ll, '-o', obj])
if len(archive_objects) != report['routines']: p.error('Missing qualified routines')
for name, path in [('verify', fixture / 'review.cpp'), ('runtime', cpu / 'runtime.cpp')]:
    obj = objects / (name + '.o'); archive_objects.append(obj)
    run(name, ['xcrun', 'clang++', *common, '-std=c++20', '-DOSL_PACKED_NO_CLI', '-I' + str(cpu), '-I' + str(fixture), '-I' + str(backend / 'remill/include'), '-c', path, '-o', obj])
obj = objects / 'oracle.o'; archive_objects.append(obj)
run('oracle', ['xcrun', 'clang', *common, '-c', fixture / 'expected.s', '-o', obj])
run('archive', ['xcrun', 'libtool', '-static', '-o', source / 'libpacked.a', *archive_objects])
shutil.copy2(cpu / 'packed-probe.mm', source / 'main.mm')
pins[cpu / 'packed-probe.mm'] = sha(cpu / 'packed-probe.mm')
project = {'name': 'OdinPackedCPU', 'options': {'deploymentTarget': {'iOS': '18.0'}}, 'targets': {
    'OdinPackedCPU': {'type': 'application', 'platform': 'iOS',
        'sources': [{'path': 'Source', 'excludes': ['*.a']}],
        'dependencies': [{'sdk': 'UIKit.framework'}, {'framework': 'Source/libpacked.a', 'embed': False}],
        'settings': {'base': {'PRODUCT_BUNDLE_IDENTIFIER': 'com.wadscorp.odinsphere.packedcpu',
            'DEVELOPMENT_TEAM': a.team, 'CODE_SIGN_STYLE': 'Automatic', 'ARCHS': 'arm64',
            'TARGETED_DEVICE_FAMILY': '1,2', 'CLANG_ENABLE_OBJC_ARC': True,
            'LIBRARY_SEARCH_PATHS': '$(PROJECT_DIR)/Source',
            'CLANG_CXX_LANGUAGE_STANDARD': 'c++20', 'CURRENT_PROJECT_VERSION': 1, 'MARKETING_VERSION': '0.1'}},
        'info': {'path': 'Info.plist', 'properties': {
            'CFBundleDisplayName': 'Odin CPU Comparison', 'UILaunchScreen': {},
            'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False},
            'UIFileSharingEnabled': True, 'LSSupportsOpeningDocumentsInPlace': True,
            'OSLRoutines': report['routines'], 'OSLVectors': report['vectors'],
            'OSLOracleSHA': report['oracle_sha256'], 'OSLSemanticsSHA': pins[semantics],
            'OSLLifterSHA': pins[lifter], 'OSLFixtureSHA': sha(manifest),
            'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait', 'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight']}}}}}
(root / 'project.json').write_text(json.dumps(project, indent=2) + '\n')
run('generate', ['xcodegen', 'generate', '--spec', root / 'project.json', '--project', root])
run('build', ['xcodebuild', '-project', root / 'OdinPackedCPU.xcodeproj', '-scheme', 'OdinPackedCPU', '-configuration', 'Release', '-sdk', a.sdk, '-destination', 'generic/platform=iOS' + (' Simulator' if a.sdk == 'iphonesimulator' else ''), '-derivedDataPath', root / 'DerivedData', '-allowProvisioningUpdates', 'build'])
app = root / 'DerivedData/Build/Products' / ('Release-' + a.sdk) / 'OdinPackedCPU.app'
run('codesign', ['codesign', '--verify', '--deep', '--strict', app])
check()
(root / 'manifest.json').write_text(json.dumps({'built': True, 'sdk': a.sdk, 'app': str(app), 'fixture_manifest_sha256': sha(manifest), 'sources': {str(path): digest for path, digest in pins.items()}, 'builder_sha256': sha(Path(__file__)), 'executable_sha256': sha(app / 'OdinPackedCPU'), 'game_booted': False, 'device_contacted': False}, indent=2) + '\n')
print(json.dumps({'built': True, 'app': str(app), 'device_contacted': False}))
