#!/usr/bin/env bash
# Link the translated StarCraft + Storm i686 entries with the i386 runtime and shim objects.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
ENTRY="$B/lift-full/entry-x86"
RT="$B/obj-x86/runtime"
SHIMS="$B/obj-x86/shim-redef"
OUT="${1:-$B/starcraft-x86}"
LLD="${LLD:-$(command -v ld.lld || true)}"
if [[ -z "$LLD" || ! -x "$LLD" ]]; then
  for p in "$HOME"/.rustup/toolchains/*/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld; do
    [[ -x "$p" ]] && { LLD="$p"; break; }
  done
fi
[[ -n "$LLD" && -x "$LLD" ]] || { echo 'ld.lld non trovato; imposta LLD=...' >&2; exit 2; }
for f in "$ENTRY/StarCraft.entry.o" "$ENTRY/storm.entry.o" "$RT/guest_data.ld" "$RT/guest_pe_bytes.o"; do
  [[ -s "$f" ]] || { echo "manca $f (eseguire tools/build-runtime-x86.sh)" >&2; exit 2; }
done
shopt -s nullglob
runtime_objs=("$RT"/*.o)
shim_objs=("$SHIMS"/*.o)
((${#runtime_objs[@]})) || { echo "oggetti runtime assenti: $RT" >&2; exit 2; }
((${#shim_objs[@]})) || { echo "oggetti shim assenti: $SHIMS" >&2; exit 2; }
mkdir -p "$(dirname "$OUT")"
BOUNDS="$RT/runtime_bounds.ld"
LAYOUT="$RT/guest_data_link.ld"
printf 'reasm_text_lo = ADDR(.text);\nreasm_text_hi = ADDR(.text) + SIZEOF(.text);\nreasm_exe_code_lo = ADDR(.text);\nreasm_exe_code_hi = ADDR(.text) + SIZEOF(.text);\nreasm_storm_code_lo = ADDR(.text);\nreasm_storm_code_hi = ADDR(.text) + SIZEOF(.text);\n' > "$BOUNDS"
# Keep the output/LOAD ordering monotonic in VMA for LLD's i386 backend. The generated
# sections are sorted by guest VA; placing them before the port .text avoids a negative file offset.
sed 's/INSERT AFTER \.text/INSERT BEFORE .text/' "$RT/guest_data.ld" > "$LAYOUT"
echo "link-x86: $LLD -m elf_i386; runtime=${#runtime_objs[@]} shim=${#shim_objs[@]}"
# Remove the old inode first: an already-running user process may still map it (ETXTBSY).
rm -f "$OUT"
clang --target=i686-unknown-linux-gnu -m32 -fno-pie -no-pie --ld-path="$LLD" \
  -Wl,-m,elf_i386 -Wl,--section-start=.text=0x18000000 -Wl,-T,"$LAYOUT" -Wl,-T,"$BOUNDS" \
  -Wl,-z,muldefs -o "$OUT" \
  "$ENTRY/StarCraft.entry.o" "$ENTRY/storm.entry.o" \
  "${runtime_objs[@]}" "${shim_objs[@]}" \
  -lSDL2 -ldl -lpthread -lm -lgcc_s -lc
if [[ "${SC_SPLIT_DEBUG:-0}" == 1 ]]; then
  OBJCOPY="${OBJCOPY:-$(command -v llvm-objcopy || command -v objcopy || true)}"
  [[ -n "$OBJCOPY" ]] || { echo 'llvm-objcopy/objcopy non trovato per SC_SPLIT_DEBUG=1' >&2; exit 2; }
  DEBUG="$OUT.debug"
  TMP="$OUT.split-debug.tmp"
  "$OBJCOPY" --only-keep-debug "$OUT" "$DEBUG"
  "$OBJCOPY" --strip-debug "$OUT" "$TMP"
  "$OBJCOPY" --add-gnu-debuglink="$DEBUG" "$TMP"
  mv "$TMP" "$OUT"
  printf 'debug separato: %s\n' "$DEBUG"
fi
printf 'creato: %s\n' "$OUT"
