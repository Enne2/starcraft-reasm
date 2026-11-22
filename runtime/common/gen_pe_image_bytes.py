#!/usr/bin/env python3
"""runtime/arm/gen_pe_image_bytes.py — i byte dell'immagine PE che il lift NON colloca.

Modello di memoria che il port deve riprodurre (è quello di Windows, non una scelta):
una sezione PE è presente a `ImageBase+RVA` per `ceil(VirtualSize/0x1000)` pagine, con i byte del
file fino a `RawSize` e **zeri oltre** (la coda di una sezione con VirtualSize > RawSize è come un
.bss). Il lift colloca solo i byte *referenziati* (`@_text`, `@_rdata`, `@_data`), quindi:

  * exe `.text`  0x00401000..0x004FDF85, lift 0x00406C0C..0x00435DE8;
  * exe `.rdata` 0x004FE000..0x0050B52C, lift 0x004FE5D0..0x00509E8C;
  * exe `.data`  0x0050C000..0x006DD694, lift uguale (tutta la VirtualSize);
  * exe `.rsrc`  0x006DE000..0x006EC000, lift **assente**  <- il fault 0x006DE00C.

Senza questi byte una lettura del guest nei buchi o legge il `.text` del *port* (maschera: succedeva
nel layout vecchio) o va in SIGSEGV (caso `.rsrc`). I blob *liftati* NON sono copie letterali del PE
(contengono indirizzi host: tabelle di puntatori a funzione), quindi l'immagine NON si può
sovrascrivere in blocco: si collocano solo i buchi.

Questo script produce:

  1. un `.incbin` per buco, nella sezione `.guest_<img>_<sez>_<n>` con i flag presi dalle
     caratteristiche della sezione PE (`a`, `ax` se MEM_EXECUTE, `aw` se MEM_WRITE) — la
     differenza di flag è anche ciò che impedisce a lld di fondere in un'unica PT_LOAD le sezioni
     R dell'eseguibile (0x006DE000) e quelle di storm (0x15001000), cosa che gonfierebbe il file
     di 236 MB di zeri (misurato: 367.690.344 byte);
  2. lo script di link completo, con le righe `.guest_*` **ordinate per VA** (lld assegna gli
     offset di file nell'ordine dello script: fuori ordine, la mappatura VA→offset si rompe);
  3. la tabella `k_pe_gaps` di quadruple {VA, size, nome, flag} + `k_pe_gaps_n`, che il runtime
     registra in `arm_va_is_mapped`: le regioni registrate sono **esattamente** quelle linkate.

Il perimetro dei byte coperti è letto dallo script di link di partenza: ogni riga
`.guest_<img>_<tag> <VA> : { <obj>(<sezione>) }` dice *quale* input section finisce *dove*; la
dimensione viene dall'oggetto stesso (`llvm-readelf -SW`). Nessun elenco duplicato.

Uso (dentro tools/build-entry-obj.sh, dopo aver scritto guest_data.ld):
  gen_pe_image_bytes.py --pe exe=.../StarCraft.orig.exe --pe storm=.../storm.orig.dll \
      --ld-in .../guest_data.ld --ld-obj .../guest_pe_bytes.o --out-ld .../guest_data.ld \
      --out-asm .../guest_pe_bytes.S --blob-dir .../pe-bytes
"""
import argparse
import os
import re
import struct
import subprocess
import sys

PAGE = 0x1000
# Spazi che non devono mai essere coperti dai byte dell'immagine (runtime del port).
RESERVED = [(0x02000000, 0x02400000, "guest_stack"),
            (0x0E000000, 0x0F000000, "runtime_arena"),
            (0x18000000, 0x20000000, "port_layout")]

LD_ROW = re.compile(r'^\s*\.guest_(\w+)_(\w+)\s+0x([0-9A-Fa-f]+)\s*:\s*\{\s*(\S+?)\s*\((\S+)\)\s*\}')
LD_END = re.compile(r'^\}\s*INSERT\b')

