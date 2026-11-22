#!/usr/bin/env python3
"""runtime/arm/patch_guest_pointers.py — riscrive nel binario i puntatori a codice rimasti byte.

Problema: SRW emette come rilocazione (`daddr <etichetta>`) i puntatori che riconosce come tali, ma
una parte dei puntatori a funzione resta **byte** nei datasegment, tipicamente nelle tabelle che il
CRT si costruisce da solo (init/atexit table). Il valore e' l'indirizzo x86 originale. Sul runtime
i386 non si notava perche' il codice tradotto stava esattamente ai VA originali; qui il codice ARM
vive altrove, quindi un `call [tabella]` salterebbe in mezzo ai dati.

Soluzione: patch *nel file*, dopo il link, guidata dai simboli del binario stesso. Le etichette di
llasm sono `loc_<VA originale>` (es. `loc_406041`, `loc_1502DE1A`), quindi il nome del simbolo E'
la chiave: nessuna mappa da mantenere a mano.

Per ogni word dei segmenti dati del guest che cade in un intervallo di codice dell'immagine:
  * VA di una funzione tradotta            -> indirizzo ARM di quella funzione;
  * VA con bit 0 alzato (entry atexit MSVC: bersaglio = value & ~1) e bersaglio tradotto ->
    indirizzo ARM (il marcatore x86 non ha equivalente in ARM, viene annotato);
  * VA non tradotto                        -> 0. Zero e' la scelta sicura per le tabelle che
    testano lo zero prima di chiamare, e l'indirizzo viene stampato: e' la worklist del lifting
    (codice che il lifter non ha coperto: resta un buco visibile, non un crash silenzioso).

Il patch avviene una volta sola, a build time: nessuna scrittura a runtime, nessun segmento reso
scrivibile (i .tdata delle immagini sono sorgenti read-only, come nell'originale).

Uso: patch_guest_pointers.py --elf build/runtime-arm/starcraft-armv6
"""
import argparse
import re
import struct
import subprocess
import sys

SYM_RE = re.compile(r"^([0-9a-f]+)\s+\w\s+(?:loc|imagebase)_([0-9A-Fa-f]{6,8})$")
SEC_RE = re.compile(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\S*)")
CODE_RANGES = ((0x00401000, 0x004FE000), (0x15001000, 0x15046000))
DATA_SECTIONS = (".guest_exe_data", ".guest_exe_rdata", ".guest_exe_tdata",
                 ".guest_storm_data", ".guest_storm_rdata", ".guest_storm_tdata")


def read_sections(elf):
    out = subprocess.run(["llvm-readelf", "-SW", elf], capture_output=True, text=True).stdout
    secs = {}
    for line in out.splitlines():
        m = SEC_RE.match(line)
        if m and m.group(2) in ("PROGBITS", "NOBITS"):
            secs[m.group(1)] = (int(m.group(3), 16), int(m.group(4), 16), int(m.group(5), 16))
    return secs


def read_symbols(elf):
    out = subprocess.run(["llvm-nm", "--defined-only", elf], capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        m = SYM_RE.match(line.strip())
        if not m:
            continue
        arm, orig = int(m.group(1), 16), int(m.group(2), 16)
        for lo, hi in CODE_RANGES:
            if lo <= orig < hi:
                table.setdefault(orig, arm)
    return table


def in_code_range(v):
    return any(lo <= v < hi for lo, hi in CODE_RANGES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    secs = read_sections(a.elf)
    table = read_symbols(a.elf)
    if not table:
        raise SystemExit("nessuna funzione tradotta trovata nei simboli di %s" % a.elf)

    data = bytearray(open(a.elf, "rb").read())
    patched = zeroed = 0
    unlifted = {}

    for name in DATA_SECTIONS:
        if name not in secs:
            continue
        va0, off, size = secs[name]
        for i in range(0, size - 3, 4):
            v = struct.unpack_from("<I", data, off + i)[0]
            if not in_code_range(v):
                continue
            arm = table.get(v)
            if arm is None and (v & 1) and table.get(v & ~1) is not None:
                arm = table[v & ~1]
                unlifted[v] = unlifted.get(v, 0) + 1
            if arm is not None:
                if arm != v and not a.dry_run:
                    struct.pack_into("<I", data, off + i, arm)
                patched += 1
            else:
                if not a.dry_run:
                    struct.pack_into("<I", data, off + i, 0)
                zeroed += 1
                unlifted[v] = unlifted.get(v, 0) + 1

    if not a.dry_run:
        open(a.elf, "wb").write(data)

    print("patch_guest_pointers: %d funzioni tradotte note" % len(table))
    print("patch_guest_pointers: %d puntatori riscritti verso il codice ARM, %d azzerati "
          "(codice non tradotto)" % (patched, zeroed))
    if unlifted:
        top = sorted(unlifted.items(), key=lambda kv: -kv[1])[:16]
        print("patch_guest_pointers: worklist lifting (VA x occorrenze): "
              + " ".join("0x%08X(%d)" % kv for kv in top))
    return 0


if __name__ == "__main__":
    sys.exit(main())
