/* runtime/arm/cpu_llasm.h — lifecycle and call/bridge ABI of the LLASM guest CPU on ARMv6.
 *
 * ORACLE (binary/ABI evidence, not guesswork):
 *   - `%_cpu` layout: emitted verbatim by `llasm` (vendor/sr/llasm/llasm.d:5001):
 *         %_cpu = type { i32 x12, [8 x double], i64 }
 *     i.e. eax ecx edx ebx esp ebp esi edi eflags st_top st_sw_cond st_cw, st[8], st_result.
 *     The C side (vendor/sr/SR/llasm-support/llasm_cpu.h) places st[] at offset 48 and the
 *     i64 field right after it; `llasm_float.c` only ever *returns the address* of that i64
 *     (PTR2REG(&st_result)) to the guest, so both layouts agree on every field the IR indexes.
 *   - guest entries: `proc X global G` in .llasm yields two symbols in the IR —
 *     `protected fastcc void @X(%_cpu*)` (body) and `protected ccc void @G(%_cpu*)` (wrapper
 *     that pushes `__return_procedure` on the guest stack and calls the body). The runtime
 *     therefore calls the **ccc wrapper** `G`.
 *   - imports: `proc S external` yields `declare hidden fastcc void @S(%_cpu*)`. SRW/LLASM
 *     turns every x86 `call` into `push <return EIP>` + `tcall`/`musttail`, so a guest callee
 *     *never returns on the host stack*: it must pop the guest return EIP and branch to it.
 *     That is the whole bridge contract implemented here.
 *
 * The instruction-set helpers (div/idiv, string ops, x87) are NOT re-derived here: the
 * architecture-neutral reference implementations shipped by SR are compiled verbatim
 * (vendor/sr/SR/llasm-support/llasm_*.c, 133 helper functions). This file/layer supplies what
 * the emitter expects *around* them: CPU construction, guest stack, guest memory access,
 * TEB/PEB (guest_cpu_helpers.c), fixed-VA mapping (game_memory.c) and the import bridge
 * (bridge.c).
 */
#ifndef ARM_CPU_LLASM_H
#define ARM_CPU_LLASM_H

#include <stddef.h>
#include <stdint.h>

/* Mirrors llasm_cpu.h. Kept in sync by the static assertions below. */
typedef struct _cpu {
    uint32_t _eax, _ecx, _edx, _ebx, _esp, _ebp, _esi, _edi, _eflags;
    uint32_t _st_top, _st_sw_cond, _st_cw;
    double   _st[8];
    uint64_t _pointer_offset;   /* unused on 32-bit ARM (PTROFS_64BIT undefined) */
    int64_t  _st_result;
    void    *stack_bottom, *stack_top;
    /* Stato dell'arco indiretto, PER THREAD (offsets 136/140, cfr. %_cpu nel .ll):
     *  - _ind_target: il target che il codice tradotto scrive subito prima di `musttail
     *    call @reasm_indirect`. Era un global (`reasm_indirect_target`) ed e' diventato un campo
     *    di _cpu perche' con due thread tradotti il global veniva sovrascritto fra lo `store`
     *    e la `ldr` dentro reasm_indirect (work/link/REPORT-flash-heap.md §5).
     *  - _ind_esp: ESP emulato prima dell'arco (solo diagnostica del percorso di STOP). */
    uint32_t _ind_target;
    uint32_t _ind_esp;
    void    *_ind_cache;   /* private 4096-entry indirect cache; host-allocated */
} _cpu;

/* Il tipo `%_cpu` nel .ll e questo struct devono coincidere: tools/dispatch-ir.py allunga la
 * dichiarazione emessa da llasm fino a questi due campi finali. Gli offset sono verificati qui. */
_Static_assert(offsetof(_cpu, _ind_target) == 136, "_cpu._ind_target deve stare a 136 (%_cpu indice 17)");
_Static_assert(offsetof(_cpu, _ind_esp) == 140, "_cpu._ind_esp deve stare a 140 (%_cpu indice 18)");
_Static_assert(offsetof(_cpu, _ind_cache) == 144, "_cpu._ind_cache deve stare a 144");
_Static_assert(offsetof(_cpu, _st) == 48, "_cpu._st deve stare a 48 (contratto llasm)");

/* Field indices as used by the emitted IR (getelementptr %_cpu, i32 0, i32 N). */
enum {
    CPU_EAX = 0, CPU_ECX = 1, CPU_EDX = 2, CPU_EBX = 3, CPU_ESP = 4, CPU_EBP = 5,
    CPU_ESI = 6, CPU_EDI = 7, CPU_EFLAGS = 8, CPU_ST_TOP = 9, CPU_ST_SW_COND = 10,
    CPU_ST_CW = 11, CPU_ST = 12
};

