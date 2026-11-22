#!/usr/bin/env bash
# Full-image lifting driver: StarCraft.exe / storm.dll.orig
#   PE32 x86 -> SRW (OUT_LLASM) -> .llasm -> llasm -> LLVM IR -> opt -> llc -> ARMv6 .o
#
# Usage:
#   tools/lift-full.sh build              build the private SRW (OUT_LLASM) + llasm
#   tools/lift-full.sh srw   exe|storm|both
#   tools/lift-full.sh ir    exe|storm|both
#   tools/lift-full.sh obj   exe|storm|both
#   tools/lift-full.sh all   exe|storm|both
#
# Everything is written under build/lift-full/. The vendor tree is copied to a
# private build snapshot before compiling, so the shared sources are never
# modified by the build. SRW reads its .sci files and relocations.csv from its
# current directory, so it is run from build/lift-full.
#
# The .o produced here keeps every translated procedure: the IR is annotated
# with @llvm.compiler.used (see retain_procedures) because all procedures are
# private and only the future runtime dispatcher will reference them by guest
# address; without the annotation LLVM's GlobalDCE deletes the whole image.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/lift-full"
SRW_SRC="$BUILD/srw-src"
BIN="$BUILD/bin"
PIPE="$BUILD/pipeline"
D_COMPILER=${D_COMPILER:-ldc2}
DISPLAY_DEBUG=${DISPLAY_DEBUG:-0}
TRIPLE=armv6-unknown-linux-gnueabihf

image_path() {
  case "$1" in
    exe)   echo "$BUILD/StarCraft.orig.exe" ;;
    storm) echo "$BUILD/storm.orig.dll" ;;
    *) echo "immagine sconosciuta: $1" >&2; exit 2 ;;
  esac
}

image_base() {
  case "$1" in
    exe)   echo StarCraft ;;
    storm) echo storm ;;
  esac
}

build_tools() {
  mkdir -p "$BIN" "$SRW_SRC" "$PIPE"
  rsync -a --delete --exclude '*.o' --exclude 'SRW.exe' --exclude '.sconsign.dblite' \
        "$ROOT/vendor/sr/SRW/" "$SRW_SRC/"
  if [[ "$DISPLAY_DEBUG" == 1 ]]; then
    sed -i 's/^#undef DISPLAY_DISASSEMBLY$/#define DISPLAY_DISASSEMBLY/' "$SRW_SRC/SR_full_llasm_instr.c"
  fi
  grep -q '^#define OUTPUT_TYPE  OUT_LLASM' "$SRW_SRC/SR_defs.h" \
    || { echo 'SR_defs.h non e in modalita OUT_LLASM' >&2; exit 2; }
  ( cd "$SRW_SRC" && scons -Q >/dev/null )
  install -m755 "$SRW_SRC/SRW.exe" "$BIN/SRW-llasm"
  "$D_COMPILER" -O2 -of="$BIN/llasm" "$ROOT/vendor/sr/llasm/llasm.d"
  echo "tool pronti: $BIN/SRW-llasm $BIN/llasm"
}

