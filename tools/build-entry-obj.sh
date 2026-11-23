#!/usr/bin/env bash
# tools/build-entry-obj.sh — oggetti ARMv6 del candidato "entry" (exec + storm) con la
# risoluzione dei salti indiretti.
#
# Riproduce, in uno script, la catena che il 20/11 era stata eseguita a mano
# (vedi CHECKPOINT.md §4-5) e vi aggiunge il passo di tools/dispatch-ir.py:
#
#   build/lift-full/{StarCraft,storm}.ll        (IR di llasm, invariata)
#     -> dispatch-ir.py                        tabella VA->entry + riscrittura dei `tcall reg`
#     -> opt verify / opt default<O2>          stesso ordine della pipeline già usata
#     -> llc arm1176 vfp2 -neon                oggetti finali
#
# Gli artefatti precedenti NON vengono sovrascritti: tutto finisce in
# build/lift-full/entry/ (fallback: build/lift-full/{StarCraft.entry.o,storm.o} restano intatti).
#
# USO E MODALITA' (work/link/REPORT-flash-speed.md)
#   tools/build-entry-obj.sh                  catena completa = candidato (default invariato)
#   tools/build-entry-obj.sh --ir             solo IR/codegen (dispatch-ir, opt, llc, layout)
#   tools/build-entry-obj.sh --runtime-only   solo runtime C/S + bridge + link (+ smoke): <2 s
#   tools/build-entry-obj.sh --fast           come il default ma opt<O1>/llc -O1 (misurato)
#   tools/build-entry-obj.sh --check          verifica soltanto input e strumenti
#   opzioni: --no-cache (ignora la cache), --no-smoke (non lanciare lo smoke),
#            --link-out <path> (default build/starcraft-armv6-local)
#
# CACHE CONTENT-ADDRESSED: ogni fase ha una chiave sha256 del comando + degli input (o della
# chiave della fase precedente, per non rileggere i .ll da 75 MB). Se l'artefatto con quella
# chiave esiste in build/lift-full/entry/.cache/, la fase viene saltata e il file copiato.
# Le fasi dipendenti solo dai PE/.def (guest_data.ld, guest_pe_bytes.o, bridge) usano la stessa
# cache, quindi non si rigenerano se gli input non cambiano.
#
# TEMPI: ogni fase aggiunge una riga a build/qa/timing.jsonl (ISO 8601, durata in ms, cache hit).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L="$ROOT/build/lift-full"
P="$L/entry"
ARM_ARCH="${ARM_ARCH:-armv6}"
TRIPLE="${TRIPLE:-armv6-unknown-linux-gnueabihf}"
CPU_FLAGS="${CPU_FLAGS:--march=armv6zk -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm}"
LLC_FLAGS="${LLC_FLAGS:--mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard}"
SYSROOT="${SYSROOT:-$ROOT/build/sysroot}"
read -r -a CPU_FLAGS_ARR <<< "$CPU_FLAGS"
read -r -a LLC_FLAGS_ARR <<< "$LLC_FLAGS"
# -I build/runtime-arm/game: ospita guest_layout_arm.h / bridge_table.c generati dagli stage
CC_ARM=(clang --target="$TRIPLE" "${CPU_FLAGS_ARR[@]}"
        --sysroot="$SYSROOT" -O1 -g -Wall -Wno-unused-parameter
        -I "$ROOT/build/runtime-arm/game")
MCALL=("${LLC_FLAGS_ARR[@]}")

CHECK_ONLY=0
MODE=full            # full | ir | runtime
FAST=0
NO_CACHE=0
DO_SMOKE=1
SMOKE_TIMEOUT=25
LINK_OUT="$ROOT/build/starcraft-${ARM_ARCH}-local"
while (( $# )); do
  case "$1" in
    --check)        CHECK_ONLY=1 ;;
    --ir)           MODE=ir ;;
    --runtime-only) MODE=runtime ;;
    --fast)         FAST=1 ;;
    --no-cache)     NO_CACHE=1 ;;
    --no-smoke)     DO_SMOKE=0 ;;
    --smoke-timeout) SMOKE_TIMEOUT="${2:?--smoke-timeout vuole i secondi}"; shift ;;
    --link-out)     LINK_OUT="${2:?--link-out vuole un percorso}"; shift ;;
    -h|--help)      sed -n '13,30p' "$0"; exit 0 ;;
    *) echo "build-entry-obj: argomento sconosciuto: $1" >&2; exit 2 ;;
  esac
  shift
