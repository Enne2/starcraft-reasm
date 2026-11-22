#!/usr/bin/env bash
# Build all runtime objects for the i686 translated entry objects.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/runtime/common"; X86="$ROOT/runtime/arch/x86"
OUT="$ROOT/build/obj-x86/runtime"; ENTRY="$ROOT/build/lift-full/entry-x86"
SC_REASM="${SC_REASM:-$HOME/dev/sc-reasm}"
SR="$ROOT/vendor/sr/SR/llasm-support"
CC=(clang --target=i686-unknown-linux-gnu -m32 -fno-pie)
CFLAGS=(-O1 -g -Wall -Wextra -Wno-unused-parameter)
mkdir -p "$OUT" "$OUT/pe-bytes"
[[ -f "$ENTRY/StarCraft.entry.o" && -f "$ENTRY/storm.entry.o" ]] || { echo 'entry-x86 objects missing' >&2; exit 2; }
[[ -f "$ENTRY/StarCraft.entry.ll" && -f "$ENTRY/storm.entry.ll" ]] || { echo 'entry-x86 IR missing' >&2; exit 2; }

python3 "$RT/gen_game_layout.py" --exe-obj "$ENTRY/StarCraft.entry.o" --exe-ir "$ENTRY/StarCraft.entry.ll" \
  --storm-obj "$ENTRY/storm.entry.o" --storm-ir "$ENTRY/storm.entry.ll" \
  --out-ld "$OUT/guest_data.ld" --out-header "$OUT/guest_layout_x86.h" \
  --pe-exe "$ROOT/build/lift-full/StarCraft.orig.exe" --pe-storm "$ROOT/build/lift-full/storm.orig.dll" \
  --out-pe "$OUT/guest_pe_bytes.c"
python3 "$RT/gen_pe_image_bytes.py" --arch x86 --pe exe="$ROOT/build/lift-full/StarCraft.orig.exe" --pe storm="$ROOT/build/lift-full/storm.orig.dll" \
  --ld-in "$OUT/guest_data.ld" --ld-obj "$OUT/guest_pe_bytes.o" --out-ld "$OUT/guest_data.ld" \
  --out-asm "$OUT/guest_pe_bytes.S" --blob-dir "$OUT/pe-bytes"
python3 "$RT/gen_storm_ordinals.py" --json "$SC_REASM/analysis/storm_ordinals.json" --out "$OUT/storm_ordinals.c"
python3 "$RT/gen_bridge_stubs.py" --arch x86 --def "$RT/bridge_table.def" --also "$ROOT/runtime/shim-port/bridge_shims.def" \
  --out "$OUT/bridge_stubs.S" --csym-out "$OUT/bridge_symbols.c" --table-out "$OUT/bridge_table.c"

for src in "$SR"/llasm_*.c; do
  [[ "$(basename "$src")" == llasm_cmps.c ]] && src="$RT/llasm_cmps.c"
  "${CC[@]}" "${CFLAGS[@]}" -I "$SR" -c "$src" -o "$OUT/$(basename "${src%.c}").o"
done
for name in cpu_llasm game_memory_arm x86_helpers bridge_arm elf_support_arm x87_crt_arm crt_init_arm guest_call_arm win32_ext_arm \
            indirect_dispatch com_bridge_arm storm_sdraw_arm storm_scomp game_glue; do
  "${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$OUT" -c "$RT/$name.c" -o "$OUT/$name.o"
done
"${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$OUT" -c "$RT/game_main_arm.c" -o "$OUT/game_main_arm.o"
for src in "$X86/indirect_dispatch_x86.S" "$X86/com_bridge_x86.S" "$X86/guest_abs_zero_x86.S" "$OUT/bridge_stubs.S" "$OUT/guest_pe_bytes.S"; do
  objname="$(basename "${src%.S}")"
  [[ "$objname" == guest_pe_bytes ]] && objname=guest_pe_bytes_asm
  "${CC[@]}" -c "$src" -o "$OUT/$objname.o"
done
SHIM_CALLS="$ROOT/build/obj-x86/shim_check/shim_calls.i686.c"
[[ -s "$SHIM_CALLS" ]] || { echo "i686 shim caller source missing: run tools/build-shim-x86.sh" >&2; exit 2; }
# Win32 SHIM exports are stdcall on i386 (the callee executes ret N). The generic
# generated callers default to cdecl, which would pop the arguments twice and corrupt
# the native stack. Annotate fixed-arity function pointer types; the variadic caller
# remains cdecl by ABI contract.
python3 - "$SHIM_CALLS" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
head, marker, tail = s.partition('static unsigned call_var(')
if not marker:
    raise SystemExit('missing variadic caller boundary in generated shim callers')
head = head.replace('unsigned (*)(', 'unsigned (__attribute__((stdcall)) *)(')
open(p, 'w').write(head + marker + tail)
PY
"${CC[@]}" "${CFLAGS[@]}" -I "$RT" -c "$SHIM_CALLS" -o "$OUT/shim_calls.o"
for src in "$OUT/bridge_symbols.c" "$OUT/bridge_table.c" "$OUT/storm_ordinals.c" "$OUT/guest_pe_bytes.c"; do
  "${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$OUT" -c "$src" -o "$OUT/$(basename "${src%.c}").o"
done
for obj in "$OUT"/*.o; do
  LC_ALL=C readelf -h "$obj" | grep -q 'Class:.*ELF32' || { echo "not ELF32: $obj" >&2; exit 1; }
  LC_ALL=C readelf -h "$obj" | grep -q 'Machine:.*Intel 80386' || { echo "not i386: $obj" >&2; exit 1; }
done
printf 'runtime-x86: %s ELF32 i386 objects in %s\n' "$(find "$OUT" -maxdepth 1 -name '*.o' | wc -l)" "$OUT"
