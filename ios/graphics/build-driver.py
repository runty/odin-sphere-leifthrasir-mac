#!/usr/bin/env python3
"""Build the pinned KosmicKrisp driver for iPhoneOS in an isolated external tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import shutil

PIN = 'b628375fb1fd99476bfbaaa0532deb6fe3a6870e'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--host-tools', required=True, type=Path, help='Matching Mesa build-tools directory; read only')
p.add_argument('--meson', required=True, type=Path)
p.add_argument('--host-runtime', type=Path, help='Private runtime-library directory for the native shader tools')
p.add_argument('--sdk', choices=('iphoneos', 'iphonesimulator', 'macosx'), default='iphoneos')
p.add_argument('--jobs', type=int, default=8)
p.add_argument('--configure-only', action='store_true')
a = p.parse_args()
root = a.root.resolve()
if len(root.parts) < 3 or root.parts[1] != 'Volumes' or not Path('/Volumes', root.parts[2]).is_mount():
    p.error('Driver build requires a mounted external disk')
if a.jobs < 1 or a.jobs > 32:
    p.error('Use 1 to 32 build jobs')
root.mkdir(parents=True, exist_ok=True)
source = root / 'mesa'
patches = [Path(__file__).with_name(name) for name in ('mesa-performance.patch', 'mesa-ios.patch')]

def run(name, *args, env=None):
    with (root / (name + '.log')).open('w') as out:
        subprocess.run([str(x) for x in args], stdout=out, stderr=subprocess.STDOUT, env=env, check=True)

if not (source / '.git').exists():
    run('source-init', 'git', 'init', source)
    run('source-fetch', 'git', '-C', source, 'fetch', '--depth', '1', 'https://github.com/shadexternals/mesa.git', PIN)
    run('source-checkout', 'git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD')
if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
    p.error('Unexpected Mesa revision; leaving source untouched')
def patch_diff(selected):
    with tempfile.TemporaryDirectory(prefix='patch-audit-', dir=root) as temporary:
        git_env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / 'index'))
        subprocess.run(['git', '-C', str(source), 'read-tree', 'HEAD'], env=git_env, check=True)
        for patch in selected:
            subprocess.run(['git', '-C', str(source), 'apply', '--cached', str(patch)], env=git_env, check=True)
        return subprocess.check_output(['git', '-C', str(source), 'diff', '--cached', '--binary', 'HEAD'], env=git_env)

present = [patch for patch in patches if subprocess.run(['git', '-C', str(source), 'apply', '--reverse', '--check', str(patch)], capture_output=True).returncode == 0]
actual = subprocess.check_output(['git', '-C', str(source), 'diff', '--binary', 'HEAD'])
if actual != patch_diff(present):
    p.error('Unexpected Mesa edits; leaving source untouched')
for patch in patches:
    if patch not in present:
        run('apply-' + patch.stem, 'git', '-C', source, 'apply', patch)
expected_diff = subprocess.check_output(['git', '-C', str(source), 'diff', '--binary', 'HEAD'])
if expected_diff != patch_diff(patches):
    p.error('Mesa patch audit failed')
# The read-only native tools must be built from the same Mesa revision.
commands = json.loads((a.host_tools / 'compile_commands.json').read_text())
cc = next(item for item in commands if item['file'].endswith('/kk_clc.c'))
host_source = (Path(cc['directory']) / cc['file']).resolve()
host_repo = next(parent for parent in host_source.parents if (parent / '.git').exists())
if subprocess.check_output(['git', '-C', str(host_repo), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
    p.error('Host shader tools use a different Mesa revision')
tools = [a.host_tools / 'src/compiler/clc/mesa_clc', a.host_tools / 'src/compiler/spirv/vtn_bindgen2',
         a.host_tools / 'src/kosmickrisp/clc/kk_clc']
if not all(tool.is_file() for tool in tools):
    p.error('Missing native shader tools')
original_tools = tools
if a.host_runtime:
    private_tools = root / 'host-tools/bin'
    private_tools.mkdir(parents=True, exist_ok=True)
    tools = []
    for tool in original_tools:
        copy = private_tools / tool.name
        shutil.copy2(tool, copy)
        run('relocate-' + tool.name, 'install_name_tool', '-change',
            '/opt/homebrew/opt/spirv-llvm-translator/lib/libLLVMSPIRVLib.23.1.dylib',
            a.host_runtime.resolve() / 'libLLVMSPIRVLib.23.1.dylib', copy)
        run('sign-' + tool.name, 'codesign', '--force', '--sign', '-', copy)
        tools.append(copy)
sdk = subprocess.check_output(['xcrun', '--sdk', a.sdk, '--show-sdk-path'], text=True).strip()
target = 'arm64-apple-macos27.2' if a.sdk == 'macosx' else 'arm64-apple-ios27.2' + ('-simulator' if a.sdk == 'iphonesimulator' else '')
flags = ['-target', target, '-isysroot', sdk]
probe = subprocess.run(['/usr/bin/clang', *flags, '-x', 'objective-c', '-fsyntax-only', '-'],
                       input='#import <Metal/Metal.h>\nid<MTL4CommandEncoder> encoder;\n',
                       capture_output=True, text=True)
(root / 'metal4-headers.log').write_text(probe.stdout + probe.stderr)
if probe.returncode:
    p.error('This SDK does not expose the required Metal 4 encoder protocols')
cross = root / (a.sdk + '.ini')
cross.write_text("[binaries]\nc = '/usr/bin/clang'\ncpp = '/usr/bin/clang++'\nobjc = '/usr/bin/clang'\nobjcpp = '/usr/bin/clang++'\nar = '/usr/bin/ar'\nstrip = '/usr/bin/strip'\npkg-config = '/usr/bin/false'\n\n[properties]\nneeds_exe_wrapper = true\n\n[host_machine]\nsystem = 'darwin'\ncpu_family = 'aarch64'\ncpu = 'aarch64'\nendian = 'little'\n\n[built-in options]\n" + ''.join(name + ' = ' + repr(flags) + '\n' for name in ('c_args', 'cpp_args', 'objc_args', 'objcpp_args', 'c_link_args', 'cpp_link_args', 'objc_link_args', 'objcpp_link_args')))
build = root / ('build-' + a.sdk)
env = dict(os.environ, PATH=os.pathsep.join([str(a.meson.parent), *[str(t.parent) for t in tools], os.environ['PATH']]))
if not (build / 'build.ninja').exists():
    run('configure', a.meson, 'setup', build, source, '--cross-file=' + str(cross), '--buildtype=release', '--prefer-static',
        '-Dplatforms=macos', '-Dvulkan-drivers=kosmickrisp', '-Dgallium-drivers=', '-Dopengl=false', '-Dzstd=disabled',
        '-Dmesa-clc=system', '-Dprecomp-compiler=system', '-Dspirv-tools=disabled', env=env)
else:
    run('reconfigure', a.meson, 'setup', '--reconfigure', '--clearcache', build, source, env=env)
manifest = {'mesa_commit': PIN, 'source_diff_sha256': hashlib.sha256(expected_diff).hexdigest(),
            'sdk': sdk, 'target': target, 'platform': a.sdk, 'cross_file_sha256': hashlib.sha256(cross.read_bytes()).hexdigest(),
            'original_host_tools': {str(t): hashlib.sha256(t.read_bytes()).hexdigest() for t in original_tools},
            'host_tools': {str(t): hashlib.sha256(t.read_bytes()).hexdigest() for t in tools}, 'host_runtime': ({'directory': str(a.host_runtime.resolve()), 'libraries': {x.name: hashlib.sha256(x.read_bytes()).hexdigest() for x in a.host_runtime.glob('*.dylib') if not x.is_symlink()}} if a.host_runtime else None), 'device_tested': False}
(root / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
if not a.configure_only:
    run('build', a.meson, 'compile', '-C', build, '-j', a.jobs, 'vulkan_kosmickrisp', 'kosmickrisp_mesa_icd', env=env)
    driver = build / 'src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib'
    run('platform-audit', 'xcrun', 'vtool', '-show-build', driver)
    platform = (root / 'platform-audit.log').read_text()
    wanted_platform = {'iphoneos': 'platform IOS\n', 'iphonesimulator': 'platform IOSSIMULATOR', 'macosx': 'platform MACOS'}[a.sdk]
    if wanted_platform not in platform or 'minos 27.2' not in platform:
        p.error('Driver platform/minimum-OS audit failed')
    if subprocess.check_output(['lipo', '-archs', str(driver)], text=True).strip() != 'arm64':
        p.error('Driver must contain only arm64')
    symbols = subprocess.check_output(['nm', '-m', '-u', str(driver)], text=True)
    if any('dynamically looked up' in line and 'weak' not in line for line in symbols.splitlines()):
        p.error('Driver has unresolved strong symbols')
    dependencies = subprocess.check_output(['otool', '-L', str(driver)], text=True)
    (root / 'dependency-audit.log').write_text(dependencies)
    for line in dependencies.splitlines()[1:]:
        path = line.strip().split(' (')[0]
        if not path.startswith(('/usr/lib/', '/System/Library/Frameworks/', '@rpath/libvulkan_kosmickrisp.dylib')):
            p.error('Unexpected target runtime dependency: ' + path)
    manifest['driver_sha256'] = hashlib.sha256(driver.read_bytes()).hexdigest()
    (root / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
