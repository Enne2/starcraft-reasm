#!/usr/bin/env python3
"""Console seriale affidabile per la VM QEMU.

Scrive i comandi un carattere alla volta (i caratteri si perdono se inviati
troppo velocemente sulla console del guest emulato) e legge le risposte dal
logfile del chardev QEMU, cercando marcatori.

Uso:
  serial-cli.py tail [n]              mostra le ultime n righe del log
  serial-cli.py wait <regex> [sec]    attende che il log contenga la regex
  serial-cli.py send <testo>          invia testo + Invio (char by char)
  serial-cli.py login <user> <pass>   attende "login:" e fa login
  serial-cli.py run <comando>         esegue un comando e stampa la risposta
"""

from __future__ import annotations

import os
import pathlib
import re
import socket
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE = ROOT / "build" / "qemu" / "runtime"
SOCK = STATE / "serial.sock"
LOG = STATE / "serial.log"

CHAR_DELAY = 0.03
LINE_DELAY = 0.35


def log_text() -> str:
    try:
        return LOG.read_bytes().decode("utf-8", "replace")
    except FileNotFoundError:
        return ""


def log_size() -> int:
    try:
        return LOG.stat().st_size
    except FileNotFoundError:
        return 0


class Console:
    def __init__(self) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(str(SOCK))

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def write_char(self, ch: str) -> None:
        self.sock.sendall(ch.encode())

    def send(self, text: str, enter: bool = True) -> None:
        # Ctrl-C preliminare per svuotare eventuali linee pendenti.
        for ch in "\x03":
            self.write_char(ch)
            time.sleep(0.15)
        for ch in text:
            self.write_char(ch)
            time.sleep(CHAR_DELAY)
        if enter:
            time.sleep(0.25)
            self.write_char("\r")
        time.sleep(LINE_DELAY)

    def wait_for(self, pattern: str, timeout: float = 120.0, start: int = 0) -> tuple[bool, str]:
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            txt = log_text()[start:]
            if rx.search(txt):
                return True, txt
            time.sleep(1.0)
        return False, log_text()[start:]


def cmd_tail(n: int) -> int:
    lines = [ln for ln in log_text().splitlines() if ln.strip()]
    for ln in lines[-n:]:
        print(ln)
    return 0


def cmd_wait(pattern: str, timeout: float) -> int:
    ok, txt = Console.__dict__ and wait_only(pattern, timeout)
    print("TROVATO" if ok else "TIMEOUT", f"({len(txt)} caratteri letti)")
    return 0 if ok else 1


def wait_only(pattern: str, timeout: float) -> tuple[bool, str]:
    rx = re.compile(pattern)
    deadline = time.time() + timeout
    start = 0
    while time.time() < deadline:
        txt = log_text()[start:]
        if rx.search(txt):
            return True, txt
        time.sleep(1.0)
    return False, log_text()[start:]


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    action = argv[1]
    if action == "tail":
        return cmd_tail(int(argv[2]) if len(argv) > 2 else 30)
    if action == "wait":
        return cmd_wait(argv[2], float(argv[3]) if len(argv) > 3 else 120.0)
    if action == "size":
        print(log_size())
        return 0
    if action == "send":
        con = Console()
        con.send(argv[2])
        con.close()
        print("inviato")
        return 0
    if action == "login":
        user, password = argv[2], argv[3]
        ok, _ = wait_only(r"login:", 240)
        print("prompt login:", "ok" if ok else "timeout")
        con = Console()
        con.send(user)
        time.sleep(1.5)
        con.send(password)
        time.sleep(3)
        con.close()
        ok, txt = wait_only(r"[$#] ", 30, )
        print("login:", "ok" if ok else "da verificare")
        print("\n".join(txt.splitlines()[-5:]))
        return 0 if ok else 1
    if action == "run":
        start = log_size()
        con = Console()
        con.send(argv[2])
        con.close()
        time.sleep(2)
        print(log_text()[start:])
        return 0
    print(f"azione sconosciuta: {action}")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
