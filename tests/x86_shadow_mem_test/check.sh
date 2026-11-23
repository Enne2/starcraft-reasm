#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/qa/x86_shadow_mem_test"
mkdir -p "$ROOT/build/qa"
gcc -m32 -O2 -ffunction-sections -fdata-sections \
  -I"$ROOT/runtime/common" -I"$ROOT/runtime/shim-port/elf" \
  "$ROOT/tests/x86_shadow_mem_test.c" \
  "$ROOT/runtime/common/game_memory.c" \
  "$ROOT/runtime/common/storm_sdraw.c" \
  -Wl,--gc-sections -o "$OUT"
"$OUT"
