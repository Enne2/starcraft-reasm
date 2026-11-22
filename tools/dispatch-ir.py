#!/usr/bin/env python3
"""tools/dispatch-ir.py — make indirect guest branches resolvable on ARMv6.

WHY (oracle, not invention)
    `vendor/sr/llasm/llasm.d`, case "tcall" (~line 2962): an x86 `call reg` / `jmp reg` becomes

        %N = inttoptr i32 %T to void(%_cpu*)*
        musttail call fastcc void %N(%_cpu* %cpu)

    i.e. the raw integer in the register is used as a *host* function pointer. That is exact on
    i386 (translated code sits at the original guest VAs) and wrong on ARM: guest code addresses
    living in the image (function tables, vtables, the CRT `_initterm` array) are not host
    addresses. Measured consequence on the candidate of 13:01: `_initterm` entry 0x004060C1
    (a real x86 function at an *odd* address) reached `bx`, whose bit0 set the Thumb state, and
    the CPU executed the port's own ARM text at the numerically equal address → SIGILL.

WHAT THIS DOES
    1. rewrites every indirect `musttail call` of one module into

        store i32 %T, i32* @reasm_indirect_target
        musttail call fastcc void @reasm_indirect(%_cpu* %cpu)   ; runtime/arch/arm/indirect_dispatch.S

       (the value travels through a global because the musttail ABI requires the callee signature
       to be identical to the caller's);
    2. emits one sorted `hidden constant` table {guest VA, translated entry} covering every
       `loc_<VA>` procedure of the images given on the command line. The table replaces the old
       post-link byte patching of code pointers *and* makes an untranslated target a *named stop*
       instead of a wild branch.

USE
    tools/dispatch-ir.py --exe StarCraft.ll --storm storm.ll \\
                         --out-exe StarCraft.entry.ll --out-storm storm.entry.ll \\
                         --entry-wrap loc_404C21 --map-extra 0x404C21=sc_loc_404C21_impl

    tools/dispatch-ir.py --dump-map build/starcraft-armv6     # verifica post-link

Exit status: 0 ok; 2 on an inconsistent table (duplicate VA, or a guest VA numerically equal to
one of our own code addresses — the only case in which resolution order could mask a mistake).
"""
import argparse
import re
import struct
import sys

RE_DEFINE = re.compile(r"^define\s+(?:private\s+|hidden\s+)?fastcc\s+void\s+@loc_([0-9A-Fa-f]+)\(%_cpu\*")
RE_INDEF_ANY = re.compile(r"^define\s+.*?@loc_([0-9A-Fa-f]+)\(")
RE_ANY_DEF = re.compile(r"^define\s+.*?@([\w.]+)\(")
RE_STORM_DEF = RE_ANY_DEF
RE_INTTOPTR = re.compile(r"^%(\w+) = inttoptr i32 (%\w+) to void\(%_cpu\*\)\*$")
# Tabella di salto *dentro* il .text del guest, la cui base coincide con l'ingresso di una
# funzione tradotta: llasm emette `ptrtoint (@loc_<VA> to i32)`, cioe' l'indirizzo HOST della
# traduzione, e la `load` successiva legge codice macchina tradotto invece dei byte del PE.
# Oracolo (StarCraft 1.16.1, `memcpy` a 0x00406BF0): 0x00406C3D `jmp *0x00406C50(,%eax,4)`,
# 0x00406C44 `jmp *0x00406D4C(,%ecx,4)` con indice negativo che ricade sulla tabella a
# 0x00406D3C, 0x00406C4C `jmp *0x00406CD0(,%ecx,4)`; le tre tabelle contengono VA guest
# (0x00406D4C/0x00406D54/... 0x00406CF0..0x00406D33). Misurato in play2: la load host leggeva
# quattro byte 0x90 di padding e produceva il target 0x90909090. Il .text guest e' mappato ai
# suoi VA originali (sezione .guest_exe_text_0 a 0x00401000), quindi la lettura corretta e'
# quella del guest: si sostituisce il `ptrtoint` con la costante del VA.
RE_TABLE_BASE = re.compile(
    r"^(\s*)(%\S+) = add i32 ptrtoint \(void\(%_cpu\*\)\* @loc_([0-9A-Fa-f]+) to i32\), (.+)$")