# IMAGE_SCN_MEM_*
SCN_EXECUTE = 0x20000000
SCN_WRITE = 0x80000000


def pe_info(path):
    d = open(path, "rb").read()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    if d[e:e + 4] != b"PE\0\0":
        raise SystemExit("%s: non è un PE" % path)
    nsec = struct.unpack_from("<H", d, e + 6)[0]
    optsz = struct.unpack_from("<H", d, e + 20)[0]
    opt = e + 24
    if struct.unpack_from("<H", d, opt)[0] != 0x10B:
        raise SystemExit("%s: non è un PE32" % path)
    base = struct.unpack_from("<I", d, opt + 28)[0]
    sizeimg = struct.unpack_from("<I", d, opt + 56)[0]
    sec, out = opt + optsz, []
    for i in range(nsec):
        o = sec + 40 * i
        name = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
        chars = struct.unpack_from("<I", d, o + 36)[0]
        out.append(dict(name=name, va=va, vsize=vsz, rawsize=rsz, rawptr=raw, chars=chars))
    return dict(path=path, data=d, base=base, sizeimage=sizeimg, secs=out)


def obj_section_size(obj, sec):
    out = subprocess.run(["llvm-readelf", "-SW", obj], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
        if m and m.group(1) == sec:
            return int(m.group(2), 16)
    raise SystemExit("%s: sezione %s assente" % (obj, sec))


def parse_ld(path):
    """-> (preambolo, righe [(va, testo)], coda) dello script di link."""
    pre, rows, tail = [], [], []
    state = 0
    for line in open(path):
        m = LD_ROW.match(line)
        if m:
            if state == 2:
                raise SystemExit("%s: riga .guest_* dopo la chiusura" % path)
            state = 1
            rows.append((int(m.group(3), 16), line.rstrip("\n")))
            continue
        if state == 1 and not line.strip():
            continue
        if LD_END.match(line):
            state = 2
            tail.append(line.rstrip("\n"))
            continue
        if state == 2:
            tail.append(line.rstrip("\n"))
        elif state == 0:
            pre.append(line.rstrip("\n"))
        # state 1 e riga non-riga: commento/blank generato da noi -> scartato
    if not rows:
        raise SystemExit("%s: nessuna riga .guest_* (script inatteso)" % path)
    return pre, rows, tail


def gaps_of(lo, hi, cov):
    out = []
    cur = lo
    for a, b in sorted(cov):
        if b <= cur:
            continue
        if a > cur:
            out.append((cur, min(a, hi)))
        cur = max(cur, b)
        if cur >= hi:
            break
    if cur < hi:
        out.append((cur, hi))
    return [(a, b) for a, b in out if b > a]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pe", action="append", required=True, metavar="NOME=PERCORSO")
    ap.add_argument("--ld-in", required=True)
    ap.add_argument("--out-ld", required=True)
    ap.add_argument("--ld-obj", required=True, help="percorso dell'oggetto come lo vede il linker")
    ap.add_argument("--out-asm", required=True)
    ap.add_argument("--blob-dir", required=True)
    ap.add_argument("--arch", choices=("arm", "x86"), default="arm")
    a = ap.parse_args()

    imgs = {}
    for spec in a.pe:
        name, _, path = spec.partition("=")
        if not name or not path:
            raise SystemExit("--pe richiede NOME=PERCORSO")
        imgs[name] = pe_info(path)

    pre, rows, tail = parse_ld(a.ld_in)

    # copertura per immagine: le righe .guest_<img>_<tag> del link, con la loro dimensione
    cov = {}
    for va, text in rows:
        m = LD_ROW.match(text)
        img = m.group(1)
        size = obj_section_size(m.group(4), m.group(5))
        cov.setdefault(img, []).append((va, va + size))

    os.makedirs(a.blob_dir, exist_ok=True)
    asm = ["/* GENERATED by runtime/arm/gen_pe_image_bytes.py — non modificare a mano.",
           " * Byte delle sezioni PE che il lift non colloca, ai loro VA originali:",
           " * i buchi di .text/.rdata e le sezioni intere non liftate (.rsrc, .reloc).",
           " * Un .incbin per buco + la tabella k_pe_gaps {VA, size, nome, flag}. */",
           ("    .syntax unified" if a.arch == "arm" else ""), ""]
    new_rows, entries, n = [], [], 0
    for name in sorted(imgs):
        pe = imgs[name]
        imgcov = cov.get(name, [])
        for sec in pe["secs"]:
            lo = pe["base"] + sec["va"]
            # estensione visibile della sezione: VirtualSize arrotondata alla pagina, limitata da
            # SizeOfImage e (per eccesso) da RawSize, che può superare di poco la VirtualSize.
            hi_va = sec["va"] + max(sec["vsize"], sec["rawsize"])
            hi = pe["base"] + min((hi_va + PAGE - 1) & ~(PAGE - 1), pe["sizeimage"])
            if hi <= lo:
                continue
            for g_lo, g_hi in gaps_of(lo, hi, imgcov):
                for r_lo, r_hi, rname in RESERVED:
                    if g_lo < r_hi and r_lo < g_hi:
                        raise SystemExit("i byte PE %s..%s invadono %s"
                                         % (hex(g_lo), hex(g_hi), rname))
                buf = bytearray()
                for va in range(g_lo, g_hi):
                    off = va - lo
                    buf.append(pe["data"][sec["rawptr"] + off] if off < sec["rawsize"] else 0)
                tag = "%s.%s" % (name, sec["name"].lstrip("."))
                sname = ".guest_%s_%s_%d" % (name, re.sub(r"[^0-9A-Za-z]", "_",
                                                         sec["name"].lstrip(".")), n)
                blob = os.path.join(a.blob_dir, "%s_%d.bin"
                                    % (re.sub(r"[^0-9A-Za-z]", "_", tag), n))
                open(blob, "wb").write(buf)
                flags = "a" + ("x" if sec["chars"] & SCN_EXECUTE else "") \
                            + ("w" if sec["chars"] & SCN_WRITE else "")
                asm.append('    .section %s,"%s",%%progbits' % (sname, flags))
                asm.append('    .incbin "%s"' % os.path.abspath(blob))
                namesym = ".Lpe_gap_name_%d" % n
                asm.append('    .section .rodata.guest_pe_gaps,"a",%progbits')
                asm.append('%s: .asciz "%s"' % (namesym, tag))
                new_rows.append((g_lo, "  %s 0x%08X : { %s(%s) }   /* %s, %u byte, \"%s\" */"
                                       % (sname, g_lo, a.ld_obj, sname, tag, g_hi - g_lo, flags)))
                entries.append((g_lo, g_hi - g_lo, namesym, flags))
                print("  %-16s 0x%08X + %6u byte  (%s)" % (tag, g_lo, g_hi - g_lo, flags))
                n += 1

    word = ".word" if a.arch == "arm" else ".long"
    asm += ["",
            "    .section .rodata.guest_pe_gaps,\"a\",%progbits",
            "    .balign 4",
            "    .globl k_pe_gaps_n",
            "k_pe_gaps_n: %s %d" % (word, len(entries)),
            "    .globl k_pe_gaps",
            "k_pe_gaps:"]
    for va, size, namesym, flags in entries:
        asm.append("    %s 0x%08X, %u, %s, %d" % (word, va, size, namesym, 1 if "w" in flags else 0))
    asm.append("")
    open(a.out_asm, "w").write("\n".join(asm))

    # script di link finale: preambolo, righe ordinate per VA, coda
    allrows = [(va, text) for va, text in rows] + new_rows
    allrows.sort(key=lambda r: r[0])
    open(a.out_ld, "w").write("\n".join(pre + [t for _, t in allrows] + tail) + "\n")
    print("gen_pe_image_bytes: %d regioni, %d righe di link -> %s, %s"
          % (len(entries), len(allrows), a.out_asm, a.out_ld))
    return 0


if __name__ == "__main__":
    sys.exit(main())
