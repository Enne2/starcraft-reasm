#!/usr/bin/env bash
# Link dello StarCraft ARMv6 con gli shim già compilati.
#
# Il punto che rompeva il link: gli oggetti degli shim in
# build/runtime-arm/shim-port/obj/ hanno i simboli col nome originale
# (AddAccessAllowedAce), mentre bridge_stubs.S chiama il thunk guest-ABI
# (AddAccessAllowedAce__c). Il rename è previsto da gen_shims_bridge.py ed è
# materializzato in build/runtime-arm/game/shim_rename.txt: va applicato con
# llvm-objcopy --redefine-syms. Senza questo passaggio gli shim non agganciano
# e il link riporta centinaia di simboli mancanti.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
S="${SYSROOT:-$B/sysroot}"
ARM_ARCH="${ARM_ARCH:-armv6}"
TRIPLE="${TRIPLE:-armv6-unknown-linux-gnueabihf}"
CPU_FLAGS="${CPU_FLAGS:--march=armv6zk -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm}"
read -r -a CPU_FLAGS_ARR <<< "$CPU_FLAGS"
LIB="$S/usr/lib/arm-linux-gnueabihf"
LLD="${LLD:-$(ls ~/.rustup/toolchains/*/lib/rustlib/*/bin/gcc-ld/ld.lld | head -1)}"
OUT="${1:-$B/starcraft-$ARM_ARCH}"

# Artefatti delle immagini tradotte.
#   ENTRY_DIR vuota  -> pipeline storica (CHECKPOINT.md): StarCraft.entry.o + storm.o
#   ENTRY_DIR=<dir>  -> tools/build-entry-obj.sh: StarCraft.entry.o + storm.entry.o e la
#                       dispatch dei salti indiretti (runtime/common/indirect_dispatch.[cS])
ENTRY_DIR="${ENTRY_DIR:-}"
if [[ -n "$ENTRY_DIR" ]]; then
  IMG_OBJS=("$ENTRY_DIR/StarCraft.entry.o" "$ENTRY_DIR/storm.entry.o"
            "$ENTRY_DIR/indirect_dispatch.o" "$ENTRY_DIR/indirect_dispatch_asm.o")
  # Byte dell'immagine PE che il lift non colloca (.rsrc, .reloc, buchi di .text/.rdata):
  # guest_data.ld li colloca ai VA originali, quindi l'oggetto deve essere sulla riga di link
  # (ld.lld non tiene conto di un input che non gli viene passato) — cfr.
  # work/link/REPORT-flash-game.md §0.
  if [[ -s "$ENTRY_DIR/guest_pe_bytes.o" ]]; then
    IMG_OBJS+=("$ENTRY_DIR/guest_pe_bytes.o")
  fi
else
  IMG_OBJS=("$B/lift-full/StarCraft.entry.o" "$B/lift-full/storm.o")
fi
for f in "${IMG_OBJS[@]}"; do
  [[ -s "$f" ]] || { echo "manca $f" >&2; exit 2; }
done

# Il glue era compilato a mano in /tmp (CHECKPOINT.md §5): lo si ricompila qui, così il link non
# dipende da un file fuori dal repository.
GLUE="$B/link/game_glue.o"
mkdir -p "$B/link"
clang --target="$TRIPLE" "${CPU_FLAGS_ARR[@]}" \
      --sysroot="$S" -O2 -I "$ROOT/runtime/common" -c "$ROOT/runtime/common/game_glue.c" -o "$GLUE"

REN="$(mktemp -d)"
trap 'rm -rf "$REN"' EXIT

# 1. thunk guest-ABI: rinomina i simboli degli shim in <sym>__c
for f in "$B"/runtime-arm/shim-port/obj/*.o; do
  llvm-objcopy --redefine-syms="$B/runtime-arm/game/shim_rename.txt" "$f" "$REN/$(basename "$f")"
done

# 2. link: lift + lato guest-ABI (game/obj) + shim rinominati + libc/SDL2 dal sysroot
#
# Layout a due spazi disgiunti (work/link/REPORT-flash-crt.md §1):
#   * codice del port a IMG_BASE=0x18000000 (--section-start=.text), fuori da immagine guest
#     0x00400000-0x006EBC28, storm 0x15000000+, stack guest 0x02000000+, arena 0x0E000000+;
#   * dati guest ai VA originali del PE, piazzati da $ENTRY_DIR/guest_data.ld (INSERT), perche'
#     il codice liftato indirizza i dati con costanti assolute e con gli alias @loc_<VA>.
# Senza guest_data.ld il link resta quello "storico" (dati nel .data del port).
# LDFLAGS_EXTRA: parametri aggiuntivi per ld.lld.
IMG_BASE="${IMG_BASE:-0x18000000}"
LAYOUT=()
if [[ -n "$ENTRY_DIR" && -s "$ENTRY_DIR/guest_data.ld" ]]; then
  LAYOUT=(--section-start=".text=$IMG_BASE" -T "$ENTRY_DIR/guest_data.ld")
fi
read -r -a EXTRA <<< "${LDFLAGS_EXTRA:-}"
"$LLD" -m armelf_linux_eabi --dynamic-linker /lib/ld-linux-armhf.so.3 -z muldefs \
  ${LAYOUT[@]+"${LAYOUT[@]}"} ${EXTRA[@]+"${EXTRA[@]}"} \
  -L"$LIB" \
  "$LIB/Scrt1.o" "$LIB/crti.o" \
  "${IMG_OBJS[@]}" \
  "${RUNTIME_OBJ_DIR:-$B/runtime-arm/game/obj}"/*.o \
  "$GLUE" \
  "$REN"/*.o \
  "$LIB/libc.so.6" "$LIB/libm.so.6" "$LIB/libgcc_s.so.1" "$LIB/libSDL2-2.0.so.0" \
  "$LIB/crtn.o" -o "$OUT" 2>"$B/link-errors.txt" || {
    echo "link FALLITO — simboli mancanti:" >&2
    grep -oE "undefined symbol: [^ ]+" "$B/link-errors.txt" | sed 's/.*symbol: //' | sort -u >&2
    exit 1
  }
if [[ "${SC_SPLIT_DEBUG:-0}" == 1 ]]; then
  OBJCOPY="${OBJCOPY:-$(command -v llvm-objcopy || command -v objcopy || true)}"
  [[ -n "$OBJCOPY" ]] || { echo 'llvm-objcopy/objcopy non trovato per SC_SPLIT_DEBUG=1' >&2; exit 2; }
  DEBUG="$OUT.debug"
  TMP="$OUT.split-debug.tmp"
  "$OBJCOPY" --only-keep-debug "$OUT" "$DEBUG"
  "$OBJCOPY" --strip-debug "$OUT" "$TMP"
  "$OBJCOPY" --add-gnu-debuglink="$DEBUG" "$TMP"
  mv "$TMP" "$OUT"
  echo "debug separato: $DEBUG"
fi
echo "creato: $OUT"