# SRW writes <base>.llasm plus segNN_{code,data}.llinc into its current
# directory, so each image needs its own directory (the include names collide).
run_dir() {
  local which=$1 dir="$BUILD/run-$1"
  mkdir -p "$dir"
  cp -f "$BUILD"/*.sci "$BUILD/relocations.csv" "$BUILD/extern.llinc" "$BUILD/macros.llinc" "$dir/"
  echo "$dir"
}

run_srw() {
  local which=$1 img base dir; img=$(image_path "$which"); base=$(image_base "$which")
  [[ -x "$BIN/SRW-llasm" ]] || build_tools
  dir=$(run_dir "$which")
  ( cd "$dir" && "$BIN/SRW-llasm" "$img" "$base.llasm" ) > "$BUILD/srw-$which.log" 2>&1 && rc=0 || rc=$?
  echo "SRW $which: exit=$rc  LLASM=$base.llasm ($(wc -l < "$dir/$base.llasm" 2>/dev/null || echo 0) righe)"
  return 0
}

run_ir() {
  local which=$1 base dir; base=$(image_base "$which"); dir=$(run_dir "$which")
  [[ -s "$dir/$base.llasm" ]] || { echo "manca $dir/$base.llasm" >&2; exit 2; }
  ( cd "$dir" && "$BIN/llasm" -O2 \
      -I "$ROOT/vendor/sr/SR/llasm-support" -I "$dir" \
      -o "$base.ll" "$base.llasm" ) 2>&1 | tee "$BUILD/llasm-$which.log"
  cp -f "$dir/$base.ll" "$BUILD/$base.ll"
  echo "IR $which: $base.ll ($(wc -l < "$BUILD/$base.ll") righe)"
}

# All procedures are private; keep them so the object holds the whole image.
retain_procedures() {
  local base=$1
  python3 - "$BUILD/$base.ll" "$BUILD/$base.retained.ll" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
names = re.findall(r'^define [^@]*@([A-Za-z0-9_.$]+)\(', src, re.M)
items = ", ".join("i8* bitcast (void(%_cpu*)* @" + n + " to i8*)" for n in names)
open(sys.argv[2], 'w').write(
    src + "\n@llvm.compiler.used = appending global [" + str(len(names)) +
    " x i8*] [" + items + "], section \"llvm.metadata\"\n")
print("proc dichiarate:", len(names))
PY
}

run_obj() {
  local which=$1 base; base=$(image_base "$which")
  local llasm="$BUILD/$base.llasm" ir="$BUILD/$base.ll" ret="$BUILD/$base.retained.ll"
  [[ -s "$ir" ]] || { echo "manca $ir" >&2; exit 2; }
  mkdir -p "$PIPE"
  clang --target="$TRIPLE" -march=armv6zk -mfpu=vfp -mfloat-abi=hard \
        -S -emit-llvm -x c /dev/null -o "$PIPE/layout.ll"
  local dl
  dl=$(awk -F'"' '/^target datalayout/ {print $2; exit}' "$PIPE/layout.ll")
  [[ -n "$dl" ]] || { echo 'clang non ha emesso il datalayout' >&2; exit 2; }

  retain_procedures "$base"

  opt -S -passes=verify -data-layout="$dl" -o "$PIPE/$base.verified.ll" "$ret"
  opt -S -passes='default<O2>' -data-layout="$dl" -o "$PIPE/$base.optimized.ll" "$PIPE/$base.verified.ll"
  llc -mtriple="$TRIPLE" -mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard \
      -filetype=obj -o "$PIPE/$base.o" "$PIPE/$base.optimized.ll"
  cp "$PIPE/$base.o" "$BUILD/$base.o"

  echo "obj $which: $base.o $(stat -c%s "$BUILD/$base.o") byte, $(grep -c '^define' "$PIPE/$base.optimized.ll") funzioni"
  LC_ALL=C readelf -h "$BUILD/$base.o" | grep -E 'Class:|Machine:|Type:'
  LC_ALL=C readelf -A "$BUILD/$base.o" | grep -E 'Tag_CPU_arch|Tag_FP_arch|Tag_CPU_name'
}

imgs() {
  local sel=${1:?exe|storm|both}
  if [[ "$sel" == both ]]; then echo "exe storm"; else echo "$sel"; fi
}

case "${1:-}" in
  build) build_tools ;;
  srw)   for w in $(imgs "${2:-}"); do run_srw "$w"; done ;;
  ir)    for w in $(imgs "${2:-}"); do run_ir "$w"; done ;;
  obj)   for w in $(imgs "${2:-}"); do run_obj "$w"; done ;;
  all)
    build_tools
    for w in $(imgs "${2:-}"); do
      run_srw "$w"; run_ir "$w"; run_obj "$w"
    done ;;
  *) sed -n '2,20p' "$0"; exit 2 ;;
esac
