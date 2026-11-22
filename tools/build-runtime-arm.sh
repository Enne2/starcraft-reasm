#!/usr/bin/env bash
# tools/build-runtime-arm.sh — environment + runtime + smoke test for the ARMv6 (Raspberry Pi OS)
# target of the StarCraft static recompilation.
#
# Owner of: runtime/common/, build/sysroot/, build/runtime-arm/. Does not touch sc-reasm, m-ht-sr or
# the vendor snapshot: the shims are copied into the build tree and patched there (see
# runtime/common/patches/).
#
# Stages (each one independent, all idempotent):
#   1  sysroot        extract the needed trees from the read-only Raspberry Pi OS image
#   2  sdl2 headers   overlay that makes the host SDL2 headers usable for __arm__
#   3  probe          plain C + SDL2 program: proves the sysroot links and runs under qemu
#   4  runtime        vendor LLASM helpers + runtime/common/*.c + generated bridge tables
#   5  guest          llasm -> LLVM IR -> ARMv6 object (smoke_guest.llasm)
#   6  smoke          link the ARMv6 ELF, run it under `qemu-arm-static -cpu arm1176 -L sysroot`
#   7  shim port      14 sc-reasm C shims compiled for ARM (with the documented patch) + link check
#   8  game           link the whole game: translated StarCraft.exe + storm.dll + runtime + shims
#   9  boot           run it under qemu-arm-static with the MPQ files symlinked from GAME_DIR
#
# Usage:  tools/build-runtime-arm.sh [--stage N] [--keep-mount] [--check]
#   --stage N     run stages 1..N (default 9)
#   --keep-mount  leave build/sysroot-mnt mounted (debugging)
#   --check       verify only: sysroot present, toolchain present, nothing written
#
# Environment: QEMU_ARM (default qemu-arm-static), LLD (default ld.lld from the ROCm LLVM tree or
# a wasi-sdk copy — anything that is LLVM lld 15+), IMAGE, SC_REASM, TOOLKIT.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-$ROOT/build/qemu/2026-04-13-raspios-bookworm-armhf.img}"
SYSROOT="${SYSROOT:-$ROOT/build/sysroot}"
MNT="${MNT:-$ROOT/build/sysroot-mnt}"
WORK="${WORK:-$ROOT/build/runtime-arm}"
SR="${SR:-$ROOT/vendor/sr/SR/llasm-support}"
RT="$ROOT/runtime/common"
SC_REASM="${SC_REASM:-$HOME/dev/sc-reasm}"
TOOLKIT="${TOOLKIT:-$HOME/dev/reasm-toolkit}"
QEMU_ARM="${QEMU_ARM:-qemu-arm-static}"
PY=python3

# ---- partition geometry of the official image (read with a 512-byte MBR read, no root) ----
PART2_OFFSET=545259520        # 1064960 * 512, verified by fdisk on the image

STAGE=9
KEEP_MOUNT=0
CHECK_ONLY=0
while (($#)); do
  case "$1" in
    --stage) STAGE="$2"; shift 2 ;;
    --keep-mount) KEEP_MOUNT=1; shift ;;
    --check) CHECK_ONLY=1; shift ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "opzione sconosciuta: $1" >&2; exit 2 ;;
  esac
done

