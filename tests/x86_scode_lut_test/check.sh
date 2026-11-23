#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/qa/opt-scode"
mkdir -p "$OUT"
cd "$ROOT"
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
        -I runtime/common -I runtime/shim-port/elf
        tests/x86_scode_lut_test/test_scode_lut.c runtime/common/game_memory.c
        -Wl,--gc-sections)

gcc -m32 "${COMMON[@]}" -o "$OUT/test_scode_lut_32"
echo '=== i386 ==='
"$OUT/test_scode_lut_32"

clang "${COMMON[@]}" -o "$OUT/test_scode_lut_64"
echo '=== clang host 64-bit ==='
"$OUT/test_scode_lut_64"
