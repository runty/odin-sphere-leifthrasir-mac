#!/usr/bin/env bash
# Compile production runtime objects and run the upstream synchronization suite on Darwin.
set -euo pipefail
src=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?Usage: macos/build-runtime.sh EXTERNAL_BUILD_DIR SDL_PREFIX FREETYPE_INCLUDE}
sdl=${2:?SDL x86-64 prefix required}
freetype=${3:?FreeType include directory required}
python3 - "$build" <<'PY'
import pathlib, sys
p=pathlib.Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not pathlib.Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Build directory requires a mounted external volume')
PY
mkdir -p "$build/objects"
flags=(-arch x86_64 -std=gnu11 -O2 -Wall -Wextra -Werror -I"$src/src" -I"$src/launcher" -I"$sdl/include" -I"$freetype"
    "-DOSL_NOTO_SANS_PATH=\"$src/third_party/fonts/NotoSans-Regular.ttf\""
    "-DOSL_NOTO_SANS_TC_PATH=\"$src/third_party/fonts/NotoSansTC-Regular.otf\"")
objects=()
for file in "$src"/src/runtime*.c; do
    [[ $file == */runtime_log.c ]] && continue
    name=$(basename "$file" .c)
    xcrun clang "${flags[@]}" -c "$file" -o "$build/objects/$name.o"
    objects+=("$build/objects/$name.o")
done
xcrun libtool -static -o "$build/libosl-runtime.a" "${objects[@]}"
xcrun clang "${flags[@]}" "$src/tests/runtime_sync_tests.c" \
    "$src"/src/runtime_{rwlock,mutex,eventflag,thread,sema}.c -o "$build/runtime-tests"
"$build/runtime-tests"
