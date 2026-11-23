/* runtime/arm/guest_cpu_helpers.c — implementation of the seams declared in guest_cpu_helpers.h.
 *
 * Everything i386-specific that the sc-reasm shims did with inline asm now goes through these
 * functions, so the C shims themselves stay portable:
 *   int $0x80 getdents64/statfs   -> glibc calls in the shims (see patches/0001-arm-port.patch)
 *   movl %fs:0x34 / movl %0,%fs:0x34 -> x86_read_fs_dword / x86_write_fs_dword (LLASM helpers)
 *   %fs based TEB                 -> an emulated TEB page in low guest memory
 */
#include "guest_cpu_helpers.h"
#include "game_memory.h"

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

extern void _exit(int);

/* X86_InterruptFlag: vendor/sr/SR/llasm-support/llasm_pushx.c only *declares* it
 * (`EXTERNCVAR` expands to `extern` in C and in C++), so the platform layer must define it —
 * exactly like SR's own game support code does. The emitted IR references it as an i8 global;
 * on little-endian ARM the low byte of this u32 is the byte the IR reads and writes (STI/CLI).
 * It is written by x86_popfd() in llasm_pushx.c and by our cpu_llasm_init(). */
uint32_t X86_InterruptFlag;

static uint32_t g_teb_va;
static uint32_t g_peb_va;

static void die(const char *what, uint32_t v) {
    arm_mem_log("[arm-x86] STOP: ");
    arm_mem_log(what);
    arm_mem_log_hex("", v);
    _exit(3);
}

static uint32_t *teb_slot(uint32_t off) {
    if (!g_teb_va || off + 4 > TEB_PAGE_SIZE) die("offset TEB non valido", off);
    return (uint32_t *) (unsigned long) (g_teb_va + off);
}

int arm_teb_init(void) {
    uint32_t *teb;
    uint32_t *peb;
    if (g_teb_va) return 0;
    teb = (uint32_t *) arm_alloc_low(TEB_PAGE_SIZE, 8);
    peb = (uint32_t *) arm_alloc_low(TEB_PAGE_SIZE, 8);
    if (!teb || !peb) return -1;
    g_teb_va = (uint32_t) (uintptr_t) teb;
    g_peb_va = (uint32_t) (uintptr_t) peb;

    teb[TEB_OFF_SELF / 4] = g_teb_va;
    teb[TEB_OFF_PEB / 4] = g_peb_va;
    teb[TEB_OFF_LAST_ERROR / 4] = 0;
    teb[TEB_OFF_STACK_ALLOC / 4] = 0;
    /* PEB + 0x10 = ProcessParameters: the MSVC CRT reads it while building argv/env
     * (see the R6009 note in reasm-toolkit/runtime/elf/teb.c). The translated StarCraft CRT does
     * not take those paths through the shims, but the field must point at readable memory: a null
     * there is a hard crash, a zeroed 64-byte block is not. */
    {
        void *pp = arm_alloc_low(64, 8);
        if (!pp) return -1;
        peb[0x10 / 4] = (uint32_t) (uintptr_t) pp;
    }
    return 0;
}

void arm_teb_set_stack(uint32_t base, uint32_t limit) {
    if (!g_teb_va) return;
    *teb_slot(TEB_OFF_STACK_BASE) = base;
    *teb_slot(TEB_OFF_STACK_LIMIT) = limit;
}

uint32_t arm_teb_base(void) { return g_teb_va; }
uint32_t arm_peb_base(void) { return g_peb_va; }

uint32_t X86_ReadFsDword(uint32_t address) {
    if (!g_teb_va) {
        if (arm_teb_init() != 0) die("TEB non inizializzabile", 0);
    }
    return *teb_slot(address);
}

void X86_WriteFsDword(uint32_t address, uint32_t value) {
    if (!g_teb_va) {
        if (arm_teb_init() != 0) die("TEB non inizializzabile", 0);
    }
    *teb_slot(address) = value;
}

uint32_t X86_ReadMemProcedure(uint32_t address, uint32_t memsize) {
    if (!arm_va_is_mapped(address, memsize)) die("lettura fuori mappa a", address);
    switch (memsize) {
        case 1: return *(const volatile uint8_t *) (unsigned long) address;
        case 2: { uint16_t v; memcpy(&v, (const void *) (unsigned long) address, 2); return v; }
        case 4: { uint32_t v; memcpy(&v, (const void *) (unsigned long) address, 4); return v; }
        default: die("dimensione lettura non valida", memsize); return 0;
    }
}

void X86_WriteMemProcedure(uint32_t address, uint32_t memsize, uint32_t eax) {
    if (!arm_va_is_mapped(address, memsize)) die("scrittura fuori mappa a", address);
    switch (memsize) {
        case 1: *(volatile uint8_t *) (unsigned long) address = (uint8_t) eax; break;
        case 2: { uint16_t v = (uint16_t) eax; memcpy((void *) (unsigned long) address, &v, 2); break; }
        case 4: { uint32_t v = eax; memcpy((void *) (unsigned long) address, &v, 4); break; }
        default: die("dimensione scrittura non valida", memsize);
    }
}

uint32_t X86_InPortProcedure(uint16_t port, uint32_t port_size) {
    (void) port_size;
    die("IN da porta I/O (non esiste su ARM), porta", port);
    return 0;
}

void X86_OutPortProcedure(uint16_t port, uint32_t port_size, uint32_t eax) {
    (void) port_size; (void) eax;
    die("OUT su porta I/O (non esiste su ARM), porta", port);
}

void X86_InterruptProcedure(uint8_t intno, void *regs) {
    (void) regs;
    /* int 0x2E/0x2F were the Win32 syscall gates; 0x29 = absolute disk I/O; 0x03 = breakpoint.
     * None of them can be honoured here. A silent return would corrupt guest state further
     * down (rule D19 of the toolkit: never a stub that returns). */
    die("int x86 non emulabile, vettore", intno);
}
