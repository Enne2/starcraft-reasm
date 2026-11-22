#!/usr/bin/env python3
"""Screenshot del framebuffer del guest via QMP (screendump), senza input.

Perche' esiste: `qmp-key.py shot` risolve il path nella cwd di QEMU (non della shell
che lo invoca) e, se gli si passa un nome che finisce in `.ppm`, converte e poi
**cancella** il file appena scritto. Questo helper usa sempre un path assoluto e
lascia sul disco solo il PNG.

Uso:
  qa-shot.py OUT.png            # un solo screendump
  qa-shot.py OUT.png 10 3       # 3 screendump a 10 s di intervallo: OUT-0.png, OUT-1.png, ...

Non invia eventi: solo `qmp_capabilities` + `screendump`.
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


class Qmp:
    def __init__(self) -> None:
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(15)
        self.s.connect(str(QMP))
        self.f = self.s.makefile("rwb")
        self.f.readline()            # greeting
        self.cmd("qmp_capabilities")

    def cmd(self, name: str, args: dict | None = None) -> dict:
        self.f.write((json.dumps({"execute": name, "arguments": args or {}}) + "\n").encode())
        self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                raise RuntimeError(f"QMP chiuso su {name}")
            r = json.loads(line)
            if "event" not in r:
                if "error" in r:
                    raise RuntimeError(f"{name}: {r['error']}")
                return r

    def close(self) -> None:
        try:
            self.s.close()
        except OSError:
            pass


def shot(q: Qmp, out: pathlib.Path) -> pathlib.Path:
    """screendump in PPM assoluto, poi conversione in PNG (richiede Pillow)."""
    out = out.resolve()
    ppm = out.with_suffix(".ppm")
    q.cmd("screendump", {"filename": str(ppm)})
    # screendump e' sincrono per il driver, ma il file puo' essere chiuso poco dopo
    for _ in range(50):
        if ppm.exists() and ppm.stat().st_size > 0:
            break
        time.sleep(0.05)
    from PIL import Image
    Image.open(ppm).save(out)
    ppm.unlink(missing_ok=True)
    return out


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = pathlib.Path(argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    interval = float(argv[2]) if len(argv) > 2 else 0.0
    count = int(argv[3]) if len(argv) > 3 else 1
    q = Qmp()
    try:
        for i in range(max(1, count)):
            name = out if count <= 1 else out.with_name(f"{out.stem}-{i}{out.suffix}")
            p = shot(q, name)
            print(f"screenshot: {p} ({p.stat().st_size} B)")
            if count > 1 and i + 1 < count:
                time.sleep(interval)
    finally:
        q.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