# Alias `@loc_<VA>` verso il blob `@_text`: llasm vi copia i frammenti DATI del .text con un
# layout COMPATTATO, quindi l'offset nell'alias e' interno al blob e non quello del PE
# (es. `@loc_409268` -> @_text+344, mentre nel PE 0x00409268-0x00401000 = 0x8268). Un
# `ptrtoint` di uno di questi alias produce l'indirizzo HOST del blob: l'indicizzazione negativa
# o un accesso oltre il frammento legge byte che nel PE non esistono (play5: `@loc_409268 - 24`
# leggeva 0x0435A008 invece di 0x00409250). Il .text del PE e' mappato ai suoi VA originali
# (`.guest_exe_text_0` a 0x00401000, `.guest_storm_text_4` a 0x15001000), quindi il valore
# corretto e' il VA guest intero.
RE_TEXT_ALIAS = re.compile(
    r"^@loc_([0-9A-Fa-f]+) = private alias i8, i8\* getelementptr "
    r"\(i8, i8\* bitcast \(%__text\* @_text to i8\*\), i32 \d+\)$")
RE_PTRTOINT_TEXT = re.compile(r"ptrtoint \(i8\* @loc_([0-9A-Fa-f]+) to i32\)")
RE_TAILCALL = re.compile(r"^(\s*)musttail call fastcc void %(\w+)\(%_cpu\* %cpu\) nounwind$")

DECLS = """
; --- aggiunte da tools/dispatch-ir.py (non modificare a mano) ---
declare fastcc void @reasm_indirect(%_cpu*)
declare fastcc void @storm_552_arm(%_cpu*)
"""

# Il target dell'arco indiretto viaggia in `_cpu._ind_target` (indice 17 del tipo, offset 136):
# e' stato per-thread. Con il global precedente (`@reasm_indirect_target`) due thread tradotti in
# parallelo si sovrascrivevano il target fra lo store e la `ldr` in reasm_indirect.S.
# Il tipo emesso da llasm ha { i32 x12, [8 x double], i64 }: qui si allunga fino ai due puntatori
# e ai due campi finali, in modo che gli offset coincidano con `typedef struct _cpu` in
# runtime/common/cpu_llasm.h (verificati li' da _Static_assert).
CPU_TYPE_OLD = "%_cpu = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, " \
               "[8 x double], i64 }"
CPU_TYPE_NEW = "%_cpu = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, " \
               "[8 x double], i64, i64, i8*, i8*, i32, i32, i8* }"
CPU_IND_TARGET = 17        # offset 136
CPU_IND_ESP = 18           # offset 140 (scritto solo da indirect_dispatch.S)


