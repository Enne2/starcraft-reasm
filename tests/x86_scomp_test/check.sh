#!/usr/bin/env bash
# Synthetic unit checks + differential run against real sectors captured from the game.
# The capture directory (SC_SCOMP_DUMP output) is optional: without it the differential step
# says SKIP, so the suite still works on a clean checkout. No game bytes are stored in the repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/tests/x86_scomp_test"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-clang}"
"$CC" -O2 -Wall -Wextra -DSC_SCOMP_TEST_ONLY -ffunction-sections -fdata-sections \
  -I"$ROOT/runtime/common" -c "$ROOT/runtime/common/storm_scomp.c" -o "$TMP/storm_scomp.o"
"$CC" -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
  -c "$D/test_scomp.c" -o "$TMP/test_scomp.o"
"$CC" -Wl,--gc-sections "$TMP/test_scomp.o" "$TMP/storm_scomp.o" -o "$TMP/test_scomp"
"$TMP/test_scomp"

"$CC" -O2 -Wall -Wextra -c "$D/diff_real.c" -o "$TMP/diff_real.o"
"$CC" -Wl,--gc-sections "$TMP/diff_real.o" "$TMP/storm_scomp.o" -o "$TMP/diff_real"
CAPTURES="${SC_SCOMP_SAMPLES:-/tmp/scomp-real-now}"
if [[ -d "$CAPTURES" ]]; then
  "$TMP/diff_real" "$CAPTURES"
  "$CC" -O2 -Wall -Wextra -c "$D/bench_real.c" -o "$TMP/bench_real.o"
  "$CC" -Wl,--gc-sections "$TMP/bench_real.o" "$TMP/storm_scomp.o" -o "$TMP/bench_real"
  "$TMP/bench_real" "$CAPTURES"
else
  echo "x86_scomp_test: SKIP differenziale/benchmark reale ($CAPTURES assente; rigenerare con SC_SCOMP_DUMP)"
fi
