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
PY
    cp "$4/$archive_platform/libodin-startup.a" "$build_dir/Source/libodin-startup.a"
    game_corpus=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["corpus_sha256"])' "$4/$archive_platform/manifest.json")
    game_enabled=1
    game_dependency=$'      - framework: Source/libodin-startup.a\n        embed: false'
else
    rm -f "$build_dir/Source/libodin-startup.a"
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
      - framework: Source/libodin-cpu.a
        embed: false
      - framework: Source/libodin-runtime.a
        embed: false
$game_dependency
$gpu_dependency
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: com.wadscorp.odinsphere.probe
        DEVELOPMENT_TEAM: '$team'
        CODE_SIGN_STYLE: Automatic
        ARCHS: arm64
        TARGETED_DEVICE_FAMILY: '1,2'
        CLANG_ENABLE_OBJC_ARC: YES
        LIBRARY_SEARCH_PATHS: '\$(PROJECT_DIR)/Source'
        GCC_PREPROCESSOR_DEFINITIONS: 'OSL_GAME_STARTUP=$game_enabled OSL_GPU_PROBE=$gpu_enabled'
        HEADER_SEARCH_PATHS: '\$(PROJECT_DIR)/Source/Headers'
        ASSETCATALOG_COMPILER_APPICON_NAME: AppIcon
        CURRENT_PROJECT_VERSION: 1
        MARKETING_VERSION: '0.1'
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: Odin Sphere Probe
        OSLBuildID: '$revision'
        OSLGameCorpus: '$game_corpus'
        OSLGPUDriver: '$gpu_corpus'
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UISupportedInterfaceOrientations: [UIInterfaceOrientationPortrait, UIInterfaceOrientationPortraitUpsideDown, UIInterfaceOrientationLandscapeLeft, UIInterfaceOrientationLandscapeRight]
YAML
xcodegen generate --spec "$build_dir/project.yml" --project "$build_dir"
xcodebuild -project "$build_dir/OdinSphereProbe.xcodeproj" -scheme OdinSphereProbe -configuration Release \
    -destination "$destination" -sdk "$probe_sdk" -derivedDataPath "$build_dir/DerivedData" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
codesign --verify --deep --strict "$build_dir/DerivedData/Build/Products/Release-$probe_sdk/OdinSphereProbe.app"
