#!/usr/bin/env bash
# Build a private signed app containing the Odin-specific AOT CPU comparisons.
set -euo pipefail
if [[ $# -lt 3 || $# -gt 5 ]]; then
    echo 'Usage: ios/build-probe.sh EXTERNAL_BUILD_DIR DEVELOPMENT_TEAM CPU_FIXTURE_OUTPUT [GAME_STARTUP_OUTPUT [GPU_OUTPUT]]' >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$1
team=$2
fixtures=$3
bundle_id=${OSL_PROBE_BUNDLE_ID:-com.wadscorp.odinsphere.probe}
[[ $bundle_id =~ ^[A-Za-z0-9.-]+$ ]] || { echo 'Invalid probe bundle identifier' >&2; exit 1; }
display_name=$(python3 -c 'import json,os; print(json.dumps(os.environ.get("OSL_PROBE_DISPLAY_NAME","Odin Sphere Probe")))')
probe_sdk=${OSL_PROBE_SDK:-iphoneos}
if [[ $probe_sdk == iphonesimulator ]]; then
    archive_platform=simulator
    destination=generic/platform=iOS\ Simulator
else
    [[ $probe_sdk == iphoneos ]] || { echo "Unsupported probe SDK" >&2; exit 1; }
    archive_platform=ios
    destination=generic/platform=iOS
fi
python3 - "$build_dir" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Build requires a mounted external disk')
PY
mkdir -p "$build_dir/Source"
cp "$source_dir/ios/main.mm" "$build_dir/Source/main.mm"
cp "$source_dir/ios/services/vm_probe.mm" "$build_dir/Source/vm_probe.mm"
# Capability-enabled distribution needs a matching explicit provisioning profile.
# Keep the baseline engineering probe reproducible with the cached wildcard.
entitlements_setting=""
if [[ ${OSL_PROBE_EXTENDED_MEMORY:-0} == 1 ]]; then
    cp "$source_dir/ios/OdinSphere.entitlements" "$build_dir/OdinSphere.entitlements"
    entitlements_setting='        CODE_SIGN_ENTITLEMENTS: OdinSphere.entitlements'
fi
iconset="$build_dir/Source/Assets.xcassets/AppIcon.appiconset"
mkdir -p "$iconset"
sips -z 1024 1024 "$source_dir/packaging/icons/app-icon-256.png" --out "$iconset/Icon.png" >/dev/null
python3 - "$iconset" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1])
(p/'Contents.json').write_text(json.dumps({'images':[{'filename':'Icon.png','idiom':'universal','platform':'ios','size':'1024x1024'}],'info':{'author':'xcode','version':1}})+'\n')
(p.parent/'Contents.json').write_text(json.dumps({'info':{'author':'xcode','version':1}})+'\n')
PY
cp "$fixtures/$archive_platform/libodin-cpu.a" "$build_dir/Source/libodin-cpu.a"
cp "$fixtures/$archive_platform/libodin-runtime.a" "$build_dir/Source/libodin-runtime.a"
game_corpus=none
game_enabled=0
game_dependency=""
if [[ $# -ge 4 ]]; then
    python3 - "$fixtures/manifest.json" "$4/$archive_platform/manifest.json" <<'PY'
import json,sys
cpu,game=[json.load(open(p)) for p in sys.argv[1:]]
if cpu['runtime_sha256']!=game['runtime_sha256']:
    raise SystemExit('CPU and game startup libraries use different checked runtimes; rebuild them together')
backend=game.get('trace_inputs')
if not backend:
    raise SystemExit('Game archive has no backend provenance; rebuild it with ios/game/build-ios.py')
if any(cpu.get(key)!=backend.get(key) for key in ('remill_commit','lifter_sha256','semantics_sha256')):
    raise SystemExit('CPU fixtures and game startup libraries use different backends; rebuild them together')
PY
    cp "$4/$archive_platform/libodin-startup.a" "$build_dir/Source/libodin-startup.a"
    cp "$source_dir/ios/services/memory_probe.mm" "$build_dir/Source/memory_probe.mm"
    cp "$source_dir/ios/services/memory.h" "$build_dir/Source/memory.h"
    game_corpus=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["corpus_sha256"])' "$4/$archive_platform/manifest.json")
    game_enabled=1
    game_dependency=$'      - framework: Source/libodin-startup.a\n        embed: false'
else
    rm -f "$build_dir/Source/libodin-startup.a" "$build_dir/Source/memory_probe.mm" "$build_dir/Source/memory.h"
fi
gpu_enabled=0
gpu_dependency=""
gpu_corpus=none
deployment=18.0
if [[ $# == 5 ]]; then
    [[ -d $5/KosmicKrisp.framework && -f $5/Headers/vulkan/vulkan.h ]] || { echo "Package the GPU driver and headers first" >&2; exit 1; }
    ditto "$5/KosmicKrisp.framework" "$build_dir/Source/KosmicKrisp.framework"
    ditto "$5/Headers" "$build_dir/Source/Headers"
    cp "$source_dir/ios/graphics/probe.mm" "$build_dir/Source/gpu_probe.mm"
    python3 - "$5/inputs.json" "$probe_sdk" <<'PYGPU'
import json,sys
if json.load(open(sys.argv[1])).get('platform','iphoneos')!=sys.argv[2]:
    raise SystemExit('GPU framework platform differs from the requested probe SDK')
PYGPU
    gpu_enabled=1
    deployment=27.2
    gpu_corpus=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["driver_sha256"])' "$5/inputs.json")
    gpu_dependency=$'      - framework: Source/KosmicKrisp.framework\n        embed: true\n        codeSign: true'
else
    rm -f "$build_dir/Source/gpu_probe.mm"
fi
sdl_enabled=0
sdl_dependency=""
sdl_corpus=none
if [[ -n ${OSL_PROBE_SDL_PREFIX:-} ]]; then
    sdl_prefix=$OSL_PROBE_SDL_PREFIX
    sdl_corpus=$(python3 - "$sdl_prefix" "$probe_sdk" <<'PYSDL'
import hashlib,json,sys
from pathlib import Path
prefix=Path(sys.argv[1]).resolve();manifest=json.loads((prefix.parent/('inputs-'+sys.argv[2]+'.json')).read_text())
library=prefix/'lib/libSDL3.a'
if manifest['platform']!=sys.argv[2] or manifest['architectures']!='arm64' or Path(manifest['library'])!=library:
    raise SystemExit('SDL prefix does not match the requested mobile platform')
if hashlib.sha256(library.read_bytes()).hexdigest()!=manifest['library_sha256']:
    raise SystemExit('SDL archive differs from its checked manifest')
print(manifest['library_sha256'])
PYSDL
)
    cp "$sdl_prefix/lib/libSDL3.a" "$build_dir/Source/libSDL3.a"
    mkdir -p "$build_dir/Source/Headers"
    ditto "$sdl_prefix/include/SDL3" "$build_dir/Source/Headers/SDL3"
    cp "$source_dir/ios/graphics/surface-probe.mm" "$build_dir/Source/surface_probe.mm"
    sdl_enabled=1
    deployment=27.2
    sdl_dependency=$'      - framework: Source/libSDL3.a\n        embed: false\n      - sdk: CoreMedia.framework\n      - sdk: CoreVideo.framework\n      - sdk: CoreAudio.framework\n      - sdk: AudioToolbox.framework\n      - sdk: AVFoundation.framework\n      - sdk: CoreBluetooth.framework\n      - sdk: CoreGraphics.framework\n      - sdk: CoreMotion.framework\n      - sdk: QuartzCore.framework'
else
    rm -f "$build_dir/Source/libSDL3.a" "$build_dir/Source/surface_probe.mm"
fi
renderer_enabled=0
renderer_dependency=""
renderer_corpus=none
guest_frontend_corpus=none
if [[ -n ${OSL_PROBE_RENDERER_ROOT:-} ]]; then
    [[ $game_enabled == 1 && $gpu_enabled == 1 && $sdl_enabled == 1 ]] || { echo 'Renderer session requires the real game runtime, driver and matching SDL prefix' >&2; exit 1; }
    [[ -n ${OSL_PROBE_GUEST_ROOT:-} ]] || { echo 'Renderer session requires OSL_PROBE_GUEST_ROOT from build-guest-session.py' >&2; exit 1; }
    renderer_corpus=$(python3 - "$OSL_PROBE_RENDERER_ROOT" "$probe_sdk" "$source_dir" "$build_dir/Source" "$OSL_PROBE_SDL_PREFIX" <<'PYRENDERER'
from pathlib import Path
import hashlib,json,shutil,sys
root=Path(sys.argv[1]).resolve();sdk=sys.argv[2];repo=Path(sys.argv[3]);output=Path(sys.argv[4]);sdl=Path(sys.argv[5]).resolve()
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((root/('inputs-'+sdk+'.json')).read_text())
if d['platform']!=sdk or not d['renderer_library_compiled'] or not d['driver_linked']:raise SystemExit('Renderer is not compiled/consumer-linked for this SDK')
for name,wanted in d['sources'].items():
 if digest(repo/name)!=wanted:raise SystemExit('Renderer source changed: '+name)
if digest(sdl.parent/('inputs-'+sdk+'.json'))!=d['sdl_manifest_sha256']:raise SystemExit('Renderer SDL metadata changed')
options=d['options'];prefix=Path(next(x.split('=',1)[1] for x in options if x.startswith('-DCMAKE_PREFIX_PATH=')).split(';')[0])
dep_path=prefix.parent/('inputs-'+sdk+'.json');deps=json.loads(dep_path.read_text())
if digest(dep_path)!=d['dependency_manifest_sha256']:raise SystemExit('Renderer dependency metadata changed')
for name,item in d['archives'].items():
 path=root/('build-'+sdk)/name
 if digest(path)!=item['sha256']:raise SystemExit('Renderer archive changed: '+name)
 shutil.copy2(path,output/path.name)
for name,item in deps['libraries'].items():
 path=prefix/'lib'/name
 if digest(path)!=item['sha256']:raise SystemExit('Renderer dependency changed: '+name)
 shutil.copy2(path,output/path.name)
print(d['archives']['gpu/liboslgpu.a']['sha256'])
PYRENDERER
)
    guest_frontend_corpus=$(python3 - "$OSL_PROBE_GUEST_ROOT" "$probe_sdk" "$source_dir" "$4/$archive_platform/manifest.json" "$OSL_PROBE_RENDERER_ROOT" "$build_dir/Source" <<'PYGUEST'
from pathlib import Path
import hashlib,json,shutil,sys
root=Path(sys.argv[1]);sdk=sys.argv[2];repo=Path(sys.argv[3]);game=json.loads(Path(sys.argv[4]).read_text());renderer=Path(sys.argv[5]);output=Path(sys.argv[6])
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((root/('inputs-'+sdk+'.json')).read_text())
if d['platform']!=sdk or d['corpus_sha256']!=game['corpus_sha256'] or d['trace_inputs']!=game['trace_inputs']:raise SystemExit('GPU guest frontend differs from game archive/SDK')
if digest(renderer/('inputs-'+sdk+'.json'))!=d['renderer_manifest_sha256']:raise SystemExit('GPU guest frontend uses different renderer metadata')
for name,wanted in d['sources'].items():
 if digest(repo/name)!=wanted:raise SystemExit('GPU guest frontend source changed: '+name)
if digest(repo/'ios/graphics/build-guest-session.py')!=d['builder_sha256']:raise SystemExit('GPU guest frontend builder changed')
archive=Path(d['archive'])
if digest(archive)!=d['archive_sha256']:raise SystemExit('GPU guest frontend archive changed')
shutil.copy2(archive,output/'libodin-gpu-guest.a')
shutil.copy2(repo/'ios/graphics/guest-session.h',output/'guest-session.h')
print(d['archive_sha256'])
PYGUEST
)
    cp "$source_dir/ios/graphics/renderer-session.mm" "$build_dir/Source/renderer_session.mm"
    cp "$source_dir/ios/graphics/idle-timer-lease.h" "$build_dir/Source/idle-timer-lease.h"
    cp "$source_dir/ios/graphics/diagnostic-stop.h" "$build_dir/Source/diagnostic-stop.h"
    cp "$source_dir/ios/graphics/touch-overlay.mm" "$build_dir/Source/touch_overlay.mm"
    mkdir -p "$build_dir/Source/ios/services" "$build_dir/Source/src"
    cp "$source_dir/ios/services/touch-controls.h" "$source_dir/ios/services/pad.h" "$build_dir/Source/ios/services/"
    cp "$source_dir/src/runtime_pad_types.h" "$build_dir/Source/src/"
    cp "$source_dir/src/oslgpu.h" "$build_dir/Source/oslgpu.h"
    cp "$source_dir/ios/services/clock.h" "$build_dir/Source/clock.h"
    for library in oslgpu miniz sirit osl_keys fmt xxhash Zycore Zydis png16; do
        renderer_dependency+=$'      - framework: Source/lib'"$library"$'.a\n        embed: false\n'
    done
    renderer_dependency+=$'      - sdk: libz.tbd'
    renderer_dependency+=$'\n      - framework: Source/libodin-gpu-guest.a\n        embed: false'
    renderer_enabled=1
else
    rm -f "$build_dir/Source/renderer_session.mm" "$build_dir/Source/idle-timer-lease.h" "$build_dir/Source/touch_overlay.mm" "$build_dir/Source/oslgpu.h" "$build_dir/Source/clock.h" "$build_dir/Source/guest-session.h" "$build_dir/Source/libodin-gpu-guest.a"
fi
revision=$(git -C "$source_dir" rev-parse HEAD)
if [[ -n $(git -C "$source_dir" status --porcelain) ]]; then revision="$revision-dirty"; fi
cat > "$build_dir/project.yml" <<YAML
name: OdinSphereProbe
options:
  deploymentTarget:
    iOS: '$deployment'
targets:
  OdinSphereProbe:
    type: application
    platform: iOS
    sources:
      - path: Source
        excludes: ['*.a', 'KosmicKrisp.framework', 'Headers']
    dependencies:
      - sdk: UIKit.framework
      - sdk: Metal.framework
      - sdk: GameController.framework
      - sdk: CoreHaptics.framework
      - framework: Source/libodin-cpu.a
        embed: false
      - framework: Source/libodin-runtime.a
        embed: false
$game_dependency
$gpu_dependency
$sdl_dependency
$renderer_dependency
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: '$bundle_id'
        DEVELOPMENT_TEAM: '$team'
        CODE_SIGN_STYLE: Automatic
        ARCHS: arm64
        TARGETED_DEVICE_FAMILY: '1,2'
        CLANG_ENABLE_OBJC_ARC: YES
        CLANG_CXX_LANGUAGE_STANDARD: c++23
$entitlements_setting
        LIBRARY_SEARCH_PATHS: '\$(PROJECT_DIR)/Source'
        GCC_PREPROCESSOR_DEFINITIONS: 'OSL_GAME_STARTUP=$game_enabled OSL_GPU_PROBE=$gpu_enabled OSL_SDL_SURFACE=$sdl_enabled OSL_RENDERER_SESSION=$renderer_enabled'
        HEADER_SEARCH_PATHS: '\$(PROJECT_DIR)/Source/Headers'
        ASSETCATALOG_COMPILER_APPICON_NAME: AppIcon
        CURRENT_PROJECT_VERSION: 1
        MARKETING_VERSION: '0.1'
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: $display_name
        OSLBuildID: '$revision'
        OSLGameCorpus: '$game_corpus'
        OSLGPUDriver: '$gpu_corpus'
        OSLSDLArchive: '$sdl_corpus'
        OSLRendererArchive: '$renderer_corpus'
        OSLGuestFrontendArchive: '$guest_frontend_corpus'
        GCSupportsControllerUserInteraction: true
        GCSupportedGameControllers:
          - ProfileName: ExtendedGamepad
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIApplicationSupportsIndirectInputEvents: true
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UISupportedInterfaceOrientations: [UIInterfaceOrientationPortrait, UIInterfaceOrientationPortraitUpsideDown, UIInterfaceOrientationLandscapeLeft, UIInterfaceOrientationLandscapeRight]
YAML
xcodegen generate --spec "$build_dir/project.yml" --project "$build_dir"
xcodebuild -project "$build_dir/OdinSphereProbe.xcodeproj" -scheme OdinSphereProbe -configuration Release \
    -destination "$destination" -sdk "$probe_sdk" -derivedDataPath "$build_dir/DerivedData" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
codesign --verify --deep --strict "$build_dir/DerivedData/Build/Products/Release-$probe_sdk/OdinSphereProbe.app"