say()  { printf '[runtime-arm] %s\n' "$*"; }
die()  { printf '[runtime-arm] ERRORE: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------------------------
find_lld() {
  local c
  for c in "${LLD:-}" /usr/lib64/rocm/llvm/bin/ld.lld /usr/lib/llvm*/bin/ld.lld \
           "$HOME"/src/wasi-sdk-*/bin/ld.lld; do
    [[ -n "$c" && -x "$c" ]] && { echo "$c"; return 0; }
  done
  return 1
}
LLD="${LLD:-$(find_lld || true)}"

# ARM profiles are selected by tools/build.sh and inherited by all build stages.
ARM_ARCH="${ARM_ARCH:-armv6}"
TRIPLE="${TRIPLE:-armv6-unknown-linux-gnueabihf}"
CPU_FLAGS="${CPU_FLAGS:--march=armv6zk -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm}"
LLC_FLAGS="${LLC_FLAGS:--mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard}"
QEMU_CPU="${QEMU_CPU:-arm1176}"
read -r -a CPU_FLAGS_ARR <<< "$CPU_FLAGS"
read -r -a LLC_FLAGS_ARR <<< "$LLC_FLAGS"
CC_ARM=(clang --target="$TRIPLE" "${CPU_FLAGS_ARR[@]}" --sysroot="$SYSROOT")
CFLAGS_ARM=(-O1 -g -Wall -Wno-unused-parameter)
LLC_ARM=(llc -mtriple="$TRIPLE" "${LLC_FLAGS_ARR[@]}")
LINK_ARM=("${CC_ARM[@]}" "--ld-path=$LLD")
GAME_ELF="$WORK/starcraft-$ARM_ARCH"

stage_check() {
  say "verifica prerequisiti"
  for t in clang llc opt "$QEMU_ARM" "$PY" fuse2fs debugfs; do
    command -v "$t" >/dev/null || die "tool mancante: $t"
  done
  [[ -n "$LLD" && -x "$LLD" ]] || die "ld.lld non trovato (imposta LLD=...)"
  "$LLD" --version | head -1
  [[ -r "$IMAGE" ]] || die "immagine assente: $IMAGE"
  [[ -x "$ROOT/build/pipeline/bin/llasm" ]] || die "llasm non costruito: esegui tools/build-tools.sh"
  if [[ -d "$SYSROOT/usr/include" ]]; then
    say "sysroot presente: $(du -sh "$SYSROOT" | cut -f1)"
  else
    say "sysroot da estrarre (stage 1)"
  fi
  say "toolchain ARMv6 OK"
}

# ---------------------------------------------------------------------------------------------
stage1_sysroot() {
  if [[ -f "$SYSROOT/usr/lib/ld-linux-armhf.so.3" && -f "$SYSROOT/usr/lib/gcc/arm-linux-gnueabihf/12/libgcc.a" ]]; then
    say "stage 1: sysroot già estratto ($(du -sh "$SYSROOT" | cut -f1))"
    return 0
  fi
  say "stage 1: estrazione del sysroot da $(basename "$IMAGE") (read-only, senza sudo)"
  mkdir -p "$SYSROOT" "$MNT"
  if ! mountpoint -q "$MNT"; then
    fuse2fs -o ro,offset="$PART2_OFFSET" "$IMAGE" "$MNT" \
      || die "fuse2fs non riesce a montare la partizione 2 (offset $PART2_OFFSET)"
    sleep 1
  fi
  mkdir -p "$SYSROOT/usr/lib/arm-linux-gnueabihf" "$SYSROOT/usr/include" \
           "$SYSROOT/usr/lib/gcc/arm-linux-gnueabihf" "$SYSROOT/usr/lib/linux"
  # multiarch runtime libraries (glibc, libSDL2, Mesa, ALSA, X11 ...) — 1:1 copy
  cp -a "$MNT/usr/lib/arm-linux-gnueabihf/." "$SYSROOT/usr/lib/arm-linux-gnueabihf/"
  # headers (glibc + kernel UAPI via the symlink farm in usr/lib/linux/uapi)
  cp -a "$MNT/usr/include/." "$SYSROOT/usr/include/"
  cp -a "$MNT/usr/lib/linux/uapi" "$SYSROOT/usr/lib/linux/"
  # crt1.o/crti.o/crtn.o, libc.so linker scripts and libgcc/crtbegin (gcc 12 as shipped)
  cp -a "$MNT/usr/lib/gcc/arm-linux-gnueabihf/12" "$SYSROOT/usr/lib/gcc/arm-linux-gnueabihf/"
  # /lib -> /usr/lib (bookworm merged-usr) and the dynamic loader at the canonical path
  [[ -L "$SYSROOT/lib" ]] || ln -sfn usr/lib "$SYSROOT/lib"
  ln -sfn arm-linux-gnueabihf/ld-linux-armhf.so.3 "$SYSROOT/usr/lib/ld-linux-armhf.so.3"
  # linker name for SDL2: the image ships the runtime object only (no -dev package), so the
  # conventional "libSDL2.so -> libSDL2-2.0.so.0" symlink has to be created to allow -lSDL2.
  ln -sfn libSDL2-2.0.so.0.2600.5 "$SYSROOT/usr/lib/arm-linux-gnueabihf/libSDL2.so"
  if (( ! KEEP_MOUNT )); then
    umount "$MNT" 2>/dev/null || fusermount3 -u "$MNT" 2>/dev/null || true
    rmdir "$MNT" 2>/dev/null || true      # leave no empty mount point behind
  fi
  say "stage 1: sysroot pronto ($(du -sh "$SYSROOT" | cut -f1))"
}

# ---------------------------------------------------------------------------------------------
stage2_sdl2_headers() {
  local inc="$WORK/sdl2-include"
  say "stage 2: overlay header SDL2 (host $(grep -m1 'define SDL_MINOR_VERSION' /usr/include/SDL2/SDL_version.h | awk '{print $3}').x)"
  mkdir -p "$inc/SDL2"
  cp /usr/include/SDL2/*.h "$inc/SDL2/"
  # SDL_config.h dispatches on __arm__ to SDL_config-arm.h, which the host package does not
  # ship (it is one file per Fedora arch). We provide the neutral one from runtime/common/include.
  cp "$RT/include/sdl2-arm/SDL_config-arm.h" "$inc/SDL2/"
  say "stage 2: header pronti in ${inc#$ROOT/}"
}

# ---------------------------------------------------------------------------------------------
stage3_probe() {
  local out="$WORK"
  say "stage 3: programma C+SDL2 nudo, per validare il sysroot da solo"
  "${LINK_ARM[@]}" "${CFLAGS_ARM[@]}" -isystem "$WORK/sdl2-include" \
      -o "$out/sysroot_probe.elf" "$RT/tests/sysroot_probe.c" -lSDL2 || die "link del probe fallito"
  file "$out/sysroot_probe.elf" | cut -c1-120
  LC_ALL=C readelf -A "$out/sysroot_probe.elf" | grep -E 'Tag_CPU_arch:|Tag_FP_arch:|Tag_ABI_VFP_args:' || true
  SDL_VIDEODRIVER=dummy "$QEMU_ARM" -cpu "$QEMU_CPU" -L "$SYSROOT" "$out/sysroot_probe.elf" \
    || die "il probe C+SDL2 non gira sotto qemu-arm-static -cpu $QEMU_CPU"
}

# ---------------------------------------------------------------------------------------------
stage4_runtime() {
  local obj="$WORK/obj"
  say "stage 4: runtime ARMv6 (helper LLASM del vendor + runtime/common)"
  mkdir -p "$obj"
  # 4a generated sources
  "$PY" "$RT/gen_bridge_stubs.py" --def "$RT/bridge_table.def" \
        --out "$WORK/bridge_stubs.S" --csym-out "$WORK/bridge_symbols.c" \
        --table-out "$WORK/bridge_table.c"
  local ordjson="$SC_REASM/analysis/storm_ordinals.json"
  [[ -f "$ordjson" ]] || say "stage 4: ATTENZIONE $ordjson assente: tabella ordinali vuota"
  "$PY" "$RT/gen_storm_ordinals.py" --json "$ordjson" --out "$WORK/storm_ordinals_arm.c"
  # 4b the 11 architecture-neutral reference TUs from SR (133 helpers, no reimplementation)
  local f
  for f in "$ROOT"/runtime/common/llasm_cmps.c "$SR"/llasm_divide.c "$SR"/llasm_float.c "$SR"/llasm_fs_mem.c \
           "$SR"/llasm_inout.c "$SR"/llasm_int.c "$SR"/llasm_movmem.c "$SR"/llasm_movs.c \
           "$SR"/llasm_pushx.c "$SR"/llasm_scas.c "$SR"/llasm_stos.c; do
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -c -o "$obj/$(basename "${f%.c}").o" "$f"
  done
  # 4c our platform layer
  for f in cpu_llasm.c game_memory_arm.c x86_helpers.c bridge_arm.c elf_support_arm.c \
           x87_crt_arm.c crt_init_arm.c guest_call_arm.c win32_ext_arm.c storm_scomp.c; do
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -c -o "$obj/${f%.c}.o" "$RT/$f"
  done
  "${CC_ARM[@]}" -c -o "$obj/bridge_stubs.o" "$WORK/bridge_stubs.S"
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -c -o "$obj/bridge_symbols.o" "$WORK/bridge_symbols.c"
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -c -o "$obj/bridge_table.o" "$WORK/bridge_table.c"
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -c -o "$obj/storm_ordinals_arm.o" "$WORK/storm_ordinals_arm.c"
  llvm-ar rcs "$WORK/libruntime-arm.a" "$obj"/*.o
  say "stage 4: $(llvm-nm --defined-only "$WORK/libruntime-arm.a" | grep -c ' T ') simboli di testo, \
$(ls "$obj" | wc -l) oggetti"
}

# ---------------------------------------------------------------------------------------------
stage5_guest() {
  say "stage 5: guest LLASM -> IR -> oggetto ARMv6"
  mkdir -p "$WORK/guest"
  "$ROOT/build/pipeline/bin/llasm" -I "$SR" -o "$WORK/guest/smoke_guest.ll" \
      "$RT/tests/smoke_guest.llasm"
  "$ROOT/tools/compile-arm.sh" "$WORK/guest/smoke_guest.ll" "$WORK/guest/smoke_guest.o" >/dev/null
  LC_ALL=C readelf -h "$WORK/guest/smoke_guest.o" | grep -E 'Class:|Machine:|Type:' | sed 's/^/    /'
}

# ---------------------------------------------------------------------------------------------
stage6_smoke() {
  say "stage 6: link ed esecuzione dello smoke test ARMv6"
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -isystem "$WORK/sdl2-include" -I "$RT" \
      -c -o "$WORK/obj/smoke_main.o" "$RT/tests/smoke_main.c"
  "${LINK_ARM[@]}" -o "$WORK/smoke.elf" "$WORK"/obj/*.o "$WORK/guest/smoke_guest.o" \
      -lSDL2 -lm
  file "$WORK/smoke.elf" | cut -c1-130
  SDL_VIDEODRIVER=dummy "$QEMU_ARM" -cpu "$QEMU_CPU" -L "$SYSROOT" "$WORK/smoke.elf"
  local rc=$?
  [[ $rc == 0 ]] || die "smoke test fallito (exit $rc)"
  # The same binary must also come up with the offscreen driver (no display in CI).
  say "stage 6: secondo giro con SDL_VIDEODRIVER=offscreen"
  SDL_VIDEODRIVER=offscreen "$QEMU_ARM" -cpu "$QEMU_CPU" -L "$SYSROOT" "$WORK/smoke.elf" \
      >"$WORK/smoke-offscreen.log" 2>&1 && say "    offscreen: PASS" \
      || say "    offscreen: non disponibile in questa SDL (vedi smoke-offscreen.log)"
}

# ---------------------------------------------------------------------------------------------
stage7_shim_port() {
  local stage="$WORK/shim-port"
  say "stage 7: port degli shim C di sc-reasm ($SC_REASM/tools/elf)"
  [[ -d "$SC_REASM/tools/elf" ]] || die "shim non trovati in $SC_REASM/tools/elf"
  rm -rf "$stage"
  mkdir -p "$stage/tools/elf"
  cp "$SC_REASM"/tools/elf/*.c "$SC_REASM"/tools/elf/*.h "$stage/tools/elf/"
  rm -f "$stage"/tools/elf/*_test.c
  ( cd "$stage" && patch -p1 --no-backup-if-mismatch < "$RT/patches/0001-i386-only-seams-to-arm.patch" ) \
      || die "la patch del port non si applica: il sorgente sc-reasm è cambiato"
  mkdir -p "$stage/obj"
  local files=(win32_shim.c win32_ui.c gdi_elf.c com_ddraw.c com_dsound.c host_elf.c dlg_elf.c
               input_elf.c win32_game.c main_elf.c smack_elf.c smack_audio.c snp_elf.c storm_ext.c)
  local f fails=0
  for f in "${files[@]}"; do
    local extra=()
    [[ "$f" == win32_ui.c ]] && extra=(-DSC_ELF_REAL_GDI=1)   # as tools/elf/build.sh does
    if "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -Wno-ignored-attributes -Wno-incompatible-library-redeclaration \
        -isystem "$WORK/sdl2-include" -I "$stage/tools/elf" -I "$TOOLKIT/runtime/elf" -I "$RT" \
        "${extra[@]}" -c -o "$stage/obj/${f%.c}.o" "$stage/tools/elf/$f" 2>"$stage/${f%.c}.log"; then
      printf '    %-16s ok\n' "$f"
    else
      printf '    %-16s FALLITO (vedi %s)\n' "$f" "${f%.c}.log"; fails=$((fails+1))
    fi
  done
  (( fails == 0 )) || die "$fails sorgenti shim non compilano per ARMv6"
  say "stage 7: link relocatable degli shim (coerenza dei simboli interni)"
  "$LLD" -r -o "$stage/shim_port_all.o" "$stage"/obj/*.o
  llvm-nm --defined-only "$stage/shim_port_all.o" | awk '{print $3}' | sort -u > "$stage/defined.txt"
  llvm-nm --defined-only "$WORK/libruntime-arm.a" | awk '{print $3}' | sort -u >> "$stage/defined.txt"
  sort -u -o "$stage/defined.txt" "$stage/defined.txt"
  llvm-nm -u "$stage/shim_port_all.o" | awk '{print $2}' | sort -u > "$stage/undef.txt"
  comm -23 "$stage/undef.txt" "$stage/defined.txt" > "$stage/undef-external.txt"
  say "    $(wc -l < "$stage/defined.txt") simboli definiti (shim + runtime), \
$(wc -l < "$stage/undef-external.txt") riferimenti esterni"
  say "    restano da fornire a link time (libc/libgcc/SDL2 + loader x86):"
  say "      $(tr '\n' ' ' < "$stage/undef-external.txt" | grep -oE '\b(loader_init|teb_[a-z_]*|g_pehdrs|g_imports[a-z_]*|reasm_iat_install|reasm_storm_ordinal_va|reasm_call_on_stack|winmain_trap|build_guard_check)\b' | sort -u | tr '\n' ' ')"
}

# ---------------------------------------------------------------------------------------------
# Stage 8 — link the whole game: translated StarCraft.exe + storm.dll + runtime + ARMv6 shims.
#
# Inputs already in the tree (produced by tools/lift-full.sh, owned by the lifting workstream):
#   build/lift-full/pipeline/{StarCraft,storm}.optimized.ll
# Nothing in sc-reasm or in the lifting outputs is modified: the IR is post-processed into
# build/runtime-arm/game/ (entry points made linkable) and the shims are patched on a copy.
stage8_game() {
  local G="$WORK/game" OBJ="$WORK/game/obj" SP="$WORK/game/shim"
  local IRX="$ROOT/build/lift-full/pipeline/StarCraft.optimized.ll"
  local IRS="$ROOT/build/lift-full/pipeline/storm.optimized.ll"
  local PE_X="$ROOT/build/lift-full/StarCraft.orig.exe"
  local PE_S="$ROOT/build/lift-full/storm.orig.dll"

  say "stage 8: link del gioco completo (immagini tradotte + runtime + shim)"
  [[ -s "$IRX" && -s "$IRS" ]] || die "IR del lifting assente: esegui tools/lift-full.sh all exe/storm"
  [[ -s "$PE_X" && -s "$PE_S" ]] || die "PE originali assenti in build/lift-full"
  mkdir -p "$G" "$OBJ" "$SP"

  # 8.1 i punti d'ingresso diventano linkabili (`private` -> `hidden`); nient'altro cambia.
  "$PY" - "$IRX" "$IRS" "$G/StarCraft.ll" "$G/storm.ll" <<'PYEOF'
import sys
src_x, src_s, dst_x, dst_s = sys.argv[1:5]
def post(src, dst, entries):
    """`private` -> `hidden` per le procedure del guest.

    Due motivi, entrambi misurati:
      * il linker elimina dal symtab i simboli locali non richiesti: con `private` restavano solo
        2 `loc_*` e la mappa VA originale -> indirizzo ARM (servita da patch_guest_pointers.py per
        riscrivere i puntatori a codice nei dati) era vuota;
      * gli entry point devono essere chiamabili dal main C.
    `hidden` = visibile al link, non esportato dinamicamente: nessun costo a runtime.
    """
    n_entry = n_all = 0
    with open(src, errors="replace") as fi, open(dst, "w") as fo:
        for line in fi:
            if line.startswith("define private fastcc void @loc_"):
                line = line.replace("define private fastcc", "define hidden fastcc", 1)
                n_all += 1
                for e in entries:
                    if line.startswith("define hidden fastcc void @" + e + "("):
                        n_entry += 1
            elif line.startswith("define private fastcc void @"):
                for e in entries:
                    if line.startswith("define private fastcc void @" + e + "("):
                        line = line.replace("define private fastcc", "define hidden fastcc", 1)
                        n_entry += 1
            fo.write(line)
    return n_all, n_entry
print("    exe: procedure resi linkabili (hidden):", post(src_x, dst_x, ["loc_404C21", "loc_4E0AE0"]))
print("    storm: procedure resi linkabili (hidden):", post(src_s, dst_s, []))
PYEOF

  # 8.2 oggetti con una sezione per datasegment, cosi' il linker puo' metterli ai VA originali
  say "    llc --data-sections (2 immagini)"
  "${LLC_ARM[@]}" --data-sections -filetype=obj -o "$G/StarCraft.data.o" "$G/StarCraft.ll" \
      || die "llc su StarCraft.ll"
  "${LLC_ARM[@]}" --data-sections -filetype=obj -o "$G/storm.data.o" "$G/storm.ll" \
      || die "llc su storm.ll"

  # 8.3 layout ai VA originali + regioni per il runtime + header PE
  say "    layout dei VA originali"
  "$PY" "$RT/gen_game_layout.py" \
      --exe-obj "$G/StarCraft.data.o" --exe-ir "$G/StarCraft.ll" \
      --storm-obj "$G/storm.data.o" --storm-ir "$G/storm.ll" \
      --pe-exe "$PE_X" --pe-storm "$PE_S" \
      --out-ld "$G/guest_layout.ld" --out-header "$G/guest_layout_arm.h" \
      --out-pe "$G/pe_headers_arm.c" || die "generazione del layout"

  # 8.4 shim patchati (0001 seam i386, 0002 callback del guest) e compilati per ARMv6
  say "    shim C: copia + patch 0001/0002 + compilazione"
  rm -rf "$SP"; mkdir -p "$SP/tools/elf" "$SP/obj"
  cp "$SC_REASM"/tools/elf/*.c "$SC_REASM"/tools/elf/*.h "$SP/tools/elf/"
  rm -f "$SP"/tools/elf/*_test.c
  ( cd "$SP" && for p in 0001-i386-only-seams-to-arm 0002-guest-callbacks \
                        0003-getprocaddress-bridge; do
                patch -p1 --no-backup-if-mismatch < "$RT/patches/$p.patch" || exit 1
              done ) \
      || die "le patch del port non si applicano: i sorgenti sc-reasm sono cambiati"
  # main_elf.c NON entra nel runtime ARM: e' il main del modello i386 (asm per lo stack basso,
  # trap nasm di WinMain, reasm_call_on_stack). Il main ARM e' runtime/common/game_main_arm.c.
  # storm_ext.c resta fuori: e' il livello di override degli ordinali storm del modello i386
  # (usa la tabella IAT generata g_imports e reasm_storm_ordinal_va); qui gli ordinali sono VA
  # dirette della storm tradotta e l'override non ha nulla da sostituire.
  local shim_files=(win32_shim win32_ui gdi_elf com_ddraw com_dsound host_elf dlg_elf input_elf
                    win32_game smack_elf smack_audio snp_elf)
  local f extra
  for f in "${shim_files[@]}"; do
    extra=()
    [[ "$f" == win32_ui ]] && extra=(-DSC_ELF_REAL_GDI=1)
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -Wno-ignored-attributes -Wno-incompatible-library-redeclaration \
        -Wno-string-compare -Wno-self-assign -Wno-builtin-requires-header \
        -isystem "$WORK/sdl2-include" -I "$SP/tools/elf" -I "$TOOLKIT/runtime/elf" -I "$RT" \
        "${extra[@]}" -c -o "$SP/obj/$f.o" "$SP/tools/elf/$f.c" || die "compilazione shim $f"
  done

  # 8.5 runtime del gioco: stessi TU dello stage 4, in una cartella separata (la tabella degli
  # import e' un'altra: qui contiene SHIM/UNK, li' solo le 17 voci a mano)
  say "    runtime ARMv6 (oggetti separati per il gioco)"
  rm -rf "$OBJ"; mkdir -p "$OBJ"     # nessun residuo della passata precedente: i simboli generati
                                     # (stub) non devono contare come "gia' definiti"
  for f in "$SR"/llasm_*.c; do
    [[ "$(basename "$f")" == llasm_cmps.c ]] && f="$ROOT/runtime/common/llasm_cmps.c"
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -c -o "$OBJ/$(basename "${f%.c}").o" "$f"
  done
  for f in cpu_llasm.c game_memory_arm.c x86_helpers.c bridge_arm.c elf_support_arm.c \
           x87_crt_arm.c crt_init_arm.c guest_call_arm.c win32_ext_arm.c storm_scomp.c; do
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -c -o "$OBJ/${f%.c}.o" "$RT/$f"
  done
  "$PY" "$RT/gen_storm_ordinals.py" --json "$SC_REASM/analysis/storm_ordinals.json" \
        --out "$G/storm_ordinals_arm.c"
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -c -o "$OBJ/storm_ordinals_arm.o" "$G/storm_ordinals_arm.c"
  # __fpmath = 0 (simbolo assoluto): va compilato PRIMA dell'elenco "defined", altrimenti il
  # generatore non sa che il simbolo e' coperto e lo tratta come mancante
  "${CC_ARM[@]}" -c -o "$OBJ/guest_abs_zero.o" "$ROOT/runtime/arch/arm/guest_abs_zero.S"

  # 8.6 cosa manca al link: simboli indefiniti dei due oggetti meno quelli gia' definiti
  local need="$G/need.txt" defined="$G/defined.txt" tmp="$G/defined.tmp"
  llvm-nm -u "$G/StarCraft.data.o" "$G/storm.data.o" | awk '{print $NF}' | grep -v ':$' \
      | sort -u > "$need"
  llvm-nm --defined-only "$G/StarCraft.data.o" "$G/storm.data.o" | awk 'NF>=3{print $3}' \
      | sort -u > "$defined"
  # solo il runtime: gli shim NON contano come definiti, i loro simboli diventano <nome>__c e il
  # nome dell'import deve restare libero per lo stub con ABI del guest
  llvm-nm --defined-only "$OBJ"/*.o 2>/dev/null | awk 'NF>=3{print $3}' | sort -u > "$tmp"
  # La libc del sysroot e libgcc forniscono i simboli che il codice C usa davvero (memset,
  # __aeabi_*): non sono import del guest e non vanno instradati nel bridge.
  {
    for a in "$SYSROOT/usr/lib/arm-linux-gnueabihf/libc.so.6" \
             "$SYSROOT/usr/lib/arm-linux-gnueabihf/libm.so.6" \
             "$SYSROOT/usr/lib/arm-linux-gnueabihf/libpthread.so.0" \
             "$SYSROOT/usr/lib/arm-linux-gnueabihf/libdl.so.2" \
             "$SYSROOT/usr/lib/arm-linux-gnueabihf/libgcc_s.so.1" \
             "$SYSROOT/usr/lib/arm-linux-gnueabihf/libgcc.a"; do
      [[ -f "$a" ]] && llvm-nm --defined-only --dynamic "$a" 2>/dev/null | awk '{print $NF}' | sed 's/@.*//'
    done
    llvm-nm --defined-only "$SYSROOT/usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0" 2>/dev/null \
      | awk '{print $NF}' | sed 's/@.*//'
  } | sort -u > "$G/defined_libc.txt"
  cat "$defined" "$tmp" "$G/defined_libc.txt" | sort -u > "$defined.new" && mv "$defined.new" "$defined"
  say "    $(wc -l < "$need") riferimenti nei due oggetti, $(wc -l < "$defined") simboli definiti"

  # 8.7 ABI guest per gli shim: arità dai prototipi C, UNK dichiarati, rinomina per objcopy
  say "    generazione della ABI guest per gli shim (arità dai prototipi)"
  "$PY" "$RT/gen_shims_bridge.py" --need "$need" --defined "$defined" \
      --shim-dir "$SP/tools/elf" --shims "$(IFS=,; echo "${shim_files[*]}")" \
      --clang-flags "--target=$TRIPLE $CPU_FLAGS --sysroot=$SYSROOT -isystem $WORK/sdl2-include -I $SP/tools/elf -I $TOOLKIT/runtime/elf -I $RT -Wno-ignored-attributes -DSC_ELF_REAL_GDI=1" \
      --extra-def "$RT/bridge_unk.def" --dynamic "$RT/bridge_dynamic.def" \
      --hand-def "$RT/bridge_table.def" \
      --out-def "$G/bridge_shims.def" --out-rename "$G/shim_rename.txt" \
      --out-impl "$G/shim_calls.c" || die "generazione del bridge degli shim"

  say "    rinomina dei simboli degli shim (<nome> -> <nome>__c) e stub guest-ABI"
  # la binutils di sistema non conosce ARM (BFD solo x86): si usa llvm-objcopy
  for f in "$SP/obj"/*.o; do
    llvm-objcopy --redefine-syms="$G/shim_rename.txt" "$f" || die "llvm-objcopy su $f"
  done
  "$PY" "$RT/gen_bridge_stubs.py" --def "$RT/bridge_table.def" --also "$G/bridge_shims.def" \
      --only-needed "$need" \
      --out "$G/bridge_stubs.S" --csym-out "$G/bridge_symbols.c" --table-out "$G/bridge_table.c"

  # 8.8 compilazione dei sorgenti generati + main del gioco
  "${CC_ARM[@]}" -c -o "$OBJ/bridge_stubs.o" "$G/bridge_stubs.S"
  for f in bridge_table bridge_symbols shim_calls pe_headers_arm; do
    "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -I "$G" -c -o "$OBJ/$f.o" "$G/$f.c"
  done
  "${CC_ARM[@]}" "${CFLAGS_ARM[@]}" -I "$RT" -I "$G" -c -o "$OBJ/game_main_arm.o" \
      "$RT/game_main_arm.c"

  # 8.9 link: script con INSERT (il layout del CRT/libc/runtime resta quello predefinito)
  say "    link (layout ai VA originali + codice guest in aree libere basse)"
  local link_args=(-no-pie -Wl,-z,max-page-size=0x1000 -T "$G/guest_layout.ld"
                   -o "$GAME_ELF" "$OBJ"/*.o "$SP/obj"/*.o
                   "$G/StarCraft.data.o" "$G/storm.data.o" -lSDL2 -lm -lpthread -ldl)
  "${LINK_ARM[@]}" "${link_args[@]}" || die "link di $GAME_ELF fallito"

  # 8.10 puntatori a codice rimasti *byte* nei segmenti dati delle immagini: si riscrivono nel
  # file, guidati dai simboli `loc_<VA>` del binario appena linkato (nessuna scrittura a runtime,
  # nessun segmento reso scrivibile)
  say "    puntatori a codice nei dati (patch nel file, guidata dai simboli loc_<VA>)"
  "$PY" "$RT/patch_guest_pointers.py" --elf "$GAME_ELF"
  file "$GAME_ELF" | cut -c1-130
  say "    $(llvm-nm --defined-only "$GAME_ELF" | wc -l) simboli definiti nel binario"
}

# ---------------------------------------------------------------------------------------------
# Stage 9 — boot del gioco sotto qemu-arm-static, con gli MPQ collegati da una cartella di lavoro.
stage9_boot() {
  local B="$WORK/boot" GAME_DIR="${GAME_DIR:-$HOME/dev/final-starcraft/Starcraft}"
  local SECS="${BOOT_TIMEOUT:-90}"
  say "stage 9: boot del gioco (qemu-arm-static -cpu $QEMU_CPU), timeout ${SECS}s"
  [[ -x "$GAME_ELF" ]] || die "$GAME_ELF assente: esegui lo stage 8"
  [[ -d "$GAME_DIR" ]] || die "cartella del gioco assente: $GAME_DIR"
  mkdir -p "$B"
  # collegamenti agli asset reali (nessuna copia: sono centinaia di MB)
  local found=0 a
  for a in "$GAME_DIR"/*.mpq "$GAME_DIR"/*.MPQ "$GAME_DIR"/*.snp; do
    [[ -e "$a" ]] || continue
    ln -sfn "$a" "$B/$(basename "$a")"; found=$((found+1))
  done
  say "    $found asset collegati in ${B#$ROOT/}"
  local rc=0
  ( cd "$B" && SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" timeout "$SECS" \
      "$QEMU_ARM" -cpu "$QEMU_CPU" -L "$SYSROOT" "$GAME_ELF" ) \
      >"$B/boot.log" 2>&1 || rc=$?
  say "    exit $rc (124 = timeout: il gioco era ancora in esecuzione), log in ${B#$ROOT/}/boot.log"
  tail -n 40 "$B/boot.log" | sed 's/^/    | /'
  # L'esito del boot e' una *misura*, non un errore dello script: lo stage riporta dove si e'
  # fermato il guest e termina 0 (il report e' il prodotto). 124 = ancora in esecuzione al timeout.
  if grep -q "SIGSEGV\|SIGBUS\|API NON IMPLEMENTATA\|non implementato" "$B/boot.log"; then
    say "    BOOT: STOP dentro il guest (vedi sopra; diagnosi in work/runtime-arm/REPORT.md §14.5)"
  elif (( rc == 124 )); then
    say "    BOOT: ancora in esecuzione dopo ${SECS}s (nessun crash): probabile ciclo del gioco"
  else
    say "    BOOT: uscita del guest con codice $rc"
  fi
  return 0
}

# ---------------------------------------------------------------------------------------------
stage_check
(( CHECK_ONLY )) && { say "--check: nessuna scrittura"; exit 0; }
(( STAGE >= 1 )) && stage1_sysroot
(( STAGE >= 2 )) && stage2_sdl2_headers
(( STAGE >= 3 )) && stage3_probe
(( STAGE >= 4 )) && stage4_runtime
(( STAGE >= 5 )) && stage5_guest
(( STAGE >= 6 )) && stage6_smoke
(( STAGE >= 7 )) && stage7_shim_port
(( STAGE >= 8 )) && stage8_game
(( STAGE >= 9 )) && stage9_boot
say "completato (stage $STAGE). Artefatti in ${WORK#$ROOT/}, sysroot in ${SYSROOT#$ROOT/}"

