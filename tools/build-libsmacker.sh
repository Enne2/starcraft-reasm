#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH=x86
while (($#)); do
  case "$1" in
    --arch) (($# >= 2)) || { echo "--arch requires x86|armv6|armv7" >&2; exit 2; }; ARCH="$2"; shift 2 ;;
    -h|--help) echo "Usage: tools/build-libsmacker.sh [--arch x86|armv6|armv7]"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
case "$ARCH" in
  x86) TRIPLE=i686-unknown-linux-gnu; FLAGS=(--target="$TRIPLE" -m32); OUT="$ROOT/build/i386-libs" ;;
  armv6) TRIPLE=armv6-unknown-linux-gnueabihf; FLAGS=(--target="$TRIPLE" -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard); OUT="$ROOT/build/armv6-libs" ;;
  armv7) TRIPLE=armv7-unknown-linux-gnueabihf; FLAGS=(--target="$TRIPLE" -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard); OUT="$ROOT/build/armv7-libs" ;;
  *) echo "unsupported --arch: $ARCH" >&2; exit 2 ;;
esac
mkdir -p "$OUT"
clang "${FLAGS[@]}" -std=gnu11 -O2 -shared -fPIC -Wall -Wextra \
  "$ROOT/vendor/libsmacker/smacker.c" -o "$OUT/libsmacker-1.2.0.so"
echo "built $OUT/libsmacker-1.2.0.so ($ARCH)"
