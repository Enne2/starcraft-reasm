#!/usr/bin/env bash
# Build the ARMv7 runtime, the guest-ABI shim layer and the complete game ELF.
# Mirrors tools/build-runtime-x86.sh: entry objects come from tools/build-entry-arm.sh
# (which also emits guest_data.ld / guest_pe_bytes.o / pe_headers_arm.c); this script
# compiles the runtime and the shims and then calls tools/link-arm.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/runtime/common"; ARM="$ROOT/runtime/arch/arm"
SR="$ROOT/vendor/sr/SR/llasm-support"
ENTRY="${ENTRY_DIR:-$ROOT/build/lift-full/entry}"
WORK="$ROOT/build/runtime-arm"
G="$WORK/game"; OBJ="$G/obj"; SP="$WORK/shim-port"
SC_REASM="${SC_REASM:-$HOME/dev/sc-reasm}"
TOOLKIT="${TOOLKIT:-$HOME/dev/reasm-toolkit}"
SYSROOT="${SYSROOT:-$ROOT/build/sysroot}"
LIB="$SYSROOT/usr/lib/arm-linux-gnueabihf"
LLD="${LLD:-$HOME/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld}"
CLANG="${CLANG:-clang}"
TRIPLE=armv7a-unknown-linux-gnueabihf
CPU_FLAGS='-march=armv7-a -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm'
read -r -a CPU <<< "$CPU_FLAGS"
CC=("$CLANG" --target="$TRIPLE" "${CPU[@]}" --sysroot="$SYSROOT")
CFLAGS=(-O1 -g -Wall -Wno-unused-parameter)
INC="$WORK/sdl2-include"

say() { printf '[runtime-armv7] %s\n' "$*"; }
die() { printf '[runtime-armv7] ERRORE: %s\n' "$*" >&2; exit 1; }

for f in "$ENTRY/StarCraft.entry.o" "$ENTRY/storm.entry.o" "$ENTRY/guest_data.ld" \
         "$ENTRY/guest_pe_bytes.o" "$ENTRY/indirect_dispatch.o" "$ENTRY/indirect_dispatch_asm.o"; do
  [[ -s "$f" ]] || die "manca $f (esegui tools/build-entry-arm.sh)"
done
[[ -d "$INC/SDL2" ]] || die "overlay header SDL2 assente: $INC"
[[ -x "$LLD" ]] || die "ld.lld non trovato (imposta LLD=...)"
mkdir -p "$G" "$OBJ" "$SP/tools/elf" "$SP/obj"