def rewrite_module(path, out_path, wrap_name=None):
    """-> (table entries [(va, symbol)], rewritten call sites, wrapper applied)"""
    with open(path, errors="replace") as f:
        lines = f.readlines()

    entries = []
    defined = set()
    text_alias = set()          # VA il cui `@loc_<VA>` e' un alias dentro il blob `@_text`
    rewritten = 0
    table_fix = 0
    alias_fix = 0
    out = []
    i = 0
    n = len(lines)

    if wrap_name:
        # The C runtime calls the CRT entry as a plain C function; the LLASM body is fastcc.
        # Rename the body and append a C-ABI wrapper with the original name (verified minimal:
        # the wrapper is only reachable from the C side, so the guest's branch discipline is
        # untouched).
        renamed = False
        for idx, line in enumerate(lines):
            if re.match(r"^define\s+(?:private\s+|hidden\s+)?fastcc\s+void\s+@%s\(" % wrap_name, line):
                lines[idx] = re.sub(r"^define\s+(?:private\s+|hidden\s+)?fastcc\s+void\s+@%s\(" % wrap_name,
                                    "define hidden fastcc void @sc_%s_impl(" % wrap_name, line, count=1)
                renamed = True
                break
        if not renamed:
            sys.exit("dispatch-ir: %s non trovato in %s" % (wrap_name, path))

    while i < n:
        line = lines[i]
        if line.startswith("define private fastcc void @loc_"):
            line = "define hidden fastcc void @loc_" + line[len("define private fastcc void @loc_"):]
            lines[i] = line
        md = RE_STORM_DEF.match(line)
        if md:
            defined.add(md.group(1))
        ma = RE_TEXT_ALIAS.match(line.rstrip("\n"))
        if ma:
            text_alias.add(int(ma.group(1), 16))
        m = RE_DEFINE.match(line)
        if m:
            entries.append((int(m.group(1), 16), "loc_" + m.group(1)))
        elif wrap_name and line.startswith("define void @%s(%%_cpu*" % wrap_name):
            pass  # the C-ABI wrapper is not a guest entry: excluded from the table on purpose
        out.append(line)
        i += 1

    # second pass: rewrite indirect calls, function by function (SSA names are function-local)
    body = "".join(out)
    if wrap_name:
        body += (
            "\n; Wrapper C-ABI per l'entry del CRT (aggiunto da tools/dispatch-ir.py).\n"
            "define void @%s(%%_cpu* %%cpu) nounwind {\n"
            "entry:\n"
            "  call fastcc void @sc_%s_impl(%%_cpu* %%cpu) nounwind\n"
            "  ret void\n"
            "}\n" % (wrap_name, wrap_name)
        )

    if path.endswith("storm.ll"):
        def swap(name, replacement, rename=None):
            nonlocal body
            pat = re.compile(r"^(define protected fastcc void @" + re.escape(name) + r"\(%_cpu\* %cpu\) nounwind \{\n).*?^}\n", re.M | re.S)
            m = pat.search(body)
            if not m:
                sys.exit("dispatch-ir: funzione esportata %s non trovata in %s" % (name, path))
            old = m.group(0)
            if rename:
                old = old.replace("@" + name + "(", "@" + rename + "(", 1)
                body = body[:m.start()] + old + body[m.end():]
                body += replacement
            else:
                body = body[:m.start()] + replacement + body[m.end():]
        for ordinal, arm in ((313, "storm_313_arm"), (334, "storm_334_arm")):
            n = "ordinal_73746f726d2e646c6c_" + str(ordinal)
            swap(n, "define protected fastcc void @%s(%%_cpu* %%cpu) nounwind {\nentry:\n  musttail call fastcc void @%s(%%_cpu* %%cpu) nounwind\n  ret void\n}\n" % (n, arm))
        n552 = "ordinal_73746f726d2e646c6c_552"
        swap(n552, "define protected fastcc void @%s(%%_cpu* %%cpu) nounwind {\nentry:\n  musttail call fastcc void @storm_552_arm(%%_cpu* %%cpu) nounwind\n  ret void\n}\n" % n552, rename="orig_storm_552")
        skip_wrap = """
; SC_SMACK=0 video skip wrappers; #454/#457/#458 stack cleanup matches IDA retn 1c/0c/04.
"""
        n454="ordinal_73746f726d2e646c6c_454"
        swap(n454, """define protected fastcc void @ordinal_73746f726d2e646c6c_454(%_cpu* %cpu) nounwind {
entry:
  %enabled = call i32 @storm_video_skip_enabled()
  %skip = icmp ne i32 %enabled, 0
  br i1 %skip, label %video.skip, label %video.original
video.original:
  musttail call fastcc void @orig_storm_454(%_cpu* %cpu) nounwind
  ret void
video.skip:
  %esp.p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
  %esp = load i32, i32* %esp.p, align 4
  %out.addr = add i32 %esp, 28
  %out.ptr = inttoptr i32 %out.addr to i32*
  store i32 1442840577, i32* %out.ptr, align 4
  %ret.ptr = inttoptr i32 %esp to i32*
  %ret = load i32, i32* %ret.ptr, align 4
  %new.esp = add i32 %esp, 32
  store i32 %new.esp, i32* %esp.p, align 4
  %eax.p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0
  store i32 1, i32* %eax.p, align 4
  %target.p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 17
  store i32 %ret, i32* %target.p, align 4
  musttail call fastcc void @reasm_indirect(%_cpu* %cpu) nounwind
  ret void
}
""", rename="orig_storm_454")
        for ordinal, retv, pops in ((457, 0, 16), (458, 1, 8)):
            n="ordinal_73746f726d2e646c6c_"+str(ordinal)
            wrap="""define protected fastcc void @%s(%%_cpu* %%cpu) nounwind {
entry:
  %%esp.p = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 4
  %%esp = load i32, i32* %%esp.p, align 4
""" % n
            # Avoid constant-expression pointer arithmetic: use explicit SSA address.
            wrap += f"  %handle.addr = add i32 %esp, 4\n  %handle.p = inttoptr i32 %handle.addr to i32*\n  %handle = load i32, i32* %handle.p, align 4\n  %is.skip = icmp eq i32 %handle, 1442840577\n  br i1 %is.skip, label %video.skip, label %video.original\nvideo.original:\n  musttail call fastcc void @orig_storm_{ordinal}(%_cpu* %cpu) nounwind\n  ret void\nvideo.skip:\n  %ret.ptr = inttoptr i32 %esp to i32*\n  %ret = load i32, i32* %ret.ptr, align 4\n  %new.esp = add i32 %esp, {pops}\n  store i32 %new.esp, i32* %esp.p, align 4\n  %eax.p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0\n  store i32 {retv}, i32* %eax.p, align 4\n  %target.p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 17\n  store i32 %ret, i32* %target.p, align 4\n  musttail call fastcc void @reasm_indirect(%_cpu* %cpu) nounwind\n  ret void\n}}\n"
            swap(n, wrap, rename="orig_storm_%d" % ordinal)
        body += "\ndeclare i32 @storm_video_skip_enabled()\n"
    lines = body.splitlines(keepends=True)
    res = []
    fn_block = None
    for line in lines:
        if line.startswith("define "):
            fn_block = {}
        elif line.rstrip("\n") == CPU_TYPE_OLD:
            res.append(CPU_TYPE_NEW + "\n")
            continue
        elif line.startswith("}"):
            fn_block = None
        elif fn_block is not None:
            # ptrtoint di un alias dentro `@_text` -> VA guest (vedi RE_TEXT_ALIAS).
            if text_alias and "ptrtoint (i8* @loc_" in line:
                hits = [v for v in RE_PTRTOINT_TEXT.findall(line)
                        if int(v, 16) in text_alias]
                if hits:
                    line = RE_PTRTOINT_TEXT.sub(
                        lambda mm: str(int(mm.group(1), 16))
                        if int(mm.group(1), 16) in text_alias else mm.group(0), line)
                    alias_fix += len(hits)
            m = RE_TABLE_BASE.match(line.rstrip("\n"))
            if m:
                va = int(m.group(3), 16)
                res.append("%s%s = add i32 %d, %s\n" % (m.group(1), m.group(2), va, m.group(4)))
                table_fix += 1
                continue
            m = RE_INTTOPTR.match(line.rstrip("\n"))
            if m:
                fn_block[m.group(1)] = m.group(2)
                res.append(line)
                continue
            m = RE_TAILCALL.match(line.rstrip("\n"))
            if m and m.group(2) in fn_block:
                indent, dest = m.group(1), m.group(2)
                src = fn_block[dest]
                # Un temporaneo per sito (nome non numerico -> nessuna collisione con le SSA
                # generate da llasm; unico perche' il suffisso cresce con i valori del blocco).
                tn = "ind.t%d" % (len(fn_block) + 1)
                res.append('%s%%"%s" = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 %d\n'
                           % (indent, tn, CPU_IND_TARGET))
                res.append('%sstore i32 %s, i32* %%"%s", align 4\n' % (indent, src, tn))
                fn_block[tn] = src
                res.append("%smusttail call fastcc void @reasm_indirect(%%_cpu* %%cpu) nounwind\n"
                           % indent)
                rewritten += 1
                continue
        res.append(line)

    with open(out_path, "w") as f:
        f.writelines(res)
        f.write(DECLS)
    if table_fix:
        print("dispatch-ir: %s: %d basi di tabella dentro .text riportate al VA guest"
              % (path, table_fix))
    if alias_fix:
        print("dispatch-ir: %s: %d ptrtoint di alias @_text riportati al VA guest"
              % (path, alias_fix))
    return entries, rewritten, defined, table_fix, alias_fix


