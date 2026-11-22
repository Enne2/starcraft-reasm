#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="$ROOT/tests/smacker_bench"
CC="${CC:-clang}"
RUNS="${RUNS:-5}"
VERIFY=0
if [[ "${1:-}" == --verify ]]; then VERIFY=1; shift; fi
LIB="${1:-$ROOT/build/i386-libs/libsmacker-1.2.0.so}"
shift || true
if (($#)); then SAMPLES=("$@"); else SAMPLES=("$DIR"/samples/*.smk); fi
[[ -f "$LIB" ]] || { echo "library not found: $LIB" >&2; exit 2; }
((${#SAMPLES[@]})) || { echo "no .smk samples" >&2; exit 2; }
mkdir -p "$ROOT/build/smacker-bench"
"$CC" --target=i686-unknown-linux-gnu -m32 -O2 -Wall -Wextra -Werror "$DIR/bench_smk.c" -ldl -o "$ROOT/build/smacker-bench/bench_smk"
if ((VERIFY)); then
  BASE="$ROOT/build/i386-libs/libsmacker-1.2.0.orig.so"
  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  for sample in "${SAMPLES[@]}"; do
    name="$(basename "$sample" .smk)"
    "$ROOT/build/smacker-bench/bench_smk" "$BASE" --dump "$TMP/$name.orig" "$sample"
    "$ROOT/build/smacker-bench/bench_smk" "$LIB" --dump "$TMP/$name.opt" "$sample"
    cmp "$TMP/$name.orig" "$TMP/$name.opt" || { echo "bit-exact mismatch: $sample" >&2; exit 1; }
    echo "bit-exact parity: $sample PASS ($(wc -c < "$TMP/$name.orig") bytes)"
  done
  "$ROOT/build/smacker-bench/bench_smk" "$BASE" --bench "$RUNS" "${SAMPLES[@]}"
  "$ROOT/build/smacker-bench/bench_smk" "$LIB" --bench "$RUNS" "${SAMPLES[@]}"
  exit 0
fi
"$ROOT/build/smacker-bench/bench_smk" "$LIB" --bench "$RUNS" "${SAMPLES[@]}"
for sample in "${SAMPLES[@]}"; do
  name="$(basename "$sample" .smk)"
  "$ROOT/build/smacker-bench/bench_smk" "$LIB" --dump "$ROOT/build/smacker-bench/${name}.dump" "$sample"
done
