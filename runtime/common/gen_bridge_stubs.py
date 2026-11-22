#!/usr/bin/env python3
"""runtime/arm/gen_bridge_stubs.py — generate bridge_stubs.S, bridge_symbols.c from the .def files.

One table, one ARM gate, three kinds of import entries:

    ENTRY(<symbol>, <nargs>, <impl>)   hand-written C implementation with the guest ABI
                                       (reads its own arguments, sets cpu->_eax/_edx).
    SHIM(<symbol>, <nargs>, <pops>)    C function from sc-reasm/tools/elf, reached through a
                                       generated per-arity caller (gen_shims_bridge.py). The C
                                       object's symbol was renamed to <symbol>__c by objcopy, so
                                       the import name is free for the guest-ABI thunk.
    UNK(<symbol>)                      no implementation: a *declared* trampoline that logs the
                                       name and stops (rule D19: never a stub that returns).

Every stub is `ldr r1, =index ; b arm_bridge_common`. The gate saves the AAPCS callee-saved
registers, asks the C dispatcher for the guest VA to resume at, restores sp and *branches*:
a guest `call`/`ret` pair therefore costs zero host stack (measured: a hot import in a loop does
not grow the stack, whereas a plain C return would).

Why the table is generated from a `.def` and not hand-written: the stub index in the assembly and
the index in the C table must agree, and the argument count is the callee-pop count of a stdcall.
One source, no drift.

Usage:
  gen_bridge_stubs.py --def runtime/arm/bridge_table.def [--also build/.../bridge_shims.def] \
      --out build/runtime-arm/bridge_stubs.S --csym-out build/runtime-arm/bridge_symbols.c \
      --table-out build/runtime-arm/bridge_table.c
"""
import argparse
import re
import sys

RE_ENTRY = re.compile(r"^\s*ENTRY\(\s*([A-Za-z_]\w*)\s*,\s*(\d+)\s*,\s*([A-Za-z_]\w*)\s*\)")
# Nomi per cui l'implementazione del *port* deve vincere sullo shim: lo shim chiama con l'ABI C
# un puntatore che nel gioco e' un ingresso *tradotto* (ABI `fastcc void(%_cpu*)`).
#   CreateThread         -> il corpo del thread (misurato SIGSEGV, REPORT-flash-heap.md §4)
#   WaitForSingleObject  -> recognizer dell'handle del nostro thread
#   DialogBoxParamA      -> il DialogProc (misurato SIGBUS subito dopo la finestra, §9.1)
HAND_WINS_OVER_SHIM = {"CreateThread", "WaitForSingleObject", "DialogBoxParamA"}
# The i386 shim's Get/SetLastError use the host Linux %fs segment, while translated
# callbacks expect the emulated Windows TEB. Route only the x86 bridge to the port bodies.
X86_HAND_WINS_OVER_SHIM = {"GetLastError", "SetLastError"}

RE_SHIM = re.compile(r"^\s*SHIM\(\s*([A-Za-z_]\w*)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")
RE_UNK = re.compile(r"^\s*UNK\(\s*([A-Za-z_]\w*)\s*\)")


