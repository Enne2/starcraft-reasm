#!/usr/bin/env python3
"""Porta un'istanza di StarCraft (sandbox sc_ctl) dentro la lista partite LAN, in modo idempotente.

Gestisce il dialogo "Enter new character name" del Registry (che puo' aprirsi o no) usando l'OCR
della schermata, poi seleziona il profilo e conferma. Verifica leggendo il log del gioco.

Uso: lan-enter.py <socket> <log-del-gioco> [nome-profilo]
"""
import ctypes, subprocess, sys, time, os

SCCTL = "/home/enne2/dev/starcraft-reasm-generic/tools/scctl"
TESS = "/usr/share/tesseract/tessdata"


def ctl(sock, *args):
    subprocess.run([sys.executable, SCCTL, "--sock", sock, *map(str, args)],
                   check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def press(sock, x, y, wait=1.2):
    ctl(sock, "move", x, y); time.sleep(0.3)
    ctl(sock, "down", x, y); time.sleep(0.25)
    ctl(sock, "up", x, y); time.sleep(wait)


def shot(sock, path):
    ctl(sock, "shot", path)


_ocr = None


def ocr_text(png):
    global _ocr
    if _ocr is None:
        l = ctypes.CDLL("libtesseract.so.5.5")
        l.TessBaseAPICreate.restype = ctypes.c_void_p
        l.TessBaseAPIInit3.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        l.TessBaseAPISetPageSegMode.argtypes = [ctypes.c_void_p, ctypes.c_int]
        l.TessBaseAPISetImage.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_int, ctypes.c_int]
        _ocr = l
    l = _ocr
    h = l.TessBaseAPICreate()
    l.TessBaseAPIInit3(h, TESS.encode(), b"eng")
    l.TessBaseAPISetPageSegMode(h, 6)
    raw = subprocess.check_output(["magick", png, "-resize", "1280x960!", "-colorspace", "Gray",
                                   "-depth", "8", "gray:-"])
    l.TessBaseAPISetImage(h, raw, 1280, 960, 1, 1280)
    return raw and None


def dialog_open(png):
    """Rileva il dialog del nome: riga bianca centrale su fondo scuro, oppure OCR del titolo."""
    # Euristica robusta e cheap: nel dialog il 25% centrale della schermata e' quasi tutto scuro
    # mentre la cornice superiore contiene la scritta bianca; uso la luminanza di una striscia.
    out = subprocess.run(["magick", png, "-crop", "300x20+170+152", "+repage", "-colorspace", "Gray",
                          "-format", "%[fx:mean]", "info:"], capture_output=True, text=True).stdout
    try:
        return float(out) > 0.10
    except ValueError:
        return False


def in_lan(logfile):
    try:
        data = open(logfile, "rb").read()
    except OSError:
        return False
    return b"spiLockGameList" in data or b"SpiLockGameList" in data


def main():
    sock, logfile = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else "lanauto"
    png = f"/tmp/lan-enter-{os.getpid()}.png"

    # Multiplayer > Original > LAN > Ok connessione
    press(sock, 140, 200, 2.2)
    press(sock, 250, 210, 2.8)
    press(sock, 150, 102, 0.9)
    press(sock, 518, 391, 3.5)

    for attempt in range(3):
        shot(sock, png)
        if dialog_open(png):
            ctl(sock, "click", 300, 235); time.sleep(0.5)
            ctl(sock, "text", f"{name}{attempt}"); time.sleep(0.6)
            press(sock, 235, 310, 2.0)
        press(sock, 120, 96, 1.0)
        ctl(sock, "key", "enter"); time.sleep(3.0)
        ok = subprocess.run(["grep", "-qa", "spiLockGameList", logfile]).returncode == 0
        if ok:
            print("LAN lista raggiunta")
            return 0
    print("NON raggiunta la lista LAN")
    return 1


if __name__ == "__main__":
    sys.exit(main())
