#!/usr/bin/env bash
# Unified build entry point for x86, ARMv6 and ARMv7 hard-float runtimes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH=""
SKIP_ENTRY=0
NO_LINK=0
usage() { echo "Usage: tools/build.sh --arch <x86|armv6|armv7|arm> [--skip-entry] [--no-link]"; }
while (($#)); do
  case "$1" in
    --arch) (($# >= 2)) || { usage >&2; exit 2; }; ARCH="$2"; shift 2 ;;
    --skip-entry) SKIP_ENTRY=1; shift ;;
    --no-link) NO_LINK=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ -n "$ARCH" ]] || { echo "--arch is required" >&2; usage >&2; exit 2; }
if [[ "$ARCH" == x86 ]]; then
  if (( SKIP_ENTRY )); then
    [[ -s "$ROOT/build/lift-full/entry-x86/StarCraft.entry.o" && -s "$ROOT/build/lift-full/entry-x86/storm.entry.o" ]] || {
      echo "x86 entry objects missing; run without --skip-entry" >&2; exit 2;
    }
  fi
  if (( ! SKIP_ENTRY )); then "$ROOT/tools/build-entry-x86.sh"; fi
  "$ROOT/tools/build-shim-x86.sh"
  "$ROOT/tools/build-runtime-x86.sh"
  if (( ! NO_LINK )); then "$ROOT/tools/link-x86.sh"; fi
  exit 0
fi
case "$ARCH" in
  arm|armv6)
    ARM_ARCH=armv6
    TRIPLE=armv6-unknown-linux-gnueabihf
    CPU_FLAGS='-march=armv6zk -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm'
    LLC_FLAGS='-mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard'
    QEMU_CPU=arm1176
    ;;
  armv7)
    ARM_ARCH=armv7
    TRIPLE=armv7a-unknown-linux-gnueabihf
    CPU_FLAGS='-march=armv7-a -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm'
    LLC_FLAGS='-mcpu=cortex-a53 -mattr=+vfp3,+neon,+hwdiv-arm -float-abi=hard'
    QEMU_CPU=cortex-a53
    ;;
  *) echo "--arch must be x86, armv6, armv7, or arm" >&2; usage >&2; exit 2 ;;
esac
export ARM_ARCH TRIPLE CPU_FLAGS LLC_FLAGS QEMU_CPU
export SYSROOT="${SYSROOT:-$ROOT/build/sysroot}"
if (( SKIP_ENTRY )); then
  echo "--skip-entry is currently supported only for x86" >&2; exit 2
fi
"$ROOT/tools/build-libsmacker.sh" --arch "$ARM_ARCH"
if (( ! SKIP_ENTRY )); then "$ROOT/tools/build-entry-obj.sh" --no-smoke; fi
"$ROOT/tools/build-runtime-arm.sh" --stage 4
if (( ! NO_LINK )); then
  ENTRY_DIR="${ENTRY_DIR:-$ROOT/build/lift-full/entry}" "$ROOT/tools/link-arm.sh"
fi
