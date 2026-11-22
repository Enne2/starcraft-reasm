#!/usr/bin/env python3
"""Esegue un comando nella VM tramite la console seriale, in modo affidabile.

Scrive il comando un carattere alla volta (la console emulata perde caratteri
se inviati troppo velocemente), attende un marcatore di fine e stampa l'output.

Uso:
  guest-sh.py "<comando>" [timeout_sec]

Richiede una shell già aperta (login fatto) sulla console seriale.
"""

from __future__ import annotations

import pathlib
import socket
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE = ROOT / "build" / "qemu" / "runtime"
LOG = STATE / "serial.log"
SOCK = STATE / "serial.sock"

MARK = "__RC__"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    command = argv[1]
    timeout = float(argv[2]) if len(argv) > 2 else 300.0

    start = LOG.stat().st_size

    def connect() -> socket.socket:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(30)
        s.connect(str(SOCK))
        return s

    def send_all(sock: socket.socket, data: str) -> socket.socket:
        """Invia in piccoli blocchi; se il socket si blocca, riconnette."""
        for i in range(0, len(data), 8):
            chunk = data[i:i + 8]
            for attempt in range(3):
                try:
                    sock.sendall(chunk.encode())
                    break
                except OSError:
                    try:
                        sock.close()
                    except OSError:
                        pass
                    time.sleep(0.5)
                    sock = connect()
            time.sleep(0.12)
        return sock

    sock = connect()
    # Ctrl-C per svuotare eventuali linee pendenti, poi il comando.
    sock = send_all(sock, "\x03\x03")
    time.sleep(0.6)
    full = f"{command}; echo {MARK}$?\r"
    sock = send_all(sock, full)
    try:
        sock.close()
    except OSError:
        pass

    deadline = time.time() + timeout
    import re
    while time.time() < deadline:
        text = LOG.read_bytes()[start:].decode("utf-8", "replace")
        m = re.search(rf"{MARK}(\d+)", text)
        if m and m.group(0) not in text[: text.find(m.group(0)) + 1]:
            pass
        if m:
            keep = []
            for line in text.splitlines():
                if re.search(rf"{MARK}\d+", line):
                    break
                keep.append(line)
            # scarta l'eco del comando e le sequenze di controllo
            clean = [ln for ln in keep if ln.strip() and not ln.strip().startswith("$ ")]
            print("\n".join(clean))
            print(f"\n[rc={m.group(1)}]")
            return 0 if m.group(1) == "0" else 1
        time.sleep(1.0)
    print("[timeout] ultime righe:")
    print("\n".join(LOG.read_bytes()[start:].decode("utf-8", "replace").splitlines()[-15:]))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