# ---- 1) shim C: copia del sorgente esterno, patch 0001/0002/0003, compilazione ARM --------
say "shim C: copia + patch + compilazione"
rm -rf "$SP"; mkdir -p "$SP/tools/elf" "$SP/obj"
# Lo snapshot locale runtime/shim-port/elf e' gia' il port ARM degli shim (seam __arm__,
# callback del guest, GetProcAddress->thunk, SnpQuery/SnpBind esportate): si copia, non si patcha.
cp "$ROOT"/runtime/shim-port/elf/*.c "$ROOT"/runtime/shim-port/elf/*.h "$SP/tools/elf/"
rm -f "$SP"/tools/elf/*_test.c

shim_files=(win32_shim win32_ui gdi_elf com_ddraw com_dsound host_elf dlg_elf input_elf
            win32_game smack_elf smack_audio snp_elf win32_shell_dlg sc_ctl)
shim_present=()
for f in "${shim_files[@]}"; do
  [[ -f "$SP/tools/elf/$f.c" ]] || { say "  shim assente nel check-out esterno: $f"; continue; }
  shim_present+=("$f")
  extra=()
  [[ "$f" == win32_ui ]] && extra=(-DSC_ELF_REAL_GDI=1)
  "${CC[@]}" "${CFLAGS[@]}" -Wno-ignored-attributes -Wno-incompatible-library-redeclaration \
      -Wno-string-compare -Wno-self-assign -Wno-builtin-requires-header \
      -isystem "$INC" -I "$SP/tools/elf" -I "$TOOLKIT/runtime/elf" -I "$RT" "${extra[@]}" \
      -c -o "$SP/obj/$f.o" "$SP/tools/elf/$f.c" || die "compilazione shim $f"
done
shim_files=("${shim_present[@]}")

# ---- 2) runtime del port: vendor LLASM + runtime/common + assembly -----------------------
say "runtime del port"
rm -rf "$OBJ"; mkdir -p "$OBJ"
for f in "$SR"/llasm_*.c; do
  [[ "$(basename "$f")" == llasm_cmps.c ]] && f="$RT/llasm_cmps.c"
  "${CC[@]}" "${CFLAGS[@]}" -I "$SR" -c -o "$OBJ/$(basename "${f%.c}").o" "$f"
done
for f in cpu_llasm.c game_memory_arm.c x86_helpers.c bridge_arm.c elf_support_arm.c \
         x87_crt_arm.c crt_init_arm.c guest_call_arm.c win32_ext_arm.c storm_scomp.c \
         storm_sdraw_arm.c com_bridge_arm.c indirect_dispatch.c; do
  "${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$G" -c -o "$OBJ/${f%.c}.o" "$RT/$f"
done
for f in indirect_dispatch.S com_bridge.S tls.S guest_abs_zero.S; do
  "${CC[@]}" -c -o "$OBJ/${f%.S}_asm.o" "$ARM/$f"
done
"${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$G" -c -o "$OBJ/game_main_arm.o" "$RT/game_main_arm.c"
"${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$G" -c -o "$OBJ/pe_headers_arm.o" "$G/pe_headers_arm.c"

# ---- 3) cosa manca al link: riferimenti dei due oggetti meno definiti -------------------
say "calcolo dei riferimenti"
need="$G/need.txt"; defined="$G/defined.txt"
llvm-nm -u "$ENTRY/StarCraft.entry.o" "$ENTRY/storm.entry.o" \
  "$ENTRY/guest_pe_bytes.o" "$ENTRY/indirect_dispatch.o" "$ENTRY/indirect_dispatch_asm.o" \
  | awk '{print $NF}' | grep -v ':$' | grep -vE '^reasm_text_(lo|hi)$' | sort -u > "$need"
llvm-nm --defined-only "$ENTRY/StarCraft.entry.o" "$ENTRY/storm.entry.o" \
  "$ENTRY/guest_pe_bytes.o" "$ENTRY/indirect_dispatch.o" "$ENTRY/indirect_dispatch_asm.o" \
  | awk 'NF>=3{print $3}' | sort -u > "$defined"
llvm-nm --defined-only "$OBJ"/*.o 2>/dev/null | awk 'NF>=3{print $3}' | sort -u >> "$defined"
{
  for a in "$LIB/libc.so.6" "$LIB/libm.so.6" "$LIB/libpthread.so.0" "$LIB/libdl.so.2" \
           "$LIB/libgcc_s.so.1" "$LIB/libSDL2-2.0.so.0"; do
    [[ -f "$a" ]] && llvm-nm --defined-only --dynamic "$a" 2>/dev/null | awk '{print $NF}' | sed 's/@.*//'
  done
} | sort -u > "$G/defined_libc.txt"
sort -u "$defined" "$G/defined_libc.txt" -o "$defined"
say "  $(wc -l < "$need") riferimenti, $(wc -l < "$defined") definiti ($(wc -l < "$G/defined_libc.txt") dal sysroot)"