def emit_table(path_out, entries, local_syms):
    entries = sorted(entries)
    for a, b in zip(entries, entries[1:]):
        if a[0] == b[0]:
            sys.exit("dispatch-ir: VA duplicato %08X (%s / %s)" % (a[0], a[1], b[1]))
    if len(entries) != len(set(e[0] for e in entries)):
        sys.exit("dispatch-ir: tabella non univoca")
    with open(path_out, "r") as src:
        existing = set(re.findall(r"^declare\s+.*?@([\w.]+)\(", src.read(), re.M))
    with open(path_out, "a") as f:
        f.write("\n; Tabella VA guest -> entry tradotta (ordinata), generata da tools/dispatch-ir.py.\n")
        # Due array paralleli invece di un array di struct: il parser di LLVM 22 rifiuta le
        # struct letterali dentro un inizializzatore di array ("expected '}' at end of struct",
        # verificato con llvm-as), mentre [N x i32] / [N x ptr] si verificano senza problemi.
        for _va, sym in entries:
            if sym not in local_syms and sym not in existing:
                f.write("declare fastcc void @%s(%%_cpu*)\n" % sym)
        f.write("@reasm_va_map_n = hidden constant i32 %d\n" % len(entries))
        f.write("@reasm_va_va = hidden constant [%d x i32] [\n" % len(entries))
        for i, (va, _sym) in enumerate(entries):
            f.write("  i32 %d%s\n" % (va, "," if i + 1 < len(entries) else ""))
        f.write("], align 4\n")
        f.write("@reasm_va_fn = hidden constant [%d x ptr] [\n" % len(entries))
        for i, (_va, sym) in enumerate(entries):
            f.write("  ptr @%s%s\n" % (sym, "," if i + 1 < len(entries) else ""))
        f.write("], align 4\n")
    return len(entries)


