#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BUILD="$ROOT/build"
if (($# < 2)); then echo "Uso: $0 input.exe output.llasm [flag LLASM...]" >&2; exit 2; fi
input=$(realpath "$1"); output=$(realpath -m "$2"); shift 2
[[ "$output" == "$BUILD"/* ]] || { echo "Output ammesso solo sotto $BUILD" >&2; exit 2; }
[[ -x "$BUILD/pipeline/bin/SRW-llasm" && -x "$BUILD/pipeline/bin/llasm" ]] || { echo 'Eseguire tools/build-tools.sh' >&2; exit 2; }
mkdir -p "$(dirname "$output")"
"$BUILD/pipeline/bin/SRW-llasm" "$input" "$output"
if [[ ${LLASM_TO_IR:-1} == 1 ]]; then
  ir="${output%.llasm}.ll"
  "$BUILD/pipeline/bin/llasm" "$@" -o "$ir" "$output"
  echo "IR: $ir"
fi
echo "LLASM: $output"
