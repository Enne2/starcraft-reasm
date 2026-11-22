#define _GNU_SOURCE
/* runtime/arm/cpu_llasm.c — CPU construction, guest stack and the call/bridge ABI on ARMv6.
 *
 * Design constraints, all observed in the emitted IR and in the compiled object (see
 * cpu_llasm.h for the citations):
 *  - every guest `ret` is `POP tmp1 ; tcall tmp1`, i.e. a *branch* through a guest VA. For a
 *    `proc X global G` the `ccc` wrapper pushes `__return_procedure` itself, so the runtime pushes
 *    nothing and the guest stack returns to its starting value (measured in the smoke test);
 *  - an imported function must remove the guest return EIP plus its stdcall arguments from the
 *    guest stack and then **branch** to the EIP — a plain C return would leave one host frame per
 *    guest->import edge and a hot GetTickCount() would eat the host stack;
 *  - `llasm_float.c` hands the guest the *numeric* address of `_st_result` (PTR2REG), so the
 *    `_cpu` object must live in guest-addressable memory below 0x80000000.
 */
#include "cpu_llasm.h"
#include "game_memory_arm.h"
#include "indirect_dispatch.h"
#include "x86_helpers.h"
#include "bridge_arm.h"

#include <setjmp.h>
#include <signal.h>
#include <ucontext.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>


/* Compile-time proof that the C view matches the IR's `%_cpu`. If this ever fails, the
 * emitter changed and the runtime must follow — better a build error than wrong offsets. */
_Static_assert(sizeof(uint32_t) == 4, "u32");
_Static_assert(offsetof(_cpu, _edx) == 8, "%_cpu field index 2 must be at 8");
_Static_assert(offsetof(_cpu, _esp) == 16, "%_cpu field index 4 (esp) must be at 16");
_Static_assert(offsetof(_cpu, _eflags) == 32, "%_cpu field index 8 must be at 32");
_Static_assert(offsetof(_cpu, _st_cw) == 44, "%_cpu field index 11 must be at 44");
_Static_assert(offsetof(_cpu, _st) == 48, "%_cpu st[8] must start at 48 (IR: index 12)");

static __thread jmp_buf g_stop;
static __thread int g_in_guest;

static void put_hex(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char b[10];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; ++i) b[2 + i] = hx[(v >> (28 - 4 * i)) & 0xF];
    (void) write(2, b, sizeof b);
}

static void cpu_log(const char *s) {
    if (s) (void) write(2, s, strlen(s));
}

_cpu *cpu_llasm_create(uint32_t stack_size) {
    _cpu *cpu;
    if (arm_arena_init() != 0) return 0;
    cpu = (_cpu *) arm_alloc_low((uint32_t) sizeof(_cpu), 8);
    if (!cpu) return 0;
    memset(cpu, 0, sizeof(_cpu));
    if (cpu_llasm_init(cpu, stack_size) != 0) return 0;
    return cpu;
}

int cpu_llasm_init_at(_cpu *cpu, uint32_t stack_size, uint32_t base);
int cpu_llasm_init(_cpu *cpu, uint32_t stack_size) {
    return cpu_llasm_init_at(cpu, stack_size, ARM_STACK_BASE);
}

/* `base` esplicita: ogni thread guest ha il proprio stack emulato in un intervallo distinto
 * (ARM_STACK_BASE + k*ARM_STACK_MAX). */
int cpu_llasm_init_at(_cpu *cpu, uint32_t stack_size, uint32_t base) {
    void *stk;
    void *ind_cache;

    if (!cpu) return -1;
    if (!stack_size) stack_size = 0x00100000u;               /* 1 MiB */
    if (stack_size > ARM_STACK_MAX) stack_size = ARM_STACK_MAX;
    stack_size = (stack_size + 0xFFFu) & ~0xFFFu;

    stk = (void *) (unsigned long) base;
    if (arm_map_at(base, stack_size) != 0) return -1;
    ind_cache = calloc(4096u, 8u);
    if (!ind_cache) return -1;

    memset(cpu, 0, sizeof(_cpu));
    cpu->_ind_cache = ind_cache;
    cpu->_eflags = CPU_EFLAGS_DEFAULT;
    cpu->_st_cw = CPU_ST_CW_DEFAULT;
    cpu->_st_top = 0;
    cpu->_st_sw_cond = 0;
    cpu->stack_bottom = stk;
    cpu->stack_top = (void *) (unsigned long) (base + stack_size);

    /* x86 pushes downwards; start 64 bytes below the top and keep esp 16-byte aligned, which
     * the AVX-era CRT and the shims' SSE-free float code both assume at call boundaries. */
    cpu->_esp = (uint32_t) (base + stack_size - 64u) & ~15u;

    X86_InterruptFlag = (cpu->_eflags & 0x0200u) ? 1u : 0u;
    if (arm_teb_init() != 0) return -1;
    arm_teb_set_stack((uint32_t) (uintptr_t) cpu->stack_top, (uint32_t) (uintptr_t) cpu->stack_bottom);
    return 0;
}