done
# LINK_OUT sempre assoluto: lo smoke fa `cd build/asset-sandbox` e un percorso relativo non
# esisterebbe piu' da li' (misurato: qemu usciva 1 in 3 ms, senza stampare nulla).
[[ "$LINK_OUT" == /* ]] || LINK_OUT="$ROOT/$LINK_OUT"
(( FAST )) && MODE_TAG="$MODE-fast" || MODE_TAG="$MODE"
(( FAST )) && OPT_PASSES='default<O1>' || OPT_PASSES='default<O2>'
LLC_OPT=(); (( FAST )) && LLC_OPT=(-O1)

die() { echo "build-entry-obj: $*" >&2; exit 1; }
say() { echo "[entry-obj] $*"; }

CACHE="$P/.cache"
TIMING="$ROOT/build/qa/timing.jsonl"
now_ms() { date +%s%3N; }
# Le fasi di exe e storm girano in sottoprocessi: il loro esito non torna per variabile, quindi
# ogni fase scrive una riga "hit:<fase>" / "miss:<fase>" in un file condiviso.
PHASES_LOG="$CACHE/.phases.log"

# --- tempi -----------------------------------------------------------------------------------
logtiming() { # fase durata_ms cache_hit
  mkdir -p "$ROOT/build/qa"
  printf '{"ts":"%s","mode":"%s","phase":"%s","ms":%s,"cache":%s}\n' \
      "$(date -Is)" "$MODE_TAG" "$1" "$2" "$3" >> "$TIMING"
}

# --- chiavi content-addressed ----------------------------------------------------------------
# key_of "<descrizione/command line>" <file...> -> 16 hex. I file mancanti entrano come tali
# (una chiave diversa: meglio un miss che un hit sbagliato).
key_of() {
  local desc="$1"; shift
  { printf '%s\n' "$desc"
    for f in "$@"; do
      if [[ -e "$f" ]]; then sha256sum "$f"; else printf 'MISSING %s\n' "$f"; fi
    done
  } | sha256sum | cut -c1-16
}

# cache_put <chiave> <file...> / cache_get <chiave> <file...>: più artefatti sotto la stessa chiave
# (una fase sola produce .ld + .S + .o, oppure i tre file del bridge).
cache_put() { local key="$1"; shift; mkdir -p "$CACHE"; local f
  for f in "$@"; do cp -f "$f" "$CACHE/$(basename "$f").$key"; done; }
cache_get() { local key="$1"; shift; local f
  for f in "$@"; do [[ -s "$CACHE/$(basename "$f").$key" ]] || return 1; done
  for f in "$@"; do cp -f "$CACHE/$(basename "$f").$key" "$f"; done; }

# phase <nome> <file-di-uscita> <chiave> <comando...>
# Se l'artefatto con quella chiave è in cache lo copia (e lo dichiara), altrimenti esegue il
# comando e mette il risultato in cache. Ogni esito finisce in timing.jsonl.
phase() {
  local name="$1" out="$2" key="$3"; shift 3
  local store="$CACHE/$(basename "$out").$key" t0 ms
  t0=$(now_ms)
  if (( ! NO_CACHE )) && [[ -s "$store" ]]; then
    cp -f "$store" "$out"
    ms=$(( $(now_ms) - t0 ))
    say "[cache hit] $name"
    logtiming "$name" "$ms" true
    echo "hit:$name" >> "$PHASES_LOG"
    return 0
  fi
  "$@"
  mkdir -p "$CACHE"
  [[ -s "$out" ]] && cp -f "$out" "$store"
  ms=$(( $(now_ms) - t0 ))
  say "$name: ${ms} ms"
  logtiming "$name" "$ms" false
  echo "miss:$name" >> "$PHASES_LOG"
}

for f in "$L/StarCraft.ll" "$L/storm.ll" "$L/StarCraft.orig.exe" "$L/storm.orig.dll"; do
  [[ -s "$f" ]] || die "manca $f (tools/lift-full.sh ir exe|storm)"
done
for t in opt llc clang python3 llvm-nm; do
  command -v "$t" >/dev/null || die "strumento mancante: $t"
done
[[ -d "$SYSROOT/usr/include" ]] || die "sysroot assente: $SYSROOT (stage 1)"
if (( CHECK_ONLY )); then
  say "input e strumenti presenti: nessuna scrittura"
  exit 0
fi

mkdir -p "$P" "$CACHE"
: > "$PHASES_LOG"
say "modalità=$MODE_TAG  cache=$([[ $NO_CACHE == 1 ]] && echo off || echo "$CACHE")"

T0=$(now_ms)

# =============================================================================================
# IR + codegen (le fasi costose: 97% del tempo). Saltate del tutto in --runtime-only.
# =============================================================================================
if [[ "$MODE" != runtime ]]; then
  # datalayout del target: l'IR di llasm non ha l'header `target datalayout`, e opt/llc devono
  # conoscerlo prima di qualunque trasformazione (stessa scelta della pipeline esistente).
  clang --target="$TRIPLE" "${CPU_FLAGS_ARR[@]}" \
        -S -emit-llvm -x c /dev/null -o "$P/layout.ll"
  DL=$(awk -F'"' '/^target datalayout/ {print $2; exit}' "$P/layout.ll")
  [[ -n "$DL" ]] || die "clang non ha emesso il datalayout"
  say "datalayout: $DL"

  # 1) VA->entry + call indiretti (una sola passata per le due immagini).
  #    L'entry del CRT riceve un wrapper C-ABI col nome originale (il runtime C la chiama così)
  #    e la sua tabella punta all'implementazione fastcc.
  DISP_ARGS=(--exe "$L/StarCraft.ll" --storm "$L/storm.ll"
             --out-exe "$P/StarCraft.entry.ll" --out-storm "$P/storm.entry.ll"
             --entry-wrap loc_404C21 --map-extra 0x404C21=sc_loc_404C21_impl
             --map-extra 0x4F8329=__sse2_available_init
             --map-extra 0x15040E07=__sse2_available_init_storm
             --map-extra 0x15036260=storm_313_arm
             --map-extra 0x15030AB0=storm_334_arm)
  KEY_DISP=$(key_of "dispatch-ir v3 ${DISP_ARGS[*]}" "$ROOT/tools/dispatch-ir.py" "$L/StarCraft.ll" "$L/storm.ll")
  # due uscite dalla stessa fase: la seconda viene messa in cache come allegato
  STORE_STORM="$CACHE/storm.entry.ll.$KEY_DISP"
  t0=$(now_ms)
  if (( ! NO_CACHE )) && [[ -s "$CACHE/StarCraft.entry.ll.$KEY_DISP" && -s "$STORE_STORM" ]]; then
    cp -f "$CACHE/StarCraft.entry.ll.$KEY_DISP" "$P/StarCraft.entry.ll"
    cp -f "$STORE_STORM" "$P/storm.entry.ll"
    say "[cache hit] dispatch-ir (2 immagini)"
    logtiming dispatch-ir "$(( $(now_ms) - t0 ))" true
    echo "hit:dispatch-ir" >> "$PHASES_LOG"
  else
    python3 "$ROOT/tools/dispatch-ir.py" "${DISP_ARGS[@]}" | sed 's/^/    /'
    mkdir -p "$CACHE"
    cp -f "$P/StarCraft.entry.ll" "$CACHE/StarCraft.entry.ll.$KEY_DISP"
    cp -f "$P/storm.entry.ll" "$STORE_STORM"
    say "dispatch-ir: $(( $(now_ms) - t0 )) ms"
    logtiming dispatch-ir "$(( $(now_ms) - t0 ))" false
    echo "miss:dispatch-ir" >> "$PHASES_LOG"
  fi

  # 2) verify -> O2 -> llc, per immagine. Le due catene sono indipendenti: girano in parallelo
  #    (prima erano in sequenza: exe 140 s + storm 25 s).
  build_image() {
    local m="$1" kv ko kl
    kv=$(key_of "opt verify $DL" "$P/$m.entry.ll")
    phase "$m:opt-verify" "$P/$m.verified.ll" "$kv" \
        opt -S -passes=verify -data-layout="$DL" -o "$P/$m.verified.ll" "$P/$m.entry.ll"
    ko=$(key_of "opt $OPT_PASSES $DL" "$kv")
    phase "$m:opt-$OPT_PASSES" "$P/$m.optimized.ll" "$ko" \
        opt -S -passes="$OPT_PASSES" -data-layout="$DL" -o "$P/$m.optimized.ll" "$P/$m.verified.ll"
    kl=$(key_of "llc $TRIPLE ${MCALL[*]} ${LLC_OPT[*]} --data-sections" "$ko")
    phase "$m:llc" "$P/$m.entry.o" "$kl" \
        llc -mtriple="$TRIPLE" "${MCALL[@]}" ${LLC_OPT[@]+"${LLC_OPT[@]}"} --data-sections \
            -filetype=obj -o "$P/$m.entry.o" "$P/$m.optimized.ll"
  }
  build_image StarCraft > /tmp/.entry-obj-exe.$$ 2>&1 &
  PID_EXE=$!
  build_image storm > /tmp/.entry-obj-storm.$$ 2>&1 &
  PID_STORM=$!
  RC=0
  wait $PID_EXE || RC=1
  wait $PID_STORM || RC=1
  cat /tmp/.entry-obj-exe.$$ /tmp/.entry-obj-storm.$$ || true
  rm -f /tmp/.entry-obj-exe.$$ /tmp/.entry-obj-storm.$$
  (( RC == 0 )) || die "opt/llc fallito (vedi sopra)"
fi

# =============================================================================================
# Layout e byte dell'immagine PE: dipendono solo dai PE + dal template del linker script, quindi
# restano in cache finché non cambiano i PE (o gli oggetti, per i nomi di sezione).
# =============================================================================================
LAYOUT_TMPL="$(cat <<LDEOF
SECTIONS {
  reasm_text_lo = ADDR(.text);
  reasm_text_hi = ADDR(.text) + SIZEOF(.text);

  .guest_exe_tdata   0x00406C0C : { $P/StarCraft.entry.o(.rodata..L_text) }
  .guest_exe_rdata   0x004FE5D0 : { $P/StarCraft.entry.o(.rodata..L_rdata) }
  .guest_exe_data    0x0050C000 : { $P/StarCraft.entry.o(.data..L_data) }
  .guest_storm_tdata 0x150062D8 : { $P/storm.entry.o(.rodata..L_text) }
  .guest_storm_rdata 0x150453C4 : { $P/storm.entry.o(.rodata..L_rdata) }
  .guest_storm_data  0x15059000 : { $P/storm.entry.o(.data..L_data) }
} INSERT AFTER .bss;
LDEOF
)"
KEY_LAYOUT=$(key_of "layout v2 $LAYOUT_TMPL" "$L/StarCraft.orig.exe" "$L/storm.orig.dll" \
                          "$ROOT/runtime/common/gen_pe_image_bytes.py")

write_layout() {
  # 2b) layout dei dati guest ai VA originali + limiti del .text del port (guest_data.ld).
  #     Senza questo script i blob @_data/@_rdata/@_text finirebbero nel .data del port e ogni
  #     accesso assoluto del codice liftato (es. 0x0050C650) cadrebbe nella pagina sbagliata:
  #     vedi work/link/REPORT-flash-crt.md §1.1.
  cat > "$P/guest_data.ld" <<LDEOF
/* GENERATED by tools/build-entry-obj.sh — NON modificare a mano.
 *
 * Dati guest ai VA originali, codice tradotto nel layout del port (base 0x18000000).
 * Perche': il codice liftato indirizza i dati con *costanti assolute* (es. 0x0050C650) e con gli
 * alias @loc_<VA>; i dati devono quindi stare ai VA del PE. Il .text del port sta a 0x18000000,
 * fuori da ogni spazio di indirizzi del guest (immagini 0x00400000/0x15000000, stack 0x02000000,
 * arena 0x0E000000), cosi' i due insiemi di indirizzi sono disgiunti e tools/dispatch-ir.py
 * --dump-map non trova collisioni.
 *
 * Solo i segmenti *dati*: nessun .text di immagini (il codice e' quello del port).
 * reasm_text_lo/hi delimitano il codice del port per la dispatch dei salti indiretti.
 * Uso: ld.lld --section-start=.text=0x18000000 -T guest_data.ld ...
 */
$LAYOUT_TMPL
LDEOF

  # 2c) Byte dell'immagine PE che il lift NON colloca, ai VA originali.
  #     Due difetti veri da questo, non due ipotesi (work/link/REPORT-flash-game.md §0):
  #       * `.rsrc` (exe 0x006DE000, VirtualSize 0xDC28, RawSize 0xE000) non era mappata da nessuna
  #         PT_LOAD, e il walker delle risorse dello shim legge `p+12` della directory radice ->
  #         SIGSEGV a 0x006DE00C, che sta DENTRO la sezione (non oltre);
  #       * i buchi di .text/.rdata non liftati: il guest li poteva leggere solo dal .text del port
  #         (maschera) o andava in fault.
  #     I blob liftati NON sono copie letterali del PE (contengono indirizzi host), quindi
  #     l'immagine non si sovrascrive in blocco: si collocano solo i buchi, con i flag presi dalle
  #     caratteristiche della sezione PE. Lo script riordina per VA anche le righe esistenti
  #     (lld assegna gli offset di file nell'ordine dello script).
  python3 "$ROOT/runtime/common/gen_pe_image_bytes.py" \
    --pe "exe=$L/StarCraft.orig.exe" --pe "storm=$L/storm.orig.dll" \
    --ld-in "$P/guest_data.ld" --out-ld "$P/guest_data.ld" \
    --ld-obj "$P/guest_pe_bytes.o" \
    --out-asm "$P/guest_pe_bytes.S" --blob-dir "$P/pe-bytes" | sed 's/^/    /'
  "${CC_ARM[@]}" -c -o "$P/guest_pe_bytes.o" "$P/guest_pe_bytes.S"
}

if [[ "$MODE" != runtime ]]; then
  t0=$(now_ms)
  if (( ! NO_CACHE )) && cache_get "$KEY_LAYOUT" "$P/guest_data.ld" "$P/guest_pe_bytes.S" \
                                            "$P/guest_pe_bytes.o"; then
    say "[cache hit] layout+PE-bytes"
    logtiming "layout+PE-bytes" "$(( $(now_ms) - t0 ))" true
    echo "hit:layout+PE-bytes" >> "$PHASES_LOG"
  else
    write_layout
    cache_put "$KEY_LAYOUT" "$P/guest_data.ld" "$P/guest_pe_bytes.S" "$P/guest_pe_bytes.o"
    say "layout+PE-bytes: $(( $(now_ms) - t0 )) ms"
    logtiming "layout+PE-bytes" "$(( $(now_ms) - t0 ))" false
    echo "miss:layout+PE-bytes" >> "$PHASES_LOG"
  fi
fi

# =============================================================================================
# Runtime del port: si ricompilano dai sorgenti attuali (game_main.c chiama
# reasm_dispatch_init, indirect_dispatch.[cS] sono nuovi). Gli oggetti *generati* dagli script
# (bridge stubs, tabella degli import, ordinali storm, shim) restano quelli di
# tools/build-runtime-arm.sh stage 7: vengono copiati, non rigenerati qui.
# =============================================================================================
say "runtime del port -> $P/runtime"
mkdir -p "$P/runtime"
cp -f "$ROOT/build/runtime-arm/game/obj"/*.o "$P/runtime/"

# 2d) tabella e stub del bridge RIGENERATI dai sorgenti, non copiati da $OBJ.
#     Perche': $OBJ e' l'uscita dello stage 8 di tools/build-runtime-arm.sh, che compila
#     bridge_table.def/bridge_shims.def in quel momento. Se la .def cambia dopo (es. un simbolo
#     esterno che passa da UNK a implementazione), l'oggetto in $OBJ resta vecchio e il link
#     ricrea lo stop UNK anche se il corpo esiste (misurato: crt_unlifted_15040E07,
#     work/link/REPORT-flash-storm.md §2). Gli ingressi sono locali e deterministici:
#     runtime/common/bridge_table.def + build/runtime-arm/game/{bridge_shims.def,need.txt}.
GEN_DIR="$ROOT/build/runtime-arm/game"
KEY_BRIDGE=$(key_of "bridge v2" "$ROOT/runtime/common/bridge_table.def" "$GEN_DIR/bridge_shims.def" \
                     "$GEN_DIR/need.txt" "$ROOT/runtime/common/gen_bridge_stubs.py")
gen_bridge() {
  python3 "$ROOT/runtime/common/gen_bridge_stubs.py" \
    --def "$ROOT/runtime/common/bridge_table.def" --also "$GEN_DIR/bridge_shims.def" \
    --only-needed "$GEN_DIR/need.txt" \
    --out "$P/bridge_stubs.S" --csym-out "$P/bridge_symbols.c" --table-out "$P/bridge_table.c" \
    | sed 's/^/    /'
  "${CC_ARM[@]}" -I "$ROOT/runtime/common" -c -o "$P/runtime/bridge_stubs.o" "$P/bridge_stubs.S"
  "${CC_ARM[@]}" -I "$ROOT/runtime/common" -c -o "$P/runtime/bridge_symbols.o" "$P/bridge_symbols.c"
  "${CC_ARM[@]}" -I "$ROOT/runtime/common" -c -o "$P/runtime/bridge_table.o" "$P/bridge_table.c"
}
say "bridge: tabella e stub rigenerati dai sorgenti"
if (( ! NO_CACHE )) && cache_get "$KEY_BRIDGE" "$P/bridge_table.c" "$P/bridge_stubs.S" \
        "$P/bridge_symbols.c" "$P/runtime/bridge_stubs.o" "$P/runtime/bridge_symbols.o" \
        "$P/runtime/bridge_table.o"; then
  say "[cache hit] bridge"
  logtiming bridge 0 true
  echo "hit:bridge" >> "$PHASES_LOG"
else
  t0=$(now_ms)
  gen_bridge
  cache_put "$KEY_BRIDGE" "$P/bridge_table.c" "$P/bridge_stubs.S" "$P/bridge_symbols.c" \
            "$P/runtime/bridge_stubs.o" "$P/runtime/bridge_symbols.o" "$P/runtime/bridge_table.o"
  say "bridge: $(( $(now_ms) - t0 )) ms"
  logtiming bridge "$(( $(now_ms) - t0 ))" false
  echo "miss:bridge" >> "$PHASES_LOG"
fi

# runtime C/S del port: chiave = sorgenti + header (un header toccato ricompila tutto: è la
# scelta sicura), condivisa da tutti i TU. Ogni file ha il proprio artefatto in cache.
RT_FILES=(cpu_llasm.c game_memory.c guest_cpu_helpers.c bridge.c elf_support.c
          x87_crt.c crt_init.c guest_call.c win32_ext.c game_main.c
          com_bridge.c storm_sdraw.c)
RT_HDRS=("$ROOT"/runtime/common/*.h "$GEN_DIR"/guest_layout_arm.h)
# Chiave per singolo TU: il suo sorgente + TUTTI gli header del runtime. Un header toccato
# ricompila tutto (sicuro), un .c toccato ricompila solo il suo oggetto.
key_rt() { key_of "runtime v2 ${CC_ARM[*]} $1" "$ROOT/runtime/common/$1" "${RT_HDRS[@]}"; }
mkdir -p "$CACHE/runtime"
RT_HIT=0
RT_MISS=0
T0RT=$(now_ms)
for f in "${RT_FILES[@]}"; do
  o="$P/runtime/${f%.c}.o"; k=$(key_rt "$f")
  if (( ! NO_CACHE )) && [[ -s "$CACHE/runtime/$(basename "$o").$k" ]]; then
    cp -f "$CACHE/runtime/$(basename "$o").$k" "$o"; RT_HIT=$((RT_HIT+1))
  else
    "${CC_ARM[@]}" -I "$ROOT/runtime/common" -c -o "$o" "$ROOT/runtime/common/$f"
    cp -f "$o" "$CACHE/runtime/$(basename "$o").$k"; RT_MISS=$((RT_MISS+1))
  fi
done
# __aeabi_read_tp: helper TLS che la glibc del sysroot non esporta piu' (variabili __thread del
# port: cpu corrente per thread, jmp_buf della chiamata guest).
# coppie "sorgente in runtime/common -> oggetto": la dispatch vive accanto alle immagini (IMG_OBJS
# del link), tls_arm.o con il resto del runtime (RUNTIME_OBJ_DIR).
for pair in "arch/arm/tls.S runtime/tls_arm.o" "common/indirect_dispatch.c indirect_dispatch.o" \
            "arch/arm/indirect_dispatch.S indirect_dispatch_asm.o" "arch/arm/com_bridge.S runtime/com_bridge_asm.o"; do
  set -- $pair
  src="$ROOT/runtime/$1"; o="$P/$2"; k=$(key_of "runtime v2 ${CC_ARM[*]} $1" "$src" "${RT_HDRS[@]}")
  if (( ! NO_CACHE )) && [[ -s "$CACHE/$(basename "$o").$k" ]]; then
    cp -f "$CACHE/$(basename "$o").$k" "$o"; RT_HIT=$((RT_HIT+1))
  else
    "${CC_ARM[@]}" -I "$ROOT/runtime/common" -c -o "$o" "$src"
    cp -f "$o" "$CACHE/$(basename "$o").$k"; RT_MISS=$((RT_MISS+1))
  fi
done
say "runtime: $RT_HIT da cache, $RT_MISS ricompilati ($(( $(now_ms) - T0RT )) ms)"
logtiming runtime "$(( $(now_ms) - T0RT ))" "$([[ $RT_MISS == 0 ]] && echo true || echo false)"
(( RT_MISS == 0 )) && echo "hit:runtime" >> "$PHASES_LOG" || echo "miss:runtime" >> "$PHASES_LOG"

for f in "$P"/*.o; do
  printf '  %-28s %9d byte\n' "$(basename "$f")" "$(stat -c%s "$f")"
done

# =============================================================================================
# Link (0,5-0,8 s) e, in --runtime-only, smoke immediato: è il ciclo di iterazione.
# =============================================================================================
if [[ "$MODE" == runtime ]]; then
  t0=$(now_ms)
  ENTRY_DIR="$P" RUNTIME_OBJ_DIR="$P/runtime" "$ROOT/tools/link-arm.sh" "$LINK_OUT"
  say "link: $(( $(now_ms) - t0 )) ms -> $LINK_OUT"
  logtiming link "$(( $(now_ms) - t0 ))" false
  echo "miss:link" >> "$PHASES_LOG"
  if (( DO_SMOKE )); then
    [[ -d "$ROOT/build/asset-sandbox" ]] || die "manca build/asset-sandbox"
    t0=$(now_ms); rc=0
    ( cd "$ROOT/build/asset-sandbox" || exit 90
      ulimit -c 0 || true
      timeout "$SMOKE_TIMEOUT" qemu-arm-static -cpu "${QEMU_CPU:-arm1176}" -L "$SYSROOT" "$LINK_OUT" ) \
      > "$ROOT/build/qa/run-last.log" 2>&1 || rc=$?
    printf '\n[smoke] exit=%s\n' "$rc" >> "$ROOT/build/qa/run-last.log"
    say "smoke: $(( $(now_ms) - t0 )) ms, exit=$rc -> build/qa/run-last.log (ultime righe:)"
    tail -6 "$ROOT/build/qa/run-last.log" | sed 's/^/    /'
    logtiming smoke "$(( $(now_ms) - t0 ))" false
    echo "miss:smoke" >> "$PHASES_LOG"
  else
    echo "hit:smoke" >> "$PHASES_LOG"
  fi
fi

# riepilogo: quali fasi hanno lavorato e quali sono arrivate dalla cache (anche dai sottoprocessi)
MISS_LIST=$(sed -n 's/^miss://p' "$PHASES_LOG" 2>/dev/null | paste -sd' ' -)
HIT_LIST=$(sed -n 's/^hit://p' "$PHASES_LOG" 2>/dev/null | paste -sd' ' -)
say "fasi eseguite: ${MISS_LIST:-nessuna}"
say "fasi da cache: ${HIT_LIST:-nessuna}"
say "tempo totale ($MODE_TAG): $(( $(now_ms) - T0 )) ms"
logtiming "TOTALE:$MODE_TAG" "$(( $(now_ms) - T0 ))" "$([[ -z $MISS_LIST ]] && echo true || echo false)"
say "ok (artefatti in build/lift-full/entry/)"
say "link: ENTRY_DIR=$P RUNTIME_OBJ_DIR=$P/runtime ./tools/link-arm.sh build/starcraft-${ARM_ARCH}-dispatch"
