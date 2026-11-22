#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/build/starcraft-x86"
SANDBOX="$ROOT/build/asset-sandbox"
SMACK="$ROOT/build/i386-libs/libsmacker-1.2.0.so"
OUT="$ROOT/build/qa/gfx/gl-palette-test.log"
if [[ ! -x "$BIN" || ! -d "$SANDBOX" || ! -f "$SMACK" ]]; then
  echo 'x86_gl_palette_test: SKIP (build/starcraft-x86, asset-sandbox o libsmacker mancante)' >&2
  exit 77
fi
if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo 'x86_gl_palette_test: SKIP (WAYLAND_DISPLAY non impostato)' >&2
  exit 77
fi
mkdir -p "$(dirname "$OUT")"
set +e
(cd "$SANDBOX" && timeout -k 2s 6s env SDL_VIDEODRIVER=wayland SC_PRESENT=gl SC_GL_VERIFY=1 SC_SKIP_INTRO=1 SC_SMACKER_SO="$SMACK" "$BIN") >"$OUT" 2>&1
rc=$?
set -e
if ! grep -q 'SC_GL_VERIFY PASS: 307200 pixel esatti' "$OUT"; then
  echo "x86_gl_palette_test: FAIL (rc=$rc; nessun readback esatto; $OUT)" >&2
  tail -40 "$OUT" >&2
  exit 1
fi
echo 'x86_gl_palette_test: PASS (307200 pixel RGBA confrontati con canvas/palette)'