void cpu_llasm_destroy(_cpu *cpu) {
    if (!cpu) return;
    free(cpu->_ind_cache);
    cpu->_ind_cache = 0;
}

void cpu_llasm_push32(_cpu *cpu, uint32_t v) {
    cpu->_esp -= 4;
    cpu_llasm_write32(cpu->_esp, v);
}

uint32_t cpu_llasm_pop32(_cpu *cpu) {
    uint32_t v = cpu_llasm_read32(cpu->_esp);
    cpu->_esp += 4;
    return v;
}

uint32_t cpu_llasm_arg32(_cpu *cpu, unsigned index) {
    return cpu_llasm_read32(cpu->_esp + 4u * index);
}

uint32_t cpu_llasm_read32(uint32_t va) {
    uint32_t v;
    if (!arm_va_is_mapped(va, 4)) {
        cpu_log("[arm-cpu] lettura guest fuori mappa a ");
        put_hex(va);
        cpu_log("\n");
        return 0;
    }
    memcpy(&v, (const void *) (unsigned long) va, 4);
    return v;
}

void cpu_llasm_write32(uint32_t va, uint32_t v) {
    if (!arm_va_is_mapped(va, 4)) {
        cpu_log("[arm-cpu] scrittura guest fuori mappa a ");
        put_hex(va);
        cpu_log("\n");
        return;
    }
    memcpy((void *) (unsigned long) va, &v, 4);
}

void arm_guest_sentinel(_cpu *cpu) {
    (void) cpu;
    /* Reached when the guest finally executes its `ret`: the guest stack held our synthetic
     * return address, so control comes here instead of off the end of the world. */
    longjmp(g_stop, 1);
}

/* How the guest actually returns (verified on the generated object code of a `global` proc):
 *     ldr r1, [r0, #esp_off]
 *     ldr r2, =__return_procedure
 *     sub r3, r1, #4 ; str r3, [r0, #esp_off] ; str r2, [r1, #-4]
 *     b  X                      <-- tail branch, lr still points into this function
 * `X` ends with `POP tmp1 ; tcall tmp1`, which loads that value and branches to
 * `__return_procedure` = `ret void` = `bx lr`. So the guest comes back here through the *normal*
 * C return of the wrapper and the guest stack is left exactly as it was. That is why
 * cpu_llasm_run() pushes nothing.
 *
 * A bare `proc X public` body (no wrapper) has no return address to consume; cpu_llasm_run_raw()
 * pushes a sentinel instead, and the routine's outermost `ret` longjmps back here.
 */
static _cpu *g_cpu_for_signal;          /* solo diagnostica (signal handler, globale) */

/* `_cpu` corrente DEL THREAD. Era un global unico: con due thread tradotti gli shim avrebbero
 * letto gli argomenti dallo stack sbagliato (work/link/REPORT-flash-heap.md §5). */
static __thread _cpu *g_cpu_current;

void cpu_llasm_set_current(_cpu *cpu);
void cpu_llasm_set_current(_cpu *cpu) { g_cpu_current = cpu; }

/* Guest code ranges, set by the game main (from the generated layout). Used only by the crash
 * handler to walk the emulated stack and print a plausible call chain: a fault inside translated
 * code otherwise reports a single address and nothing about who called it. */
static uint32_t g_code_lo[2], g_code_hi[2];

void cpu_llasm_set_code_ranges(uint32_t lo0, uint32_t hi0, uint32_t lo1, uint32_t hi1);
void cpu_llasm_set_code_ranges(uint32_t lo0, uint32_t hi0, uint32_t lo1, uint32_t hi1) {
    g_code_lo[0] = lo0; g_code_hi[0] = hi0;
    g_code_lo[1] = lo1; g_code_hi[1] = hi1;
}

int cpu_llasm_run(_cpu *cpu, void (*entry)(_cpu *)) {
    if (!cpu || !entry) return -1;
    if (g_in_guest) {
        cpu_log("[arm-cpu] cpu_llasm_run annidato: non supportato\n");
        return -1;
    }
    g_in_guest = 1;
    g_cpu_for_signal = cpu;
    g_cpu_current = cpu;
    entry(cpu);
    g_cpu_current = 0;
    g_in_guest = 0;
    return 0;
}

