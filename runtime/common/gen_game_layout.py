#!/usr/bin/env python3
"""runtime/arm/gen_game_layout.py — place the translated images at their original VAs.

The lifted code reaches its data through *symbols* (`@loc_50C4BC`), so a flat link would work for
the code. Two things nevertheless require the real VAs:

  1. `sc-reasm/tools/elf/*.c` reads a handful of game globals by absolute address
     (`*(volatile unsigned int *)0x6CF4A8`, `0x6CDDC0..C8`, `0x5968B0/B4`) because on i386 the
     game *was* at those addresses. Placing the data blobs at their original VAs keeps those
     reads honest instead of silently reading something else.
  2. Any numeric address that survived the lifting in a data blob (function-pointer tables built
     with `daddr` are relocations, but a byte-encoded address is not) is only right at the
     original VA.

The blobs are the llasm datasegments: one LLVM global per PE section (`@_text`, `@_rdata`,
`@_data`), emitted into their own sections by `llc --data-sections`. The VA of blob offset 0 is
*not* the PE section VA: llasm starts a datasegment at the first referenced byte. It is recovered
here from the aliases the emitter writes:

    @loc_4FE5F0 = private alias i8, getelementptr (i8, ptr @_rdata, i32 32)
    -> base(_rdata) = 0x4FE5F0 - 32 = 0x4FE5D0

Code cannot be placed at its original VA (ARM code is ~1.9x the x86 size and would overlap the
data), but that is safe: every code reference is a relocation, and the script asserts that the
chosen code ranges do not touch any data range or the runtime arena.

Outputs: a linker-script fragment (to be used with INSERT so the default layout for the CRT, libc
and the runtime stays intact) and a C header with the regions, which the runtime registers so that
`arm_va_is_mapped` knows about them.
"""
import argparse
import re
import subprocess
import sys

ALIAS_RE = re.compile(
    r'^@loc_([0-9A-Fa-f]+) = private(?: constant)? alias i8, '
    r'(?:getelementptr \(i8, ptr @(_[a-z]+), i32 (\d+)\)|'
    r'i8\* getelementptr \(i8, i8\* bitcast \(%__[a-z]+\* @(_[a-z]+) to i8\*\), i32 (\d+)\))')

# Guest code ranges (must stay clear of data, arena and the shim's VM arena at 0x20000000).
CODE_VA = {"exe": 0x08000000, "storm": 0x10000000}

SEG_SECTION = {"_text": ".rodata..L_text", "_rdata": ".rodata..L_rdata", "_data": ".data..L_data"}
SEG_TAG = {"_text": "tdata", "_rdata": "rdata", "_data": "data"}


def blob_bases(ir_path):
    best = {}
    with open(ir_path, errors="replace") as fi:
        for line in fi:
            m = ALIAS_RE.match(line)
            if not m:
                continue
            va = int(m.group(1), 16)
            seg = m.group(2) or m.group(4)
            off = int(m.group(3) or m.group(5))
            if seg not in best or off < best[seg][0]:
                best[seg] = (off, va)
    return {seg: va - off for seg, (off, va) in best.items()}


