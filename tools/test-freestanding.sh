#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BUILD="$ROOT/build/pipeline"
mkdir -p "$BUILD/test"
cat > "$BUILD/test/start.ll" <<'IR'
define void @_start() noreturn {
entry:
  call void asm sideeffect "mov r0, #23\0Amov r7, #1\0Asvc #0", ""()
  unreachable
}
IR
"$ROOT/tools/compile-arm.sh" "$BUILD/test/start.ll" "$BUILD/test/start.elf"
if "${QEMU_ARM:-qemu-arm-static}" -cpu arm1176 "$BUILD/test/start.elf"; then status=0; else status=$?; fi
[[ $status == 23 ]] || { echo "Expected guest exit 23, got $status" >&2; exit 1; }
echo 'PASS: ARMv6 Linux syscall exit(23) under QEMU'
