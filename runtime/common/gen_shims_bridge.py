#!/usr/bin/env python3
"""runtime/arm/gen_shims_bridge.py — build the guest-ABI layer for the sc-reasm Win32 shims.

Why this exists
---------------
The lifted game objects call Win32 APIs as *guest* functions: arguments on the emulated x86 stack,
return value in eax, callee pops (stdcall). The sc-reasm shims implement the same APIs as ordinary
C functions (AAPCS on ARM: r0-r3 + host stack, caller/callee rules of the C prototype). Something
must bridge the two, and it must know, per API, **how many stdcall words the callee has to pop** —
on i386 that number was implicit in the `ret N` emitted by `__attribute__((stdcall))`; on ARM the
attribute is ignored (clang warns and drops it), so it has to be materialised here.

Oracle for the arity: the shim sources themselves. Every API has exactly one prototype there, and
clang can be asked for it (`-Xclang -ast-dump`). That is the same declaration the C compiler uses
when it builds the function, so the two can never disagree. Nothing is guessed: a symbol with no
prototype and no explicit UNK declaration aborts the generation.

What it emits
-------------
  1. `<out-def>`      SHIM(...) / UNK(...) lines, consumed by gen_bridge_stubs.py, which owns the
                      single stub table and the ARM gate.
  2. `<out-rename>`   pairs `<sym> <sym>__c` for `objcopy --redefine-syms`: the C shim keeps its
                      code but releases the import name to the guest-ABI thunk. No source is
                      modified — the rename happens on the build-tree copy of the objects.
  3. `<out-impl>`     the per-arity callers (`unsigned call3(void *fn, _cpu *cpu)`) that read the
                      arguments from the guest stack and call the C function with the right AAPCS
                      shape. The C compiler emits the register/stack split, so no hand-written
                      assembly is involved and arity > 4 works for free.

Variadics: `wsprintfA` is cdecl in the shim (`int wsprintfA(char*, const char*, ...)`), so the
guest pops its own arguments. The caller passes the two named arguments plus a fixed window of 8
extra words; that is enough for every `wsprintf` format the game uses and reads only words the
guest already pushed.
"""
import argparse
import re
import subprocess
import sys

FUNC_RE = re.compile(r"FunctionDecl[^']*?\b(\w+) '([^']*)'")
MAX_ARITY = 16
VAR_WINDOW = 8

UNK_HINT = """
Symbols that appear in the game objects but have neither a shim prototype nor a declared
UNK entry. Add them to bridge_table.def as UNK(<name>) after checking that there is really no
implementation available (rule D19 of the project: never a stub that silently returns).
"""


def parse_sig(sig):
    """'S u32 (void)' -> (nargs, stdcall, variadic, ret)."""
    lp = sig.find('(')
    if lp < 0:
        return None
    ret = sig[:lp].strip()
    stdcall = ret.startswith("S ") or "__attribute__((stdcall))" in sig
    params = sig[lp + 1:sig.rindex(')')].strip()
    variadic = params.endswith("...")
    core = params.replace("...", "").strip().rstrip(",").strip()
    if core in ("", "void"):
        n = 0
    else:
        n = 1 + core.count(",")
    return n, stdcall, variadic, ret


CONFLICTS = []


