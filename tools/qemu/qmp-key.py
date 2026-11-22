#!/usr/bin/env python3
"""Invia tasti alla VM via QMP send-key (tastiera USB -> console tty1).

Uso:
  qmp-key.py key <nome> [<nome>...]     es. qmp-key.py key ret down
  qmp-key.py type "<testo>"             digita testo (mappa US)
  qmp-key.py shot <file.png>            screendump
"""

from __future__ import annotations

import json
import pathlib
import socket
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE = ROOT / "build" / "qemu" / "runtime"
QMP = STATE / "qmp.sock"

# Mappa carattere -> qcode (layout US)
CHARS = {
    "a": "a", "b": "b", "c": "c", "d": "d", "e": "e", "f": "f", "g": "g",
    "h": "h", "i": "i", "j": "j", "k": "k", "l": "l", "m": "m", "n": "n",
    "o": "o", "p": "p", "q": "q", "r": "r", "s": "s", "t": "t", "u": "u",
    "v": "v", "w": "w", "x": "x", "y": "y", "z": "z",
    "0": "0", "1": "1", "2": "2", "3": "3", "4": "4",
    "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
    " ": "spc", "-": "minus", "_": "shift-minus", ".": "dot", ",": "comma",
    "/": "slash", ":": "shift-semicolon", ";": "semicolon", "=": "equal",
    "'": "apostrophe", '"': "shift-apostrophe", "\n": "ret",
    "(": "shift-9", ")": "shift-0", "$": "shift-4", "#": "shift-3",
    "@": "shift-2", "!": "shift-1", "%": "shift-5", "*": "shift-8",
    "+": "shift-equal", "&": "shift-7", "?": "shift-slash",
}
SHIFTED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ")


class Qmp:
    def __init__(self) -> None:
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect(str(QMP))
        self.f = self.s.makefile("rwb")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name: str, args: dict | None = None) -> dict:
        self.f.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "event" not in r:
                return r

    def close(self) -> None:
        self.s.close()


def press(q: Qmp, names: list[str]) -> None:
    keys = []
    for n in names:
        if n.startswith("shift-"):
            keys.append({"type": "qcode", "data": "shift"})
            keys.append({"type": "qcode", "data": n[6:]})
        else:
            keys.append({"type": "qcode", "data": n})
    q.cmd("send-key", {"keys": keys, "hold-time": 60})


def type_text(q: Qmp, text: str) -> None:
    for ch in text:
        if ch in SHIFTED:
            press(q, ["shift", ch.lower()])
        elif ch in CHARS:
            code = CHARS[ch]
            if code.startswith("shift-"):
                press(q, ["shift", code[6:]])
            else:
                press(q, [code])
        else:
            raise SystemExit(f"carattere non mappato: {ch!r}")
        time.sleep(0.06)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    q = Qmp()
    try:
        if argv[1] == "key":
            for n in argv[2:]:
                press(q, [n])
                time.sleep(0.2)
            print("ok:", " ".join(argv[2:]))
        elif argv[1] == "type":
            type_text(q, argv[2])
            print("digitato:", argv[2])
        elif argv[1] == "shot":
            q.cmd("screendump", {"filename": str(pathlib.Path(argv[2]).with_suffix(".ppm"))})
            from PIL import Image
            ppm = pathlib.Path(argv[2]).with_suffix(".ppm")
            Image.open(ppm).save(argv[2])
            ppm.unlink(missing_ok=True)
            print("screenshot:", argv[2])
        else:
            print(f"azione sconosciuta: {argv[1]}")
            return 2
    finally:
        q.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