/* Initial x87 control word / eflags. 0x037F = x87 default (all exceptions masked, 64-bit
 * precision, round-to-nearest); 0x202 = IF set + bit 1 (reserved, always 1 on x86). */
#define CPU_ST_CW_DEFAULT  0x037Fu
#define CPU_EFLAGS_DEFAULT 0x00000202u

/* A `_cpu` is always allocated inside the low (< 0x80000000) arena: `llasm_float.c` hands the
 * guest the *numeric address* of `_st_result` (PTR2REG) and the guest dereferences it. */
_cpu *cpu_llasm_create(uint32_t stack_size);
void cpu_llasm_destroy(_cpu *cpu);

/* Guest stack: real memory at the guest VA, inside the low arena. `cpu->esp` is a guest VA.
 * `cpu_llasm_init` reserves the stack, sets esp to the top (16-byte aligned) and leaves the
 * x87 stack empty. */
int  cpu_llasm_init(_cpu *cpu, uint32_t stack_size);

/* Variante con base esplicita dello stack emulato: serve ai thread guest, ognuno con il proprio
 * intervallo (ARM_STACK_BASE + k*ARM_STACK_MAX), perche' `cpu_llasm_init` mappa sempre allo
 * stesso VA. */
int  cpu_llasm_init_at(_cpu *cpu, uint32_t stack_size, uint32_t base);

/* Imposta il `_cpu` corrente DEL THREAD (TLS): lo chiama il runtime quando fa girare codice
 * tradotto su un thread diverso da quello principale. */
void cpu_llasm_set_current(_cpu *cpu);

void     cpu_llasm_push32(_cpu *cpu, uint32_t v);
uint32_t cpu_llasm_pop32(_cpu *cpu);
uint32_t cpu_llasm_arg32(_cpu *cpu, unsigned index);   /* [esp + 4*index], 0 = first arg */
uint32_t cpu_llasm_read32(uint32_t va);
void     cpu_llasm_write32(uint32_t va, uint32_t v);

/* Run a guest entry that was declared `proc X global G`: pass the **ccc wrapper** `G`
 * (symbol `G`, the `c_*` name SRW/llasm emits). The wrapper installs the guest return address
 * (`__return_procedure`) itself, so the runtime pushes nothing and the guest stack returns to
 * its starting value. Returns 0 on a completed run, non-zero on API misuse. Re-entry happens
 * only through the import bridge, which tail-branches; nested runs are rejected. */
int cpu_llasm_run(_cpu *cpu, void (*entry)(_cpu *));

/* Run a bare guest routine (`proc X public`, a fastcc body with no wrapper): the runtime pushes
 * the sentinel return address, so the routine's outermost `ret` comes back through longjmp.
 * Returns 0 if the sentinel was reached, 1 if the routine returned without consuming it
 * (which means its stack was unbalanced). Used by the test to prove the sentinel path. */
int cpu_llasm_run_raw(_cpu *cpu, void (*entry)(_cpu *));

/* Sentinel target used as the initial guest return address by cpu_llasm_run_raw. */
void arm_guest_sentinel(_cpu *cpu);

/* The _cpu of the run in progress (0 before the first cpu_llasm_run/_raw). Used by the shim
 * callback layer, which cannot take a cpu parameter through the Win32 prototypes. */
_cpu *cpu_llasm_current(void);

/* Guest code ranges, for the crash handler's stack walk (call `with` the values from the
 * generated layout: the two code regions of the translated images). */
void cpu_llasm_set_code_ranges(uint32_t lo0, uint32_t hi0, uint32_t lo1, uint32_t hi1);

/* Diagnostic: install a SIGSEGV/SIGBUS handler that prints the guest esp/eax/edx and the address
 * that faulted. A crash inside translated code otherwise looks like "qemu: signal 11" with no
 * clue; this is the ARM equivalent of the x86 runtime's stack dump. Returns 0 on success. */
int cpu_llasm_install_crash_handler(_cpu *cpu);

/* Import bridge: called by the generated ARM stubs (bridge_stubs.S). Pops the guest return EIP
 * and the declared number of stdcall arguments, runs the implementation, returns the guest VA
 * to branch to. Never returns to its caller by normal means; the stub does the branch so the
 * host stack stays at entry level (no frame per guest->import edge). */
uint32_t arm_bridge_dispatch(_cpu *cpu, uint32_t index);

#endif /* ARM_CPU_LLASM_H */
