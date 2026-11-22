#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/tests/x86_runtime_check"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
printf 'test_bridge\n' > "$TMP/needed"
printf 'ENTRY(test_bridge, 0, arm_bridge_common_c)\n' > "$TMP/bridge.def"
python3 "$ROOT/runtime/common/gen_bridge_stubs.py" --arch x86 --def "$TMP/bridge.def" --only-needed "$TMP/needed" --out "$TMP/bridge.S"
CC=(clang --target=i686-unknown-linux-gnu -m32 -fno-pie)
"${CC[@]}" -c "$ROOT/runtime/arch/x86/indirect_dispatch_x86.S" -o "$TMP/dispatch.o"
"${CC[@]}" -c "$ROOT/runtime/arch/x86/com_bridge_x86.S" -o "$TMP/com.o"
"${CC[@]}" -c "$TMP/bridge.S" -o "$TMP/bridge.o"
"${CC[@]}" -c "$D/callers.S" -o "$TMP/callers.o"
"${CC[@]}" -c "$D/fixture.c" -o "$TMP/fixture.o"
"${CC[@]}" -no-pie "$TMP"/{fixture,callers,dispatch,com,bridge}.o -o "$TMP/check"
"$TMP/check"
echo 'x86_runtime_check: PASS'
