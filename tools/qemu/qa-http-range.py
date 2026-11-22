#!/usr/bin/env python3
"""Server HTTP minimale su loopback con supporto Range (ripresa di un trasferimento).

Perche' esiste: `python3 -m http.server` **ignora** l'header Range (risponde 200 con il file
intero), quindi `curl -C -` non puo' riprendere: misurato il 2026-11-20 su questo rig, con un
trasferimento che nel guest procede a ~0,25 MB/s, la ripresa non e' un lusso ma la sola difesa
contro l'uccisione della VM a meta' file (un file da 579 MB richiede ~40 minuti).

Bind solo su 127.0.0.1 (il guest lo raggiunge come 10.0.2.2): nessuna esposizione di rete.

Uso:
  qa-http-range.py <directory> [porta] [interfaccia]
"""

from __future__ import annotations

import os
import pathlib
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

ROOT = pathlib.Path("/")
CHUNK = 256 * 1024


class RangeHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "qa-http-range/1"

    def log_message(self, fmt: str, *args) -> None:      # log compatto, niente stdout rumoroso
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _resolve(self, url: str) -> pathlib.Path | None:
        p = (ROOT / unquote(urlsplit(url).path).lstrip("/")).resolve()
        try:
            p.relative_to(ROOT.resolve())
        except ValueError:
            return None
        return p if p.is_file() else None

    def do_HEAD(self) -> None:
        self._respond(head_only=True)

    def do_GET(self) -> None:
        self._respond(head_only=False)

    def _respond(self, head_only: bool) -> None:
        f = self._resolve(self.path)
        if f is None:
            self.send_error(404, "not found")
            return
        size = f.stat().st_size
        start, end, status = 0, size - 1, 200
        rng = self.headers.get("Range")
        if rng:
            m = re.fullmatch(r"bytes=(\d*)-(\d*)", rng.strip())
            if m:
                a, b = m.group(1), m.group(2)
                if a:
                    start = int(a)
                    end = int(b) if b else size - 1
                elif b:                       # last N bytes
                    start = max(0, size - int(b))
                if start >= size or start > end:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                end = min(end, size - 1)
                status = 206
        n = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(n))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if head_only:
            return
        try:
            with f.open("rb") as fh:
                fh.seek(start)
                left = n
                while left > 0:
                    block = fh.read(min(CHUNK, left))
                    if not block:
                        break
                    self.wfile.write(block)
                    left -= len(block)
        except (BrokenPipeError, ConnectionResetError):
            pass                              # client sparito (VM uccisa): nessun errore fatale


def main(argv: list[str]) -> int:
    global ROOT
    if len(argv) < 2:
        print(__doc__)
        return 2
    ROOT = pathlib.Path(argv[1]).resolve()
    if not ROOT.is_dir():
        print(f"directory inesistente: {ROOT}", file=sys.stderr)
        return 2
    port = int(argv[2]) if len(argv) > 2 else 8898
    iface = argv[3] if len(argv) > 3 else "127.0.0.1"
    ThreadingHTTPServer.daemon_threads = True
    srv = ThreadingHTTPServer((iface, port), RangeHandler)
    print(f"serving {ROOT} su http://{iface}:{port} (Range abilitato)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