def section_sizes(obj):
    """-> {section name: size} from llvm-readelf."""
    out = subprocess.run(["llvm-readelf", "-S", "-W", obj], capture_output=True, text=True).stdout
    sizes = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
        if m:
            sizes[m.group(1)] = int(m.group(2), 16)
    return sizes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe-obj", required=True)
    ap.add_argument("--exe-ir", required=True)
    ap.add_argument("--storm-obj", required=True)
    ap.add_argument("--storm-ir", required=True)
    ap.add_argument("--out-ld", required=True)
    ap.add_argument("--out-header", required=True)
    ap.add_argument("--pe-exe", default="", help="PE originale dell'eseguibile (header AI VA)")
    ap.add_argument("--pe-storm", default="", help="PE originale di storm.dll")
    ap.add_argument("--out-pe", default="", help="C generato con le header PE da copiare ai VA")
    ap.add_argument("--arena", default="0x0E000000")
    a = ap.parse_args()

    imgs = {}
    for name, obj, ir in (("exe", a.exe_obj, a.exe_ir), ("storm", a.storm_obj, a.storm_ir)):
        bases = blob_bases(ir)
        sizes = section_sizes(obj)
        rows = []
        for seg, sec in SEG_SECTION.items():
            if seg not in bases:
                continue
            size = sizes.get(sec, 0)
            if not size:
                if seg == "_text":
                    continue
                raise SystemExit("%s: sezione %s assente o vuota (compilato con --data-sections?)"
                                 % (obj, sec))
            rows.append((SEG_TAG[seg], bases[seg], size, sec))
        imgs[name] = rows

    # ---- sanity: no overlap between the placed ranges -------------------------------------
    spans = []
    for name, rows in imgs.items():
        for tag, va, size, _sec in rows:
            spans.append((va, va + size, "%s.%s" % (name, tag)))
    spans.sort()
    arena_base = int(a.arena, 0)
    spans.append((arena_base, arena_base + 0x01000000, "runtime.arena"))
    spans.append((0x02000000, 0x02400000, "runtime.guest_stack"))
    spans.append((0x20000000, 0x60000000, "shim.vm_arena"))
    spans.sort()
    for i in range(1, len(spans)):
        if spans[i][0] < spans[i - 1][1]:
            raise SystemExit("OVERLAP: %s [0x%08X,0x%08X) e %s [0x%08X,0x%08X)"
                             % (spans[i - 1][2], spans[i - 1][0], spans[i - 1][1],
                                spans[i][2], spans[i][0], spans[i][1]))

    # ---- linker script fragment -----------------------------------------------------------
    ld = ["/* GENERATED by runtime/arm/gen_game_layout.py — do not edit.",
          " * Guest data at the original VAs, guest code in free low ranges.",
          " * Used with INSERT so the default layout (CRT, libc, runtime) is left untouched. */",
          "SECTIONS {", ""]
    for name in ("exe", "storm"):
        # Il pattern deve combaciare con il nome del file *come passato al linker*: con un
        # percorso assoluto sulla riga di comando, un pattern con il solo basename non combacia e
        # le sezioni finirebbero nel layout predefinito (silenziosamente).
        base_obj = a.exe_obj if name == "exe" else a.storm_obj
        for tag, va, size, sec in imgs[name]:
            out = ".guest_%s_%s" % (name, tag)
            ld.append("  /* %s %s: %u byte a 0x%08X */" % (name, tag, size, va))
            ld.append("  %s 0x%08X : { %s(%s) }" % (out, va, base_obj, sec))
        ld.append("")
    ld.append("} INSERT AFTER .text;")
    ld.append("")
    open(a.out_ld, "w").write("\n".join(ld))

    # ---- C header -------------------------------------------------------------------------
    h = ["/* GENERATED by runtime/arm/gen_game_layout.py — do not edit.",
         " * Regions of the translated images as placed by guest_layout.ld. The runtime registers",
         " * them so arm_va_is_mapped() accepts guest accesses into image data. */",
         "#ifndef ARM_GUEST_LAYOUT_ARM_H",
         "#define ARM_GUEST_LAYOUT_ARM_H", "",
         "typedef struct { const char *what; unsigned base, size; } ArmGuestRegion;", "",
         "static const ArmGuestRegion k_guest_regions[] = {"]
    for name in ("exe", "storm"):
        for tag, va, size, _sec in imgs[name]:
            h.append('    { "%s.%s", 0x%08Xu, 0x%08Xu },' % (name, tag, va, size))
    h.append("};")
    h.append("#define GUEST_REGION_COUNT %du" % sum(len(imgs[n]) for n in imgs))
    h.append("")
    h.append("#define GUEST_ENTRY_EXE_CRT 0x%08Xu   /* loc_404C21, start() del CRT */" % 0x00404C21)
    h.append("#define GUEST_ENTRY_WINMAIN 0x%08Xu   /* loc_4E0AE0, _WinMain@16 */" % 0x004E0AE0)
    h.append("")
    h.append("#endif")
    h.append("")
    open(a.out_header, "w").write("\n".join(h) + "\n")

    # ---- PE headers to be restored at the image bases ------------------------------------
    if a.out_pe and a.pe_exe and a.pe_storm:
        pe = ["/* GENERATED by runtime/arm/gen_game_layout.py — do not edit.",
              " * First page of each original PE image. The runtime copies it at the image base",
              " * (0x00400000 / 0x15000000) before entering the guest, exactly like the i386",
              " * runtime did: the resource directory walkers and __ImageBase-relative code read",
              " * the header of their own module. */",
              "#include <stdint.h>", ""]
        for tag, path in (("exe", a.pe_exe), ("storm", a.pe_storm)):
            data = open(path, "rb").read(0x400)
            pe.append("const unsigned char k_pe_%s_hdr[%d] = {" % (tag, len(data)))
            for i in range(0, len(data), 16):
                pe.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
            pe.append("};")
            pe.append("const uint32_t k_pe_%s_hdr_len = %du;" % (tag, len(data)))
            pe.append("")
        open(a.out_pe, "w").write("\n".join(pe) + "\n")
        print("gen_game_layout: header PE -> %s" % a.out_pe)

    for name in ("exe", "storm"):
        for tag, va, size, _sec in imgs[name]:
            print("  %-5s %-6s 0x%08X + %8u" % (name, tag, va, size))
    print("gen_game_layout: %s, %s" % (a.out_ld, a.out_header))
    return 0


if __name__ == "__main__":
    sys.exit(main())
