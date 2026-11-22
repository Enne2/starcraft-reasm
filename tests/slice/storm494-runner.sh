#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/slice"
BIN="$ROOT/build/pipeline/bin"
mkdir -p "$OUT"
for t in python3 i686-w64-mingw32-gcc wine llvm-objdump clang qemu-arm-static readelf; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 2; }; done
[[ -x "$BIN/SRW-llasm" && -x "$BIN/llasm" ]] || { echo "missing SRW/llasm in $BIN" >&2; exit 2; }
python3 "$ROOT/tests/slice/make_storm494_pe.py"
# Byte-identity assertion: PE .text first 27 bytes equal the copied IDA sequence.
python3 - "$OUT/storm494_slice.exe" <<'PY'
import sys
b=open(sys.argv[1],'rb').read(); pe=int.from_bytes(b[0x3c:0x40],'little'); opt=pe+4+20
sec=opt+224
raw=int.from_bytes(b[sec+20:sec+24],'little')
expected=bytes.fromhex('55 8b ec 8b 4d 0c 85 c9 8b 45 08 74 0a 8d 49 00 c6 00 00 40 49 75 f9 5d c2 08 00')
assert b[raw:raw+len(expected)]==expected
PY
# x86 oracle executes the byte-exact routine from an assembly object.
i686-w64-mingw32-gcc -nostdlib -Wl,--subsystem,console -Wl,-e,_mainCRTStartup -o "$OUT/storm494_x86.exe" "$ROOT/tests/slice/storm494_x86_test.c" "$ROOT/tests/slice/storm494_x86.S"
if WINEPREFIX="$OUT/wineprefix-storm494" WINEDEBUG=-all wine "$OUT/storm494_x86.exe" > "$OUT/storm494-wine.log" 2>&1; then :; else s=$?; tail -15 "$OUT/storm494-wine.log"; exit "$s"; fi
# SRW starts code discovery from ordinal-only export 494; all output stays in build/slice.
(cd "$OUT" && "$BIN/SRW-llasm" "$OUT/storm494_slice.exe" "$OUT/storm494_slice.llasm" > "$OUT/storm494-srw.log" 2>&1)
: > "$OUT/extern.llinc"; : > "$OUT/macros.llinc"
"$BIN/llasm" -O2 -I "$ROOT/vendor/sr/SR/llasm-support" -I "$OUT" -o "$OUT/storm494_slice.ll" "$OUT/storm494_slice.llasm"
rg -q 'define protected fastcc void @ordinal_73746f726d2e646c6c_494' "$OUT/storm494_slice.ll"
clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard -c "$OUT/storm494_slice.ll" -o "$OUT/storm494_slice.o"
clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard -ffreestanding -fno-builtin -fno-stack-protector -c "$ROOT/tests/slice/storm494_arm_bridge.c" -o "$OUT/storm494_arm_bridge.o"
LD="$HOME/projects/zero95/.tools/arm-gnu-toolchain/bin/arm-none-eabi-ld"
"$LD" -m armelf -static -e _start -o "$OUT/storm494_arm.elf" "$OUT/storm494_arm_bridge.o" "$OUT/storm494_slice.o"
readelf -A "$OUT/storm494_arm.elf" > "$OUT/storm494-arm-attributes.txt"
grep -q 'Tag_CPU_arch: v6' "$OUT/storm494-arm-attributes.txt"
grep -q 'Tag_FP_arch: VFPv2' "$OUT/storm494-arm-attributes.txt"
qemu-arm-static -cpu arm1176 "$OUT/storm494_arm.elf"
echo 'PASS: Storm #494 exact-code slice, x86 Wine oracle, SRW->LLASM->LLVM, ARM1176 QEMU differential.'
