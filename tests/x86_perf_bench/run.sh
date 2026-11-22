#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/qa/bench-dispatch-mem"
RUNS="${1:-5}"
CALLS="${2:-50000000}"
mkdir -p "$ROOT/build/qa"
gcc -m32 -O2 -ffunction-sections -fdata-sections \
  -I"$ROOT/runtime/common" -I"$ROOT/runtime/shim-port/elf" \
  "$ROOT/tests/x86_perf_bench/bench_dispatch_mem.c" \
  "$ROOT/runtime/common/indirect_dispatch.c" \
  "$ROOT/runtime/common/game_memory_arm.c" \
  "$ROOT/runtime/common/storm_sdraw_arm.c" \
  -Wl,--gc-sections -o "$OUT"
for run in $(seq 1 "$RUNS"); do
  echo "=== run $run/$RUNS ==="
  "$OUT" "$CALLS"
done
