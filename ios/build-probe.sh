#!/usr/bin/env bash
# Build a private signed app containing the Odin-specific AOT CPU comparisons.
set -euo pipefail
if [[ $# != 3 ]]; then
    echo 'Usage: ios/build-probe.sh EXTERNAL_BUILD_DIR DEVELOPMENT_TEAM CPU_FIXTURE_OUTPUT' >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$1
team=$2
fixtures=$3
python3 - "$build_dir" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Build requires a mounted external disk')
PY
mkdir -p "$build_dir/Source"
cp "$source_dir/ios/main.mm" "$build_dir/Source/main.mm"
cp "$fixtures/ios/libodin-cpu.a" "$build_dir/Source/libodin-cpu.a"
revision=$(git -C "$source_dir" rev-parse HEAD)
if [[ -n $(git -C "$source_dir" status --porcelain) ]]; then revision="$revision-dirty"; fi
cat > "$build_dir/project.yml" <<YAML
name: OdinSphereProbe
options:
  deploymentTarget:
    iOS: '18.0'
targets:
  OdinSphereProbe:
    type: application
    platform: iOS
    sources:
      - path: Source
        excludes: ['*.a']
    dependencies:
      - sdk: UIKit.framework
      - sdk: Metal.framework
      - framework: Source/libodin-cpu.a
        embed: false
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: com.wadscorp.odinsphere.probe
        DEVELOPMENT_TEAM: '$team'
        CODE_SIGN_STYLE: Automatic
        TARGETED_DEVICE_FAMILY: '1,2'
        CLANG_ENABLE_OBJC_ARC: YES
        LIBRARY_SEARCH_PATHS: '\$(PROJECT_DIR)/Source'
        CURRENT_PROJECT_VERSION: 1
        MARKETING_VERSION: '0.1'
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: Odin Sphere Probe
        OSLBuildID: '$revision'
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UISupportedInterfaceOrientations: [UIInterfaceOrientationPortrait, UIInterfaceOrientationLandscapeLeft, UIInterfaceOrientationLandscapeRight]
YAML
xcodegen generate --spec "$build_dir/project.yml" --project "$build_dir"
xcodebuild -project "$build_dir/OdinSphereProbe.xcodeproj" -scheme OdinSphereProbe -configuration Release \
    -destination generic/platform=iOS -derivedDataPath "$build_dir/DerivedData" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
codesign --verify --deep --strict "$build_dir/DerivedData/Build/Products/Release-iphoneos/OdinSphereProbe.app"
