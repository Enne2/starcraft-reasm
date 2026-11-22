/* runtime/arm/indirect_dispatch.h — resolution of indirect guest code addresses on ARMv6.
 *
 * ORACLE (why this exists, not a guess):
 *   - `vendor/sr/llasm/llasm.d` (case "tcall", ~line 2962) turns an x86 `call reg` / `jmp reg`
 *     into `musttail call fastcc void inttoptr(i32 %reg)`: the *value* in the register is used
 *     as a host function pointer. That is exact on i386, where the translated code sits at the
 *     original guest VAs; on ARM the translated code is elsewhere, so a guest code address read
 *     from the image (function tables, vtables, `_initterm` arrays) is not callable.
 *   - Measured consequence: the CRT's initialiser table (guest .data VA 0x0050C4C0) holds
 *     0x004060C1, 0x00406AA1, 0x0040D8C6, 0x004F8329; `bx` on the first one switched the CPU to
 *     Thumb (bit0 of an *odd x86 address*, not a tag) and executed the port's own ARM text at
 *     the numerically equal address → SIGILL. Log: work/link/REPORT-flash-runtime.md.
 *
 * Contract: translated code stores the target in `_cpu._ind_target` (offset 136, written by
 * tools/dispatch-ir.py) and tail-calls `reasm_indirect(%_cpu*)` (the emitter's musttail ABI
 * requires identical signatures, so the value cannot be a second argument). Il campo e' dentro
 * `_cpu`: PER THREAD, cosi' due thread tradotti non si sovrascrivono il target. The assembly entry resolves the value and *branches*: no
 * host frame is added to the guest call chain, exactly like the bridge stubs.
 */
#ifndef ARM_INDIRECT_DISPATCH_H
#define ARM_INDIRECT_DISPATCH_H

#include <stdint.h>

/* Il target non ha piu' un global dedicato: e' `_cpu._ind_target` (vedi cpu_llasm.h), scritto
 * dal codice tradotto subito prima della tail call e letto una volta da reasm_indirect. */

/* One entry per translated procedure: guest VA -> host entry, as two parallel sorted `hidden`
 * constant arrays emitted by tools/dispatch-ir.py from the `loc_<VA>` definitions of the IR.
 * (Two arrays and not an array of structs: LLVM 22's parser rejects literal struct types inside
 * an array initialiser; verified with llvm-as. The C side pays nothing: same 92839 entries.) */
extern const uint32_t  reasm_va_va[];
extern void *const     reasm_va_fn[];
extern const uint32_t  reasm_va_map_n;

/* Captures the window of the port's own code, from the linker symbols
 * `__executable_start` / `etext`: a value inside it is one of our own addresses (a return
 * label pushed by the emitter, or a bridge stub), not a guest VA. */
void reasm_dispatch_init(void);

/* Called from the assembly entry; returns the host entry to branch to, or 0 when the value is
 * neither a translated guest VA nor one of our own code addresses. */
void *reasm_indirect_resolve(uint32_t target);

/* Never returns: prints the target, the caller (reverse-mapped through the table) and the guest
 * CPU state, then exits with status 71. A declared stop, never a silent stub. */
void reasm_indirect_unresolved(void *cpu, uint32_t target);

#endif /* ARM_INDIRECT_DISPATCH_H */
