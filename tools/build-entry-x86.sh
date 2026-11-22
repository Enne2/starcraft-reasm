#!/usr/bin/env bash
# Build ELF32 i386 entry objects from lifted StarCraft/Storm LLVM IR (no runtime/link stage).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L="$ROOT/build/lift-full"
P="$L/entry-x86"
TRIPLE=i686-unknown-linux-gnu
for f in "$L/StarCraft.ll" "$L/storm.ll"; do [[ -s "$f" ]] || { echo "missing input: $f" >&2; exit 1; }; done
for t in clang python3 opt llc llvm-nm readelf llvm-objdump; do command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }; done
mkdir -p "$P"
clang --target="$TRIPLE" -m32 -S -emit-llvm -x c /dev/null -o "$P/layout-i686.ll"
DL=$(awk -F'"' '/^target datalayout/ {print $2; exit}' "$P/layout-i686.ll")
[[ -n "$DL" ]] || { echo "clang emitted no datalayout" >&2; exit 1; }
printf '[entry-x86] datalayout: %s\n' "$DL"
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
  local start end
  start=$(date +%s)
  opt -S -passes=verify -data-layout="$DL" -o "$verified" "$src"
  opt -S -passes='default<O1>' -data-layout="$DL" -o "$optimized" "$verified"
  # Preserve the explicit guest section attributes from the lifted LLVM globals;
  # without this, i686 coalesces host tables and guest blobs into .data/.rodata.
  llc -mtriple="$TRIPLE" -data-sections -relocation-model=static -filetype=obj -o "$obj" "$optimized"
  end=$(date +%s)
  printf '[entry-x86] %s: %ss, %s bytes\n' "$module" "$((end-start))" "$(stat -c%s "$obj")"
}
build_one StarCraft
build_one storm
for obj in "$P/StarCraft.entry.o" "$P/storm.entry.o"; do
  echo "--- $(basename "$obj") ---"
  header=$(LC_ALL=C readelf -h "$obj")
  printf '%s\n' "$header" | grep -E 'Class:.*ELF32|Type:.*REL|Machine:.*Intel 80386' || {
    echo "unexpected ELF header: $obj" >&2; exit 1;
  }
  symbols=$(llvm-nm "$obj")
  if [[ "$obj" == "$P/StarCraft.entry.o" ]]; then
    for sym in reasm_va_map_n reasm_va_va reasm_va_fn loc_404C21 sc_loc_404C21_impl; do
      grep -Eq " [A-Za-z] $sym$" <<<"$symbols" || { echo "missing $sym in $obj" >&2; exit 1; }
    done
  else
    grep -Eq ' [A-Za-z] ordinal_73746f726d2e646c6c_313$' <<<"$symbols" || {
      echo "missing storm ordinal symbol in $obj" >&2; exit 1;
    }
  fi
done
llvm-objdump -d --disassemble-symbols=loc_404C21,sc_loc_404C21_impl "$P/StarCraft.entry.o"
printf '[entry-x86] outputs: %s\n' "$P"
