#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'kill "${PID:-}" 2>/dev/null || true; wait "${PID:-}" 2>/dev/null || true; rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -I"$ROOT/runtime/shim-port/elf" \
  "$ROOT/tests/sc_ctl_test/harness.c" "$ROOT/runtime/shim-port/elf/sc_ctl.c" -pthread -o "$TMP/harness"
SC_CTL_SOCK="$TMP/test.sock" "$TMP/harness" >"$TMP/server.log" 2>&1 & PID=$!
python3 - "$ROOT/tools/scctl" "$TMP/test.sock" "$TMP/test.png" <<'PY'
import os, socket, struct, subprocess, sys, time, zlib
client, sock, png = sys.argv[1:]
def call(cmd):
    return subprocess.check_output([client, "--sock", sock, *cmd.split()], text=True).strip()
for _ in range(100):
    try:
        state=call("state")
        break
    except (OSError, subprocess.CalledProcessError):
        time.sleep(.03)
else:
    raise SystemExit("socket did not become ready")
assert state.startswith("ok state frame=") and "w=640 h=480" in state and "pal=1" in state, state
reply=call("shot " + png)
assert reply.startswith("ok shot "), reply
raw_path=os.path.join(os.path.dirname(png), "canvas.raw")
reply=call("rawshot " + raw_path)
assert reply.startswith("ok rawshot "), reply
raw_canvas=open(raw_path,"rb").read()
assert len(raw_canvas)==640*480 and set(raw_canvas)=={1}, (len(raw_canvas), set(raw_canvas))
assert os.path.isfile(raw_path + ".png"), "rawshot must save a palette-rendered PNG from the same captured frame"
b=open(png,"rb").read(); assert b[:8]==b"\x89PNG\r\n\x1a\n"
o=8; chunks=[]; width=height=None
while o<len(b):
    n=struct.unpack(">I",b[o:o+4])[0]; typ=b[o+4:o+8]; data=b[o+8:o+8+n]; o+=12+n
    if typ==b"IHDR": width,height=struct.unpack(">II",data[:8])
    if typ==b"IDAT": chunks.append(data)
assert (width,height)==(640,480),(width,height)
raw=zlib.decompress(b"".join(chunks))
assert raw[0]==0 and raw[1:4]==bytes((0x12,0x34,0x56)),raw[:4]
print(f"PASS PNG {width}x{height} {len(b)} bytes pixel={raw[1:4].hex()}")
PY
