#!/usr/bin/env bash
set -euo pipefail
src=$(cd "$(dirname "$0")/.." && pwd)
work=${1:?Usage: macos/probe.sh WORK_DIR [VULKAN_PREFIX]}
mkdir -p "$work"
work=$(cd "$work" && pwd)
xcrun clang -arch x86_64 -O2 -Wall -Wextra -Werror -I"$src/src" "-DOSL_SOURCE_DIR=\"$work\"" \
    "$src/tests/isa_tests.c" "$src/src/isa_emulate.c" "$src/src/lzcnt_compat.c" -o "$work/isa-test"
ROSETTA_ADVERTISE_AVX=1 "$work/isa-test"
xcrun clang -arch x86_64 -O2 -Wall -Wextra -Werror -I"$src/src" \
    "$src/macos/probe-memory.c" "$src/src/runtime_memory.c" "$src/src/runtime_fiber.c" -o "$work/probe-memory"
"$work/probe-memory"
xcrun clang -arch x86_64 -O2 -Wall -Wextra -Werror -Wl,-dead_strip -I"$src/src" \
    "$src/macos/probe-tls.c" "$src/src/runtime_memory.c" -o "$work/probe-tls"
"$work/probe-tls"
if [[ $# -ge 2 ]]; then
    vk=$2
    xcrun clang -arch x86_64 -Wall -Wextra -Werror -I"$vk/include" "$src/macos/probe-vulkan.c" \
        -L"$vk/lib" -lvulkan -Wl,-rpath,"$vk/lib" -o "$work/probe-vulkan"
    "$work/probe-vulkan"
fi
