#!/usr/bin/env bash
# Build the experimental AOT backend in an isolated external cache.
set -euo pipefail
root=${1:?Usage: ios/build-backend.sh EXTERNAL_BACKEND_DIR [LLVM_PREFIX]}
llvm=${2:-/opt/homebrew/opt/llvm}
source_dir=$(cd "$(dirname "$0")" && pwd)
pin=66c488c9911fd9b368e4d0c9647d74239f8bba8b
python3 - "$root" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Backend requires a mounted external disk')
PY
[[ $("$llvm/bin/llvm-config" --version) == 23.1.5 ]] || { echo 'The reference backend requires LLVM 23.1.5' >&2; exit 1; }
mkdir -p "$root"
if [[ ! -d $root/remill ]]; then
    git clone --no-checkout https://github.com/lifting-bits/remill.git "$root/remill"
    git -C "$root/remill" checkout --detach "$pin"
    for name in llvm23 lifter-lifetime packed-simd pcmpistrm vpermilps rcpps blendps shufps vpermilpd rsqrtps double-select insertps movemask sse41-rounding startup-lane-insert; do
        git -C "$root/remill" apply "$source_dir/remill-$name.patch"
    done
else
    [[ $(git -C "$root/remill" rev-parse HEAD) == "$pin" ]] || { echo 'Unexpected backend revision; leaving it untouched' >&2; exit 1; }

fi
python3 "$source_dir/audit-backend.py" --backend "$root/remill"
cmake -G Ninja -S "$root/remill/dependencies" -B "$root/remill-deps" -DUSE_EXTERNAL_LLVM=ON \
    "-DCMAKE_PREFIX_PATH=$llvm" "-DCMAKE_INSTALL_PREFIX=$root/remill-install"
cmake --build "$root/remill-deps" --parallel 8
cmake -G Ninja -S "$root/remill" -B "$root/remill-build" \
    "-DCMAKE_PREFIX_PATH=$root/remill-install" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/remill-build" --parallel 8
