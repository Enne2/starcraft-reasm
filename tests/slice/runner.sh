#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/slice"
BIN="$ROOT/build/pipeline/bin"
mkdir -p "$OUT"
for tool in i686-w64-mingw32-gcc i686-w64-mingw32-dlltool wine llvm-objdump clang llc opt readelf qemu-arm-static; do
  command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 2; }
done
[[ -x "$BIN/SRW-llasm" && -x "$BIN/llasm" ]] || { echo "missing built SRW/llasm in $BIN" >&2; exit 2; }
# PE32 export is ordinal-only; the executable's import library references #1.
i686-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,_DllMainCRTStartup@12 -Wl,--out-implib,"$OUT/libstorm.a" -o "$OUT/storm.dll" "$ROOT/tests/slice/storm_stub.c" "$ROOT/tests/slice/storm.def"
i686-w64-mingw32-dlltool -d "$ROOT/tests/slice/storm.def" -l "$OUT/libstorm-ordinal.a" -m i386
i686-w64-mingw32-gcc -nostdlib -Wl,--subsystem,console -Wl,-e,_mainCRTStartup -o "$OUT/ordinal_fixture.exe" "$ROOT/tests/slice/ordinal_fixture.c" "$OUT/libstorm-ordinal.a" -lkernel32
llvm-objdump -p "$OUT/ordinal_fixture.exe" > "$OUT/imports.txt"
awk '$1 == "DLL" && $2 == "Name:" { storm = (tolower($3) == "storm.dll") }
     /^Export Table:/ { storm = 0 }
     storm && NF == 1 && $1 == "1" { found = 1 }
     END { exit !found }' "$OUT/imports.txt" || { echo 'storm.dll ordinal #1 not present' >&2; exit 1; }
# Run the fixture under Wine as the x86 oracle. DLL sits next to executable.
if WINEPREFIX="$OUT/wineprefix" WINEDEBUG=-all wine "$OUT/ordinal_fixture.exe" > "$OUT/wine.log" 2>&1; then
  :
else
  status=$?; tail -20 "$OUT/wine.log"; exit "$status"
fi
# Exercise the real SRW PE loader/emitter against this tiny ordinal-import PE.
(cd "$OUT" && "$BIN/SRW-llasm" "$OUT/ordinal_fixture.exe" ordinal_fixture.llasm)
test -s "$OUT/ordinal_fixture.llasm"
: > "$OUT/extern.llinc"
: > "$OUT/macros.llinc"
"$BIN/llasm" -O2 -I "$ROOT/vendor/sr/SR/llasm-support" -I "$OUT" -o "$OUT/ordinal_fixture.ll" "$OUT/ordinal_fixture.llasm"
test -s "$OUT/ordinal_fixture.ll"
# Bind precisely the synthetic Storm ordinal emitted by SRW to an ARM bridge.
# It returns from the guest CALL by popping its guest return EIP, then enters the
# LLASM-generated continuation block. This preserves the generated CPU/stack logic.
python3 - "$OUT/ordinal_fixture.ll" "$OUT/ordinal_fixture_arm.ll" <<'PY'
import sys
src=open(sys.argv[1]).read()
old='declare hidden fastcc void @ordinal_73746f726d2e646c6c_1(%_cpu*) nounwind'
new='''define hidden fastcc void @ordinal_73746f726d2e646c6c_1(%_cpu* %cpu) nounwind {
  %eaxp = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
  store i32 5, i32* %eaxp, align 4
  %espp = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
  %esp0 = load i32, i32* %espp, align 4
  %esp1 = add i32 %esp0, 4
  store i32 %esp1, i32* %espp, align 4
  call fastcc void @loc_40100A(%_cpu* %cpu) nounwind
  ret void
}'''
if src.count(old) != 1: raise SystemExit('expected ordinal bridge declaration not found exactly once')
open(sys.argv[2],'w').write(src.replace(old,new))
PY
"$ROOT/tools/compile-arm.sh" "$OUT/ordinal_fixture_arm.ll" "$OUT/ordinal_fixture_arm.o"
clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard -ffreestanding -fno-builtin -fno-stack-protector -c "$ROOT/tests/slice/arm_bridge.c" -o "$OUT/arm_bridge.o"
LD=${ARM_LD:-$HOME/projects/zero95/.tools/arm-gnu-toolchain/bin/arm-none-eabi-ld}
"$LD" -m armelf -static -e _start -o "$OUT/ordinal_generated_arm.elf" "$OUT/arm_bridge.o" "$OUT/ordinal_fixture_arm.o"
readelf -A "$OUT/ordinal_generated_arm.elf" > "$OUT/arm-attributes.txt"
grep -Eq 'Tag_CPU_arch: v6([[:space:]]|$)' "$OUT/arm-attributes.txt"
grep -q 'Tag_FP_arch: VFPv2' "$OUT/arm-attributes.txt"
"${QEMU_ARM:-qemu-arm-static}" -cpu arm1176 "$OUT/ordinal_generated_arm.elf"
echo 'PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack bridge; ARMv6/VFPv2 attributes verified.'