def collect_ast(shim_dir, files, clang_flags):
    proto = {}
    for f in files:
        src = f if f.endswith(".c") else f + ".c"
        cmd = ["clang"] + clang_flags + ["-Xclang", "-ast-dump", "-fsyntax-only",
                                        "%s/%s" % (shim_dir, src)]
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        if r.returncode != 0 or "FunctionDecl" not in r.stdout:
            sys.stderr.write("gen_shims_bridge: AST dump fallito per %s\n%s\n"
                             % (src, r.stderr[-2000:]))
            return {}
        out = r.stdout
        for line in out.splitlines():
            if "FunctionDecl" not in line:
                continue
            m = FUNC_RE.search(line)
            if not m:
                continue
            name, sig = m.group(1), m.group(2)
            rec = parse_sig(sig)
            if rec is None:
                continue
            if name in proto:
                old = proto[name]
                if (old[0], old[2]) != (rec[0], rec[2]):
                    raise SystemExit("arity/variadic conflict for %s: %r vs %r" % (name, old, rec))
                if old[1] != rec[1]:
                    # Same signature, declared once with and once without the stdcall marker.
                    # The *definition* in these sources always carries `S`, and the guest ABI
                    # needs that (callee-pop) answer.
                    CONFLICTS.append(name)
                    rec = (rec[0], True, rec[2], rec[3])
            proto[name] = rec
    return proto


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--need", required=True, help="symbols referenced by the game objects")
    ap.add_argument("--defined", required=True, help="symbols already defined (objects+runtime)")
    ap.add_argument("--shim-dir", required=True)
    ap.add_argument("--shims", required=True, help="comma separated shim source basenames")
    ap.add_argument("--clang-flags", default="", help="flags for the AST dump, space separated")
    ap.add_argument("--extra-def", default="", help="def file with explicit UNK(...) declarations")
    ap.add_argument("--dynamic", default="", help="def file DYNAMIC(...): API risolte via GetProcAddress")
    ap.add_argument("--hand-def", default="",
                    help="bridge_table.def: le voci ENTRY() sono gia' coperte a mano")
    ap.add_argument("--out-def", required=True)
    ap.add_argument("--out-rename", required=True)
    ap.add_argument("--out-impl", required=True)
    a = ap.parse_args()

    need = [s.strip() for s in open(a.need) if s.strip()]
    defined = set(s.strip() for s in open(a.defined) if s.strip())
    hand = set()
    if a.hand_def:
        for line in open(a.hand_def):
            m = re.match(r"\s*ENTRY\(\s*(\w+)\s*,", line)
            if m:
                hand.add(m.group(1))
    targets = sorted(set(need) - defined)

    unk = set()
    if a.extra_def:
        for line in open(a.extra_def):
            m = re.match(r"\s*UNK\(\s*(\w+)\s*\)", line)
            if m:
                unk.add(m.group(1))

    proto = collect_ast(a.shim_dir, [s for s in a.shims.split(",") if s],
                        [f for f in a.clang_flags.split() if f])

    # API che il gioco risolve a runtime (LoadLibrary + GetProcAddress): non sono nei riferimenti
    # statici ma devono avere comunque uno stub con ABI del guest.
    dynamic = []
    if a.dynamic:
        for line in open(a.dynamic):
            m = re.match(r"\s*DYNAMIC\(\s*(\w+)\s*\)", line)
            if m:
                dynamic.append(m.group(1))

    # Una voce scritta a mano copre il simbolo solo se NON esiste uno shim C con prototipo: se lo
    # shim c'e', vince lo shim (stessa semantica del runtime i386) e la voce a mano viene scartata
    # da gen_bridge_stubs.py (--only-needed + precedenza SHIM).
    targets = [t for t in targets if not (t in hand and t not in proto)]

    shims, traps, missing = [], [], []
    for sym in targets + [d for d in dynamic if d not in targets]:
        if sym in proto:
            n, stdcall, variadic, _ret = proto[sym]
            shims.append((sym, n, stdcall, variadic))
        elif sym in unk:
            traps.append(sym)
        else:
            missing.append(sym)

    dyn_missing = [d for d in dynamic if d not in proto]
    if dyn_missing:
        sys.stderr.write("gen_shims_bridge: DYNAMIC senza prototipo negli shim: %s\n"
                         % " ".join(dyn_missing))
        return 2

    if missing:
        sys.stderr.write("gen_shims_bridge: %d simboli senza arità e senza UNK: %s\n"
                         % (len(missing), " ".join(missing[:40])))
        sys.stderr.write(UNK_HINT)
        return 2

    # ---- 1) def file consumed by gen_bridge_stubs.py -------------------------------------
    lines = ["/* GENERATED by runtime/arm/gen_shims_bridge.py — do not edit.",
             " * SHIM(<symbol>, <nargs>, <esp words to pop>): C shim reached through the guest ABI.",
             " * UNK(<symbol>): no implementation, declared trampoline that logs and stops. */",
             ""]
    for sym, n, stdcall, variadic in shims:
        pops = 0 if (variadic and not stdcall) else n
        lines.append("SHIM(%s, %d, %d, %d)%s" % (sym, n, pops, 1 if variadic else 0,
                                                  "   /* variadic/cdecl: the guest pops */"
                                                  if variadic else ""))
    for sym in traps:
        lines.append("UNK(%s)" % sym)
    open(a.out_def, "w").write("\n".join(lines) + "\n")

    # ---- 2) rename list for objcopy -------------------------------------------------------
    ren = ["# GENERATED by runtime/arm/gen_shims_bridge.py — objcopy --redefine-syms input"]
    for sym, n, stdcall, variadic in shims:
        ren.append("%s %s__c" % (sym, sym))
    open(a.out_rename, "w").write("\n".join(ren) + "\n")

    # ---- 3) per-arity callers -------------------------------------------------------------
    impl = ["/* GENERATED by runtime/arm/gen_shims_bridge.py — do not edit.",
            " *",
            " * One caller per arity: it reads the stdcall arguments from the emulated x86 stack",
            " * ([esp+4*i], the return EIP has already been popped) and calls the C shim with the",
            " * AAPCS shape the C compiler chose for that prototype. Register arguments land in",
            " * r0-r3 and the rest on the host stack automatically — no hand-written assembly.",
            " */",
            '#include "_cpu"'.replace('"_cpu"', '"cpu_llasm.h"'),
            "",
            "typedef unsigned arm_u32;",
            ""]
    used = sorted(set((n, v) for (_s, n, _st, v) in shims if not v))
    for n, _v in used:
        params = ", ".join("arm_u32" for _ in range(n)) if n else "void"
        args = ", ".join("cpu_llasm_arg32(cpu, %d)" % i for i in range(n))
        impl.append("static unsigned call%d(void *fn, _cpu *cpu) {" % n)
        impl.append("    return ((unsigned (*)(%s)) fn)(%s);" % (params, args))
        impl.append("}")
        impl.append("")
    if any(v for (_s, _n, _st, v) in shims):
        extra = ", ".join("arm_u32" for _ in range(VAR_WINDOW))
        args = ", ".join("cpu_llasm_arg32(cpu, %d)" % i for i in range(2 + VAR_WINDOW))
        impl.append("/* variadic (cdecl): 2 named arguments + a read-only window of %d words */"
                    % VAR_WINDOW)
        impl.append("static unsigned call_var(void *fn, _cpu *cpu) {")
        impl.append("    return ((unsigned (*)(arm_u32, arm_u32, %s)) fn)(%s);" % (extra, args))
        impl.append("}")
        impl.append("")
    impl.append("/* arity -> caller; index by entry->nargs, VAR_ARG for the variadic entries. */")
    impl.append("unsigned (*arm_shim_caller(unsigned nargs))(void *, _cpu *);")
    impl.append("unsigned (*arm_shim_caller(unsigned nargs))(void *, _cpu *) {")
    impl.append("    switch (nargs) {")
    for n in range(MAX_ARITY + 1):
        if any(n == u for (u, _v) in used):
            impl.append("        case %d: return call%d;" % (n, n))
    impl.append("        default: return 0;")
    impl.append("    }")
    impl.append("}")
    impl.append("")
    impl.append("unsigned (*arm_shim_caller_var(void))(void *, _cpu *) {")
    impl.append("    return call_var;" if any(v for (_s, _n, _st, v) in shims) else "    return 0;")
    impl.append("}")
    impl.append("")
    open(a.out_impl, "w").write("\n".join(impl) + "\n")

    if CONFLICTS:
        print("gen_shims_bridge: stdcall sia con sia senza attributo, prevale stdcall: %s"
              % ", ".join(sorted(set(CONFLICTS))))
    print("gen_shims_bridge: %d SHIM, %d UNK (%d richiesti, %d già definiti)"
          % (len(shims), len(traps), len(targets), len(defined)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
