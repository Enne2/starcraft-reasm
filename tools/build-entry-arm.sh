#!/usr/bin/env bash
# Build the ARMv7 translated-entry objects and their PE data layout.
# Kept separate from build-entry-obj.sh: that legacy ARM path hardcodes a guest .L_text
# section, which LLVM does not emit for the current entry objects (the x86 layout generator
# already handles an absent/empty _text section).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L="$ROOT/build/lift-full"
P="${ENTRY_DIR:-$L/entry-armv7}"
TRIPLE=armv7a-unknown-linux-gnueabihf
CPU_FLAGS='-march=armv7-a -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm'
LLC_FLAGS='-mcpu=cortex-a53 -mattr=+vfp3,+neon,+hwdiv-arm -float-abi=hard'
SYSROOT="${SYSROOT:-$ROOT/build/sysroot}"
LLD="${LLD:-$HOME/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld}"
CLANG="${CLANG:-clang}"
SC_REASM="${SC_REASM:-$HOME/dev/sc-reasm}"
read -r -a CPU <<< "$CPU_FLAGS"
read -r -a LLC <<< "$LLC_FLAGS"
CC=("$CLANG" --target="$TRIPLE" "${CPU[@]}" --sysroot="$SYSROOT" --ld-path="$LLD")
for f in "$L/StarCraft.ll" "$L/storm.ll" "$L/StarCraft.orig.exe" "$L/storm.orig.dll"; do
  [[ -s "$f" ]] || { echo "missing input: $f" >&2; exit 1; }
done
for t in python3 opt llc llvm-nm llvm-readelf; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done
[[ -d "$SYSROOT/usr/include" ]] || { echo "sysroot missing: $SYSROOT" >&2; exit 1; }
mkdir -p "$P" "$ROOT/build/runtime-arm/game"

"$CLANG" --target="$TRIPLE" "${CPU[@]}" --sysroot="$SYSROOT" -S -emit-llvm -x c /dev/null -o "$P/layout.ll"
DL=$(awk -F'"' '/^target datalayout/ {print $2; exit}' "$P/layout.ll")
[[ -n "$DL" ]] || { echo 'clang emitted no ARM datalayout' >&2; exit 1; }
printf '[entry-arm] datalayout: %s\n' "$DL"

python3 "$ROOT/tools/dispatch-ir.py" \
  --exe "$L/StarCraft.ll" --storm "$L/storm.ll" \
  --out-exe "$P/StarCraft.entry.ll" --out-storm "$P/storm.entry.ll" \
  --entry-wrap loc_404C21 \
  --map-extra 0x404C21=sc_loc_404C21_impl \
  --map-extra 0x4F8329=__sse2_available_init \
  --map-extra 0x15040E07=__sse2_available_init_storm \
  --map-extra 0x15036260=storm_313_arm \
  --map-extra 0x15030AB0=storm_334_arm

build_one() {
  local module="$1" src verified optimized obj
  src="$P/$module.entry.ll"
  verified="$P/$module.verified.ll"
  optimized="$P/$module.optimized.ll"
  obj="$P/$module.entry.o"
  opt -S -passes=verify -data-layout="$DL" -o "$verified" "$src"
  opt -S -passes='default<O2>' -data-layout="$DL" -o "$optimized" "$verified"
  llc -mtriple="$TRIPLE" "${LLC[@]}" --data-sections -filetype=obj -o "$obj" "$optimized"
  LC_ALL=C readelf -h "$obj" | grep -E 'Class:.*ELF32|Machine:.*ARM|Type:.*REL' >/dev/null
  local symbols
  symbols=$(llvm-nm "$obj")
  if [[ "$module" == StarCraft ]]; then
    grep -Eq ' [A-Za-z] reasm_va_va$' <<<"$symbols"
  else
    grep -Eq ' [A-Za-z] ordinal_73746f726d2e646c6c_313$' <<<"$symbols"
  fi
  printf '[entry-arm] %s: %s bytes\n' "$module" "$(stat -c%s "$obj")"
}
build_one StarCraft
build_one storm

# Generate the same layout + PE-gap inputs used by the x86 runtime path. gen_game_layout.py
# intentionally omits _text when LLVM has no corresponding data section; do not synthesize it.
python3 "$ROOT/runtime/common/gen_game_layout.py" \
  --exe-obj "$P/StarCraft.entry.o" --exe-ir "$P/StarCraft.entry.ll" \
  --storm-obj "$P/storm.entry.o" --storm-ir "$P/storm.entry.ll" \
  --out-ld "$P/guest_data.ld" \
  --out-header "$ROOT/build/runtime-arm/game/guest_layout_arm.h" \
  --pe-exe "$L/StarCraft.orig.exe" --pe-storm "$L/storm.orig.dll" \
  --out-pe "$ROOT/build/runtime-arm/game/pe_headers_arm.c"
python3 "$ROOT/runtime/common/gen_pe_image_bytes.py" --arch arm \
  --pe exe="$L/StarCraft.orig.exe" --pe storm="$L/storm.orig.dll" \
  --ld-in "$P/guest_data.ld" --ld-obj "$P/guest_pe_bytes.o" \
  --out-ld "$P/guest_data.ld" --out-asm "$P/guest_pe_bytes.S" \
  --blob-dir "$P/pe-bytes"
# Linker-defined bounds of the port .text: referenced by indirect_dispatch.S to decide whether an
# indirect target already points into translated host code. gen_game_layout.py does not emit them.
python3 - "$P/guest_data.ld" <<'PYLD'
import sys
from pathlib import Path
p = Path(sys.argv[1]); s = p.read_text()
anchor = "SECTIONS {\n"
if anchor not in s:
    raise SystemExit("SECTIONS anchor missing in " + str(p))
s = s.replace(anchor, anchor +
              "  reasm_text_lo = ADDR(.text);\n"
              "  reasm_text_hi = ADDR(.text) + SIZEOF(.text);\n", 1)
p.write_text(s)
PYLD
"${CC[@]}" -c -o "$P/guest_pe_bytes.o" "$P/guest_pe_bytes.S"

# Dispatch support is part of the ARM entry link, not the runtime archive.
"${CC[@]}" -I "$ROOT/runtime/common" -c -o "$P/indirect_dispatch.o" \
  "$ROOT/runtime/common/indirect_dispatch.c"
"${CC[@]}" -I "$ROOT/runtime/common" -c -o "$P/indirect_dispatch_asm.o" \
  "$ROOT/runtime/arch/arm/indirect_dispatch.S"
"${CC[@]}" -c -o "$P/tls_arm.o" "$ROOT/runtime/arch/arm/tls.S"

python3 "$ROOT/runtime/common/gen_storm_ordinals.py" \
  --json "$SC_REASM/analysis/storm_ordinals.json" \
  --out "$ROOT/build/runtime-arm/game/storm_ordinals_arm.c"

printf '[entry-arm] output: %s\n' "$P"
