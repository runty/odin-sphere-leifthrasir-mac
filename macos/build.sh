#!/usr/bin/env bash
# Incremental x86-64 Darwin build using explicit driver, crypto/PNG, FreeType and Cocoa SDL inputs.
set -euo pipefail
src=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?Usage: macos/build.sh BUILD VK_PREFIX DEPS_PREFIX FREETYPE_BUILD FREETYPE_INCLUDE SDL3_CMAKE_DIR}
vk=${2:?Vulkan/SDL/fmt/Zydis/xxHash prefix required}
deps=${3:?x86-64 OpenSSL and PNG prefix required}
ftbuild=${4:?FreeType build directory required}
ftinclude=${5:?FreeType include directory required}
sdl=${6:?Cocoa-capable SDL3 CMake directory required}
python3 - "$build" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not pathlib.Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Build directory requires a mounted external disk')
PY
while read -r path url pin; do
    [[ -z ${path:-} || $path == \#* ]] && continue
    dest=$src/gpu/$path
    if [[ -e $dest ]]; then
        [[ -f $dest/.osl_commit && $(cat "$dest/.osl_commit") == "$pin" ]] || {
            echo "Dependency at $dest differs from its pin; leaving it untouched" >&2; exit 1;
        }
    else
        mkdir -p "$dest"
        git -C "$dest" init -q
        git -C "$dest" fetch -q --depth 1 "$url" "$pin"
        git -C "$dest" checkout -q FETCH_HEAD
        printf '%s' "$pin" > "$dest/.osl_commit"
    fi
done < "$src/gpu/deps.txt"
export PKG_CONFIG_PATH="$vk/lib/pkgconfig"
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    "-DCMAKE_PREFIX_PATH=$vk;$(brew --prefix boost);$(brew --prefix robin-map);$(brew --prefix vulkan-memory-allocator)" \
    "-DSDL3_DIR=$sdl" \
    "-DOPENSSL_CRYPTO_LIBRARY=$deps/lib/libcrypto.a" "-DOPENSSL_INCLUDE_DIR=$deps/include" \
    "-DPNG_LIBRARY_RELEASE=$deps/lib/libpng16.a" "-DPNG_PNG_INCLUDE_DIR=$deps/include" \
    "-DFREETYPE_LIBRARY_RELEASE=$ftbuild/libfreetype.a" \
    "-DFREETYPE_INCLUDE_DIR_freetype2=$ftinclude" "-DFREETYPE_INCLUDE_DIR_ft2build=$ftinclude"
cmake --build "$build" --parallel 8
