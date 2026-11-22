#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BUILD="$ROOT/build"
if (($# != 2)); then echo "Uso: $0 input.ll output.o|output.elf" >&2; exit 2; fi
input=$(realpath "$1"); output=$(realpath -m "$2")
[[ "$output" == "$BUILD"/* ]] || { echo "Output ammesso solo sotto $BUILD" >&2; exit 2; }
mkdir -p "$(dirname "$output")"
triple=armv6-unknown-linux-gnueabihf
# Derive the target data layout from this installed clang, then apply it before opt.
clang --target="$triple" -march=armv6zk -mfpu=vfp -mfloat-abi=hard -S -emit-llvm -x c /dev/null -o "$BUILD/pipeline/layout.ll"
datalayout=$(awk -F'"' '/^target datalayout/ {print $2; exit}' "$BUILD/pipeline/layout.ll")
[[ -n "$datalayout" ]] || { echo 'clang did not emit target datalayout' >&2; exit 2; }
opt -S -passes=verify -data-layout="$datalayout" -o "$BUILD/pipeline/verified.ll" "$input"
opt -S -passes='default<O2>' -data-layout="$datalayout" -o "$BUILD/pipeline/optimized.ll" "$BUILD/pipeline/verified.ll"
llc -mtriple="$triple" -mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard -filetype=obj -o "$BUILD/pipeline/output.o" "$BUILD/pipeline/optimized.ll"
if [[ "$output" == *.o ]]; then cp "$BUILD/pipeline/output.o" "$output"
else
  LD=${ARM_LD:-$HOME/projects/zero95/.tools/arm-gnu-toolchain/bin/arm-none-eabi-ld}
  [[ -x "$LD" ]] || { echo "Linker non trovato: $LD" >&2; exit 2; }
  "$LD" -m armelf -static -e _start -o "$output" "$BUILD/pipeline/output.o"
fi
LC_ALL=C readelf -h "$output" | grep -E 'Class:|Machine:|Type:'