# ---- 4) ABI guest per gli shim + stub + tabella del bridge ------------------------------
say "generazione ABI guest degli shim"
python3 "$RT/gen_shims_bridge.py" --need "$need" --defined "$defined" \
  --shim-dir "$SP/tools/elf" --shims "$(IFS=,; echo "${shim_files[*]}")" \
  --clang-flags "--target=$TRIPLE $CPU_FLAGS --sysroot=$SYSROOT -isystem $INC -I $SP/tools/elf -I $TOOLKIT/runtime/elf -I $RT -I $G -Wno-ignored-attributes -DSC_ELF_REAL_GDI=1" \
  --extra-def "$RT/bridge_unk.def" --dynamic "$RT/bridge_dynamic.def" \
  --hand-def "$RT/bridge_table.def" \
  --out-def "$G/bridge_shims.def" --out-rename "$G/shim_rename.txt" \
  --out-impl "$G/shim_calls.c" || die "generazione del bridge degli shim"
"${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$G" -c -o "$OBJ/shim_calls.o" "$G/shim_calls.c"
python3 "$RT/gen_bridge_stubs.py" --arch arm --def "$RT/bridge_table.def" \
  --also "$G/bridge_shims.def" --only-needed "$need" \
  --out "$G/bridge_stubs.S" --csym-out "$G/bridge_symbols.c" --table-out "$G/bridge_table.c" \
  || die "generazione degli stub del bridge"
"${CC[@]}" -c -o "$OBJ/bridge_stubs.o" "$G/bridge_stubs.S"
for f in bridge_symbols bridge_table; do
  "${CC[@]}" "${CFLAGS[@]}" -I "$RT" -I "$G" -c -o "$OBJ/$f.o" "$G/$f.c"
done
python3 "$RT/gen_storm_ordinals.py" --json "$SC_REASM/analysis/storm_ordinals.json" \
  --out "$G/storm_ordinals_arm.c"
"${CC[@]}" "${CFLAGS[@]}" -c -o "$OBJ/storm_ordinals_arm.o" "$G/storm_ordinals_arm.c"

for o in "$OBJ"/*.o "$SP"/obj/*.o; do
  LC_ALL=C readelf -h "$o" | grep -q 'Class:.*ELF32' || die "non ELF32: $o"
  LC_ALL=C readelf -h "$o" | grep -q 'Machine:.*ARM' || die "non ARM: $o"
done
say "  $(find "$OBJ" -maxdepth 1 -name '*.o' | wc -l) oggetti runtime, $(find "$SP/obj" -name '*.o' | wc -l) shim"

# ---- 5) link completo ------------------------------------------------------------------
say "link del gioco"
# LLD vuole un ordine dei VA monotono: come fa tools/link-x86.sh, le sezioni guest (ordinate per VA)
# vanno PRIMA del .text del port, altrimenti l'offset di file diventa negativo ("output file too
# large"). Si passa a link-arm.sh una dir di link con il solo script trasformato.
LE="$G/link-entry"; rm -rf "$LE"; mkdir -p "$LE"
for f in "$ENTRY"/*.o; do ln -sfn "$f" "$LE/$(basename "$f")"; done
# lld confronta i pattern del linker script con il path degli input *come passati*: gli oggetti
# arrivano dal farm di symlink "$LE", quindi anche i path nel .ld vanno riscritti su "$LE",
# altrimenti le sezioni `.guest_storm_rdata`/`.guest_storm_data` (dati storm ai VA originali)
# finiscono nel .data del port e i VA guest restano non mappati (fault a 0x1505FC20).
sed -e 's/INSERT AFTER \.text/INSERT BEFORE .text/' -e "s|$ENTRY/|$LE/|g" "$ENTRY/guest_data.ld" > "$LE/guest_data.ld"
ENTRY_DIR="$LE" RUNTIME_OBJ_DIR="$OBJ" SYSROOT="$SYSROOT" LLD="$LLD" \
  ARM_ARCH=armv7 TRIPLE="$TRIPLE" CPU_FLAGS="$CPU_FLAGS" \
  "$ROOT/tools/link-arm.sh" "${OUT:-$ROOT/build/starcraft-armv7}"
