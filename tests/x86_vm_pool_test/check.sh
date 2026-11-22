#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/qa/x86_vm_pool_test"
mkdir -p "$ROOT/build/qa"
gcc -m32 -O2 -pthread -ffunction-sections -fdata-sections \
  -I"$ROOT/runtime/common" -I"$ROOT/runtime/shim-port/elf" \
  -I"${TOOLKIT:-$HOME/dev/reasm-toolkit}/runtime/elf" \
  "$ROOT/tests/x86_vm_pool_test.c" -Wl,--gc-sections -o "$OUT"
"$OUT"