int cpu_llasm_run_raw(_cpu *cpu, void (*entry)(_cpu *)) {
    if (!cpu || !entry) return -1;
    if (g_in_guest) {
        cpu_log("[arm-cpu] cpu_llasm_run_raw annidato: non supportato\n");
        return -1;
    }
    g_in_guest = 1;
    g_cpu_for_signal = cpu;
    g_cpu_current = cpu;
    if (setjmp(g_stop) == 0) {
        cpu_llasm_push32(cpu, (uint32_t) (uintptr_t) &arm_guest_sentinel);
        entry(cpu);
        g_in_guest = 0;
        return 1;              /* returned without consuming the sentinel: unbalanced stack */
    }
    g_in_guest = 0;
    return 0;                  /* sentinel reached: the guest executed its final ret */
}

/* ---- crash diagnostics ------------------------------------------------------------------- */

static void cpu_signal(int sig, siginfo_t *si, void *uctx) {
    _cpu *cpu = g_cpu_for_signal;
    cpu_log(sig == SIGSEGV ? "[arm-cpu] SIGSEGV\n" : "[arm-cpu] SIGBUS\n");
    /* The faulting instruction is in the ucontext (ARM: mcontext.arm_pc); the value at [esp] is
     * only the *return* address of the guest, i.e. where the caller would resume. Both are
     * printed because they answer different questions ("what faulted" vs "who called it"). */
    if (uctx) {
        ucontext_t *uc = (ucontext_t *) uctx;
#if defined(__i386__)
        uint32_t pc = (uint32_t) uc->uc_mcontext.gregs[REG_EIP];
        uint32_t lr = 0; /* x86 has no link register */
#elif defined(__arm__)
        uint32_t pc = (uint32_t) uc->uc_mcontext.arm_pc;
        uint32_t lr = (uint32_t) uc->uc_mcontext.arm_lr;
#else
#error unsupported runtime architecture
#endif
        cpu_log("[arm-cpu] fault pc ");
        put_hex(pc);
        cpu_log("  lr ");
        put_hex(lr);
        cpu_log("\n");
        /* Quale procedura TRADOTTA contiene il pc? La tabella della dispatch e' ordinata per VA,
         * non per indirizzo host: scansione lineare (siamo sul percorso di crash) cercando la voce
         * piu' alta che non supera il pc. Rende il log autoesplicativo: senza, serve addr2line. */
        {
            /* tabelle della dispatch (indirect_dispatch.h) */
            uint32_t i, best = 0, bestva = 0;
            for (i = 0; i < reasm_va_map_n; ++i) {
                uint32_t fn = (uint32_t) reasm_va_fn[i];
                if (fn <= pc && (best == 0 || fn > best)) { best = fn; bestva = reasm_va_va[i]; }
            }
            if (best) {
                cpu_log("[arm-cpu] procedura tradotta: entry VA ");
                put_hex(bestva);
                cpu_log("  offset nel proc ");
                put_hex(pc - best);
                cpu_log("\n");
            }
        }
    }
    if (si) {
        cpu_log("[arm-cpu] fault address ");
        put_hex((uint32_t) (uintptr_t) si->si_addr);
        cpu_log("\n");
    }
    if (cpu) {
        int i, shown = 0;
        cpu_log("[arm-cpu] catena sullo stack del guest (indirizzi di codice):\n");
        for (i = 0; i < 512 && shown < 10; ++i) {
            uint32_t va = cpu->_esp + (uint32_t) i * 4u;
            uint32_t v;
            if (cpu->stack_top && va + 4 > (uint32_t) (uintptr_t) cpu->stack_top) break;
            v = cpu_llasm_read32(va);
            if ((v >= g_code_lo[0] && v < g_code_hi[0]) ||
                (v >= g_code_lo[1] && v < g_code_hi[1])) {
                cpu_log("   +"); put_hex(va - cpu->_esp);
                cpu_log(" "); put_hex(v); cpu_log("\n");
                ++shown;
            }
        }
        cpu_log("[arm-cpu] guest eip ");
        put_hex(cpu_llasm_read32(cpu->_esp));
        cpu_log("  esp ");
        put_hex(cpu->_esp);
        cpu_log("  eax ");
        put_hex(cpu->_eax);
        cpu_log("  edx ");
        put_hex(cpu->_edx);
        cpu_log("\n");
    }
    _exit(9);
}

_cpu *cpu_llasm_current(void) { return g_cpu_current ? g_cpu_current : g_cpu_for_signal; }

int cpu_llasm_install_crash_handler(_cpu *cpu) {
    struct sigaction sa;
    g_cpu_for_signal = cpu;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = cpu_signal;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, 0) != 0) return -1;
    if (sigaction(SIGBUS, &sa, 0) != 0) return -1;
    return 0;
}