# --------------------------------------------------------------------------------------------
# post-link verification: dump the table from the ELF and check the invariant
# --------------------------------------------------------------------------------------------
def dump_map(elf_path):
    """Post-link invariant check: no host entry address may equal a guest VA.

    That is the only way the resolution order in reasm_indirect_resolve() could pick the wrong
    branch (a return label mistaken for a translated guest VA). The table itself carries both
    sets, so the check needs nothing but the linked ELF.
    """
    import subprocess

    syms = {}
    for line in subprocess.run(["llvm-nm", elf_path], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in ("reasm_va_va", "reasm_va_fn", "reasm_va_map_n"):
            syms[p[2]] = int(p[0], 16)
    for need in ("reasm_va_va", "reasm_va_fn", "reasm_va_map_n"):
        if need not in syms:
            sys.exit("dump-map: simbolo %s assente in %s" % (need, elf_path))

    secs = []
    for line in subprocess.run(["readelf", "-SW", elf_path], capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if m:
            secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))

    def read_at(addr, count, elem="<I"):
        esize = struct.calcsize(elem)
        fmt = "<%d%s" % (count, elem[1:])
        for name, va, off, size in secs:
            if va and va <= addr < va + size:
                need = count * esize
                avail = va + size - addr
                if need > avail:
                    sys.exit("dump-map: %08X esce dalla sezione %s (servono %d byte, disponibili %d)"
                             % (addr, name, need, avail))
                with open(elf_path, "rb") as f:
                    f.seek(off + (addr - va))
                    raw = f.read(need)
                return list(struct.unpack(fmt, raw))
        sys.exit("dump-map: %08X non in nessuna sezione" % addr)

    n = read_at(syms["reasm_va_map_n"], 1)[0]
    vas = read_at(syms["reasm_va_va"], n)
    fns = read_at(syms["reasm_va_fn"], n)
    vas_set = set(vas)
    bad = [(v, f) for v, f in zip(vas, fns) if f in vas_set]
    print("voci: %d   primo VA %08X   ultimo VA %08X" % (n, vas[0], vas[-1]))
    print("ordinate e univoche: %s" % ("si" if all(a < b for a, b in zip(vas, vas[1:])) else "NO"))
    print("host entry il cui indirizzo coincide con un VA guest: %d" % len(bad))
    for v, f in bad[:10]:
        print("  collisione: VA %08X  entry %08X" % (v, f))
    return 0 if not bad else 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe")
    ap.add_argument("--storm")
    ap.add_argument("--out-exe")
    ap.add_argument("--out-storm")
    ap.add_argument("--entry-wrap", default="")
    ap.add_argument("--map-extra", action="append", default=[],
                    help="VA=simbolo: voce aggiuntiva (es. 0x404C21=sc_loc_404C21_impl)")
    ap.add_argument("--dump-map", default="")
    a = ap.parse_args()

    if a.dump_map:
        return dump_map(a.dump_map)

    if not (a.exe and a.out_exe):
        ap.error("servono --exe e --out-exe")
    exe_entries, exe_rw, exe_defined, exe_tf, exe_af = rewrite_module(
        a.exe, a.out_exe, a.entry_wrap or None)
    storm_entries, storm_rw, storm_tf, storm_af = [], 0, 0, 0
    if a.storm and a.out_storm:
        storm_entries, storm_rw, _storm_defined, storm_tf, storm_af = rewrite_module(a.storm, a.out_storm)
    entries = exe_entries + storm_entries
    for item in a.map_extra:
        va_s, sym = item.split("=", 1)
        entries.append((int(va_s, 16), sym))
    total = emit_table(a.out_exe, entries, exe_defined)
    print("dispatch-ir: %d VA in tabella (exe %d, storm %d), %d call-site indiretti riscritti "
          "(exe %d, storm %d), %d basi di tabella .text corrette (exe %d, storm %d), "
          "%d ptrtoint alias @_text portati al VA guest (exe %d, storm %d)"
          % (total, len(exe_entries), len(storm_entries),
             exe_rw + storm_rw, exe_rw, storm_rw, exe_tf + storm_tf, exe_tf, storm_tf,
             exe_af + storm_af, exe_af, storm_af))
    return 0


if __name__ == "__main__":
    sys.exit(main())