def parse(path):
    """-> list of (symbol, kind, nargs, pops, target)."""
    out = []
    for line in open(path, encoding="utf-8"):
        s = line.split("/*")[0].strip()
        m = RE_ENTRY.match(s)
        if m:
            out.append((m.group(1), "HAND", int(m.group(2)), int(m.group(2)), m.group(3), 0))
            continue
        m = RE_SHIM.match(s)
        if m:
            out.append((m.group(1), "SHIM", int(m.group(2)), int(m.group(3)),
                        m.group(1) + "__c", int(m.group(4))))
            continue
        m = RE_UNK.match(s)
        if m:
            out.append((m.group(1), "UNK", 0, 0, "", 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--def", dest="deffile", required=True)
    ap.add_argument("--arch", choices=("arm", "x86"), default="arm")
    ap.add_argument("--also", default="", help="extra def file (generated SHIM/UNK entries)")
    ap.add_argument("--out", required=True, help="assembly stubs")
    ap.add_argument("--csym-out", default="", help="C table name -> stub address")
    ap.add_argument("--table-out", default="", help="C dispatcher table")
    ap.add_argument("--only-needed", default="",
                    help="tieni solo le voci referenziate dal guest (una per riga): nel link del "
                         "gioco un HAND non usato colliderebbe con il simbolo C omonimo dello shim")
    a = ap.parse_args()

    entries = parse(a.deffile)
    if a.only_needed:
        # Filtro applicato SOLO alle voci scritte a mano: quelle generate (bridge_shims.def)
        # esistono perche' servono davvero (statiche o risolte via GetProcAddress).
        needed = set(x.strip() for x in open(a.only_needed) if x.strip())
        before = len(entries)
        entries = [e for e in entries if e[0] in needed]
        if len(entries) != before:
            print("gen_bridge_stubs: %d voci a mano scartate (non referenziate dal guest)"
                  % (before - len(entries)))
    if a.also:
        entries += parse(a.also)
    if not entries:
        raise SystemExit("no entries found in %s" % a.deffile)
    # Same import in both files? The shim implementation is the project's own (same semantics as
    # the i386 runtime), so SHIM wins over the hand-written HAND entry of bridge_table.def, which
    # exists for the standalone smoke test where no shim is linked. Two entries of the same kind
    # are a real error.
    #
    # UNK e' il *ripiego* generato da gen_shims_bridge.py per un simbolo indefinito al link: se lo
    # stesso nome ha una voce con implementazione (ENTRY a mano oppure SHIM), vince quella. Senza
    # questa regola un simbolo che il runtime implementa ma che compare ancora in bridge_shims.def
    # (file generato prima della modifica alla .def) produrrebbe due stub con lo stesso nome.
    by_name = {}
    overridden = []
    replaced_unk = []
    for e in entries:
        old = by_name.get(e[0])
        if old is None:
            by_name[e[0]] = e
        elif {old[1], e[1]} == {"HAND", "SHIM"}:
            # Eccezione dichiarata: per HAND_WINS_OVER_SHIM l'implementazione del
            # *port* deve vincere sullo shim. Lo shim chiama il puntatore del thread con l'ABI C,
            # mentre il corpo del thread guest e' un ingresso tradotto con ABI `void(%_cpu*)`:
            # misurato SIGSEGV (work/link/REPORT-flash-heap.md §4). Il port lo esegue sullo stack
            # emulato (runtime/arm/guest_call_arm.c) e riconosce il proprio handle in
            # WaitForSingleObject; gli handle veri (eventi, UI) restano allo shim.
            hand_wins = HAND_WINS_OVER_SHIM | (X86_HAND_WINS_OVER_SHIM if a.arch == "x86" else set())
            keep = e if (e[1] == "SHIM" and e[0] not in hand_wins) else old
            by_name[e[0]] = keep
            overridden.append(e[0])
        elif "UNK" in (old[1], e[1]) and old[1] != e[1]:
            keep = old if old[1] != "UNK" else e
            by_name[e[0]] = keep
            replaced_unk.append(e[0])
        else:
            raise SystemExit("duplicate bridge entry: %s (%s vs %s)" % (e[0], old[1], e[1]))
    entries = list(by_name.values())
    if overridden:
        print("gen_bridge_stubs: SHIM prevale su HAND per: %s" % ", ".join(sorted(overridden)))
    if replaced_unk:
        print("gen_bridge_stubs: implementazione prevale su UNK per: %s"
              % ", ".join(sorted(replaced_unk)))

    # ---- assembly -------------------------------------------------------------------------
    if a.arch == "arm":
        asm = ["/* GENERATED by runtime/arm/gen_bridge_stubs.py — do not edit. */",
               "\t.arm", "\t.syntax unified", "\t.text", "",
               "\t.global arm_bridge_common", "\t.type arm_bridge_common, function",
               "arm_bridge_common:",
               "\t@ r0 = _cpu*, r1 = bridge table index",
               "\tpush\t{r4, r5, r6, lr}\t@ 16 bytes: keeps sp 8-byte aligned for the C call",
               "\tmov\tr4, r0",
               "\tbl\tarm_bridge_common_c\t@ (cpu, index) -> r0 = guest VA to resume at",
               "\tmov\tr2, r0\t\t\t@ r2 is caller-saved and NOT restored by the pop below",
               "\tmov\tr0, r4\t\t\t@ restore the guest cpu pointer (r0 = %_cpu for the guest)",
               "\tpop\t{r4, r5, r6, lr}\t@ sp back to the entry value: no host frame per edge",
               "\tbx\tr2",
               "\t.size arm_bridge_common, . - arm_bridge_common", ""]
    else:
        asm = ["/* GENERATED by runtime/arm/gen_bridge_stubs.py — do not edit. */",
               "\t.text", "\t.globl x86_bridge_common", "\t.type x86_bridge_common, @function",
               "x86_bridge_common:", "\tpushl %ebp", "\tmovl %esp, %ebp",
               "\tpushl %ebx", "\tpushl %ecx", "\tpushl %eax", "\tmovl %ecx, %ebx",
               "\tandl $-16, %esp", "\tsubl $8, %esp",
               "\tpushl -12(%ebp)", "\tpushl -8(%ebp)", "\tcall arm_bridge_common_c",
               "\taddl $16, %esp", "\tsubl $12, %esp", "\tpushl %eax",
               "\tcall reasm_indirect_resolve", "\taddl $16, %esp",
               "\ttestl %eax, %eax", "\tjz .Lx86_bridge_unresolved",
               "\tmovl %eax, %edx", "\tmovl -12(%ebp), %eax",
               "\tmovl -8(%ebp), %ecx", "\tmovl -4(%ebp), %ebx",
               "\tmovl %ebp, %esp", "\tpopl %ebp", "\tjmp *%edx",
               ".Lx86_bridge_unresolved:", "\tmovl 136(%ebx), %eax",
               "\tandl $-16, %esp", "\tsubl $8, %esp", "\tpushl %eax",
               "\tpushl %ebx", "\tcall reasm_indirect_unresolved", "\tud2",
               "\t.size x86_bridge_common, .-x86_bridge_common", ""]
    for idx, (sym, kind, nargs, pops, tgt, var) in enumerate(entries):
        asm += ["\t.global %s" % sym, "\t.type %s, %%function" % sym,
                "%s:\t/* %s nargs=%d pops=%d%s%s */"
                % (sym, kind, nargs, pops, (" -> " + tgt) if tgt else "",
                   " variadic" if var else ""),
                ("\tldr\tr1, =%d" % idx if a.arch == "arm" else "\tmovl $%d, %%eax" % idx),
                ("\tb\tarm_bridge_common" if a.arch == "arm" else "\tjmp x86_bridge_common"),
                "\t.size %s, . - %s" % (sym, sym), ""]
    asm += (["\t.ltorg", ""] if a.arch == "arm" else ["\t.balign 4", ""])
    open(a.out, "w", encoding="utf-8").write("\n".join(asm))

    # ---- C table --------------------------------------------------------------------------
    if a.table_out:
        c = ["/* GENERATED by runtime/arm/gen_bridge_stubs.py — do not edit. */",
             '#include "bridge_arm.h"',
             '#include "cpu_llasm.h"', ""]
        for sym, kind, nargs, pops, tgt, var in entries:
            if kind == "SHIM":
                c.append("extern void %s(void);          /* renamed C shim */" % tgt)
            elif kind == "HAND":
                c.append("extern void %s(_cpu *cpu);" % tgt)
        c.append("")
        c.append("unsigned (*arm_shim_caller(unsigned nargs))(void *, _cpu *);")
        c.append("unsigned (*arm_shim_caller_var(void))(void *, _cpu *);")
        c.append("")
        c.append("const ArmBridgeEntry g_bridge[] = {")
        for sym, kind, nargs, pops, tgt, var in entries:
            if kind == "HAND":
                c.append('    { "%s", ARM_BRIDGE_HAND, %d, %d, %d, (void *) %s, 0 },'
                         % (sym, nargs, pops, var, tgt))
            elif kind == "SHIM":
                # `call` resta 0: il caller per arità si risolve a runtime (una chiamata a
                # funzione non e' un'espressione costante in C)
                c.append('    { "%s", ARM_BRIDGE_SHIM, %d, %d, %d, (void *) &%s, 0 },'
                         % (sym, nargs, pops, var, tgt))
            else:
                c.append('    { "%s", ARM_BRIDGE_UNK, 0, 0, 0, 0, 0 },' % sym)
        c.append("};")
        c.append("const unsigned g_bridge_count = %d;" % len(entries))
        c.append("")
        open(a.table_out, "w", encoding="utf-8").write("\n".join(c) + "\n")

    # ---- name -> stub address (only real implementations) ---------------------------------
    if a.csym_out:
        c = ["/* GENERATED by runtime/arm/gen_bridge_stubs.py — do not edit.",
             " * Address of every *implemented* bridge stub, so GetProcAddress resolves by name on",
             " * ARM (on x86 the IAT slot already held the address). UNK entries are deliberately",
             " * absent: a lookup must fail loudly instead of handing out a trap address. */",
             '#include "elf_support_arm.h"', ""]
        for sym, kind, nargs, pops, tgt, var in entries:
            if kind != "UNK":
                c.append("extern void %s(void);" % sym)
        c.append("")
        c.append("const ArmBridgeSymbol g_bridge_symbols[] = {")
        for sym, kind, nargs, pops, tgt, var in entries:
            if kind != "UNK":
                c.append('    { "%s", (void *) &%s },' % (sym, sym))
        n_impl = sum(1 for e in entries if e[1] != "UNK")
        c.append("};")
        c.append("const unsigned g_bridge_symbols_n = %d;" % n_impl)
        c.append("")
        open(a.csym_out, "w", encoding="utf-8").write("\n".join(c) + "\n")

    kinds = {}
    for e in entries:
        kinds[e[1]] = kinds.get(e[1], 0) + 1
    print("gen_bridge_stubs: %d stub (%s) -> %s" % (len(entries),
          ", ".join("%s %d" % kv for kv in sorted(kinds.items())), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
