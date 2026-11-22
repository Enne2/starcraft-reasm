/* runtime/arm/x87_crt_arm.c — the MSVC CRT floating-point helper the lifted code calls.
 *
 * `__CIpow` is the chip-independent `pow` entry of the MSVC x87 CRT: a naked routine that takes
 * both operands from the x87 register stack and leaves the result there (C prototype
 * `void _CIpow(void)`). The lifter emitted it as an external guest procedure:
 *
 *     declare hidden fastcc void @__CIpow(%_cpu*)      ; 6 real call sites, all in storm.dll
 *     ... x87_fld_double ... ; push <return EIP> ; musttail @__CIpow
 *
 * Contract used here (evidence, plus the one point that cannot be confirmed offline):
 *   - two operands on the emulated stack, base in st(0) and exponent in st(1): the lifted call
 *     sites load the exponent first and the base last, which is the order the MSVC CRT's own
 *     `pow` uses before `call _CIpow`;
 *   - net effect = pop one slot and leave the result as the new st(0). Storing into ST(1) and then
 *     advancing `_st_top` is exactly the convention of the vendored helpers (`x87_faddp_st` does
 *     `ST(num) += ST0; POP_REGS;` — see llasm_float.c), so the result lands in the slot that the
 *     advanced top points at.
 *   [?] The operand order is not confirmable against a real msvcrt.dll (none in this workspace).
 *   If a math path ever computes the wrong power, swapping base/exp is the single line to change.
 *
 * `__fpmath` — the CRT's shared transcendental entry (sin/cos/tan/log/... chosen by an opcode the
 * caller pushes) — is *not* implemented: StarCraft.exe only stores its address in the CRT function
 * table (verified: one R_ARM_ABS32 relocation, no call site). It stays a declared UNK trampoline
 * in bridge_table.def: a valid address, and a loud named stop if anything ever calls it.
 */
#include "cpu_llasm.h"

#include <math.h>
#include <stdint.h>

#define ST(i) cpu->_st[((int) cpu->_st_top + (i)) & 7]

void __CIpow(_cpu *cpu);
void __CIpow(_cpu *cpu) {
    double base = ST(0);
    double exp = ST(1);

    ST(1) = pow(base, exp);
    cpu->_st_top = (cpu->_st_top + 1u) & 7u;   /* net pop of one operand */
    cpu->_st_sw_cond = 0;                      /* CLEAR_X87_FLAGS, as in llasm_float.c */
}
