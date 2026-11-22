/* runtime/arm/x86_helpers.h — platform seams the LLASM reference helpers call into.
 *
 * Evidence for the exact set of symbols (from the emitted IR in build/slice and the .llinc
 * declarations in vendor/sr/SR/llasm-support):
 *
 *   X86_ReadFsDword / X86_WriteFsDword      <- llasm_fs_mem.c  (fs:[off] load/store)
 *   X86_InPortProcedure / X86_OutPortProcedure <- llasm_inout.c
 *   X86_ReadMemProcedure / X86_WriteMemProcedure <- llasm_movmem.c
 *   X86_InterruptProcedure                  <- llasm_int.c (x86_int)
 *   X86_InterruptFlag (variable, u32)       <- llasm_pushx.c / CLI/STI in llasm_xti.llinc
 *
 * On i386 the first group is `%fs:` + `int $0x80`; here it is an explicit emulated TEB and
 * direct memory access. Nothing in this file uses inline asm, %fs, or i386 syscall numbers.
 */
#ifndef ARM_X86_HELPERS_H
#define ARM_X86_HELPERS_H

#include <stdint.h>

/* ---- TEB / PEB (the fs: segment) ----
 * Layout used (identical to the 32-bit Windows TEB, as already relied upon by the toolkit:
 * ~/dev/reasm-toolkit/runtime/elf/teb.c):
 *   0x00 SEH chain, 0x04 StackBase, 0x08 StackLimit, 0x18 Self, 0x20 ClientId,
 *   0x28 ActiveRpcHandle, 0x2C ThreadLocalStoragePointer, 0x30 ProcessEnvironmentBlock,
 *   0x34 LastErrorValue.
 * The TEB page is real guest memory (< 0x80000000) so that translated code that takes the
 * address of fs:[0x30] (PEB) can dereference it exactly as on Windows. */
#define TEB_PAGE_SIZE 0x1000u
#define TEB_OFF_STACK_BASE  0x04u
#define TEB_OFF_STACK_LIMIT 0x08u
#define TEB_OFF_SELF        0x18u
#define TEB_OFF_STACK_ALLOC 0x20u
#define TEB_OFF_PEB         0x30u
#define TEB_OFF_LAST_ERROR  0x34u

int  arm_teb_init(void);                       /* idempotent; 0 on success */
void arm_teb_set_stack(uint32_t base, uint32_t limit);
uint32_t arm_teb_base(void);                   /* guest VA of the TEB, 0 if not initialised */
uint32_t arm_peb_base(void);

/* The two functions the LLASM fs helper calls. Offsets outside the TEB page stop the process
 * with a diagnostic instead of returning a plausible-looking zero (rule: never a silent stub).
 * Calling convention: SR's CCALL is empty on every target except i386/Windows-MSVC, i.e. the
 * plain AAPCS call on ARM — no attribute needed (and none is silently dropped). */
uint32_t X86_ReadFsDword(uint32_t address);
void     X86_WriteFsDword(uint32_t address, uint32_t value);

/* ---- memory procedures (SRW self-modifying/dynamic accesses) ---- */
uint32_t X86_ReadMemProcedure(uint32_t address, uint32_t memsize);
void     X86_WriteMemProcedure(uint32_t address, uint32_t memsize, uint32_t eax);

/* ---- I/O ports: no such thing on ARM; a diagnostic stop, never a fake zero ---- */
uint32_t X86_InPortProcedure(uint16_t port, uint32_t port_size);
void     X86_OutPortProcedure(uint16_t port, uint32_t port_size, uint32_t eax);

/* ---- software interrupts (int 0x2E/0x2F/0x3/0x29/... used by Win32/CRT code) ---- */
void X86_InterruptProcedure(uint8_t intno, void *regs);

/* Set by POPFD/STI/CLI; mirrors llasm_pushx.c's definition site. */
extern uint32_t X86_InterruptFlag;

#endif /* ARM_X86_HELPERS_H */
