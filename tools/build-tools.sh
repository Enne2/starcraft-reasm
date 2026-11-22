#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BUILD="$ROOT/build/pipeline"
mkdir -p "$BUILD/bin" "$BUILD/srw-src"
D_COMPILER=${D_COMPILER:-ldc2}
command -v "$D_COMPILER" >/dev/null || { echo "D compiler non trovato: $D_COMPILER" >&2; exit 2; }
# Private SRW snapshot; never patch shared vendor files.
cp -a "$ROOT/vendor/sr/SRW/." "$BUILD/srw-src/"
python3 - "$BUILD/srw-src/SR_defs.h" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); s=p.read_text(); old='#define OUTPUT_TYPE  OUT_X86'
if old not in s and '#define OUTPUT_TYPE  OUT_LLASM' not in s: raise SystemExit('OUTPUT_TYPE anchor missing')
p.write_text(s.replace(old, '#define OUTPUT_TYPE  OUT_LLASM'))
PY
"$D_COMPILER" -O2 -of="$BUILD/bin/llasm" "$ROOT/vendor/sr/llasm/llasm.d"
command -v scons >/dev/null || { echo 'scons non trovato' >&2; exit 2; }
(cd "$BUILD/srw-src" && scons -Q)
install -m755 "$BUILD/srw-src/SRW.exe" "$BUILD/bin/SRW-llasm"
echo "Toolchain pronta: $BUILD/bin/llasm e SRW-llasm"
