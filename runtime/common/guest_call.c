#define _GNU_SOURCE
/* runtime/arm/guest_call.c — calling *guest* procedures from C shims (the callback direction).
 *
 * The sc-reasm shims were written for the i386 ELF runtime, where the game's code *is* native code
 * at its VA: `win32_ui.c` calls a window procedure with
 *      int (*p)(unsigned,unsigned,unsigned,unsigned) = (void *)w->wndproc; p(hwnd,msg,wp,lp);
 * In the LLASM model a guest procedure is a translation that takes exactly one argument, the
 * `%_cpu`, reads its arguments from the emulated x86 stack and returns by *branching* to the VA it
 * popped. A direct call would execute with garbage — which is why the ARM port routes every
 * callback through this file (see patches/0002-guest-callbacks.patch).
 *
 * Mechanism: push the arguments right-to-left on the guest stack, push a sentinel return address,
 * jump into the guest and let the sentinel `longjmp` back. longjmp restores the host stack, so the
 * frames that the guest left behind (it returns by branching, never by `pop {..,pc}`) are
 * reclaimed, and nested call-outs (shim -> guest -> shim -> guest) are bounded by the guest's own
 * recursion, not by a host stack leak.
 *
 * This is legal re-entry: `cpu_llasm_run` refuses nesting, this call-out is the *sanctioned*
 * nesting point, and it saves/restores esp around the call so a mis-declared arity cannot
 * accumulate.
 */
#include "cpu_llasm.h"
#include "game_memory.h"
#include "indirect_dispatch.h"
#include "thread_affinity.h"

#include <setjmp.h>
#include <stdint.h>
#include <unistd.h>

#if defined(__i386__)
/* Lifted code uses fastcc (%_cpu* in ECX), not the C cdecl function-pointer ABI. */
extern void x86_call_guest_fast(_cpu *cpu, void (*entry)(_cpu *));
static void guest_invoke(_cpu *cpu, void *entry) {
    x86_call_guest_fast(cpu, (void (*)(_cpu *)) entry);
}
#elif defined(__arm__)
static void guest_invoke(_cpu *cpu, void *entry) {
    ((void (*)(_cpu *)) entry)(cpu);
}
#else
#error unsupported runtime architecture
#endif

#define MAX_ARGS 12

#define MAX_DEPTH 16

/* One jmp_buf per nesting level: a WndProc may call SendMessage, which calls into the guest
 * again (modal dialogs do exactly that). LIFO, like the host stack. */
static __thread jmp_buf g_jb[MAX_DEPTH];   /* per thread: un thread guest ha il suo */
static __thread int g_depth;

void arm_guest_call_sentinel(_cpu *cpu);
void arm_guest_call_sentinel(_cpu *cpu) {
    (void) cpu;
    if (g_depth <= 0) _exit(3);
    longjmp(g_jb[g_depth - 1], 1);
}

/* Shared implementation. Returns the guest's eax, or 0 when the call could not be made. */
static int guest_call_at(_cpu *cpu, void *entry, unsigned nargs, const uint32_t *args, int *status) {
    uint32_t saved_esp;
    unsigned i;
    if (status) *status = 0;
    if (!cpu || !entry || nargs > MAX_ARGS) {
        if (status) *status = -1;
        return 0;
    }
    if (g_depth >= MAX_DEPTH) {
        if (status) *status = -3;
        return 0;
    }
    saved_esp = cpu->_esp;
    if (setjmp(g_jb[g_depth]) == 0) {
        g_depth++;
        for (i = nargs; i > 0; --i) cpu_llasm_push32(cpu, args[i - 1]);
        cpu_llasm_push32(cpu, (uint32_t) (uintptr_t) &arm_guest_call_sentinel);
        guest_invoke(cpu, entry);
        /* Reaching here means the procedure was a `c_*` wrapper that returned normally. */
        g_depth--;
    } else {
        g_depth--;                 /* the sentinel fired: the guest executed its final ret */
    }
    {
        int r = (int) cpu->_eax;
        cpu->_esp = saved_esp;
        return r;
    }
}

/* Callback pointers may be guest VAs (e.g. a pointer read from original PE data) OR host
 * addresses of translated entries (the linker-resolved form emitted for relocations in lifted
 * data). Resolve both through the same gate used by translated indirect branches. */
static int guest_call(_cpu *cpu, uint32_t proc, unsigned nargs, const uint32_t *args, int *status) {
    void *entry;
    /* reasm_indirect_resolve first maps exact guest VAs, then accepts host addresses only inside
     * [reasm_text_lo,reasm_text_hi). Do not pre-reject with arm_va_is_mapped(): that excludes
     * valid relocated callback pointers such as ptrtoint(@loc_4D1D70) from lifted .data. */
    entry = reasm_indirect_resolve(proc);
    if (!entry) {
        if (status) *status = -4;
        return 0;
    }
    return guest_call_at(cpu, entry, nargs, args, status);
}

/* --- thread guest: CreateThread --------------------------------------------------------------
 * Un thread Win32 creato dal gioco riceve `fn(arg)`, dove `fn` e' un ingresso *tradotto*
 * (indirizzo host dentro il .text del port) con ABI `fastcc void(%_cpu*)`: chiamarlo con l'ABI C
 * fa dereferenziare l'argomento come `_cpu` (misurato: SIGSEGV a 0x188A0408, arg=0 -> fault 0x10;
 * work/link/REPORT-flash-heap.md §4). E non si puo' nemmeno eseguirlo in modo sincrono dentro
 * CreateThread: il corpo di storm aspetta un evento che segnala il chiamante -> deadlock
 * (misurato). Quindi: thread host vero, con un `_cpu` e uno stack emulato PROPRI.
 *
 * Cosa e' per-thread oggi: `_cpu._ind_target` (target della dispatch), `cpu_llasm_current()`
 * (TLS), `g_jb`/`g_depth` (TLS), lo stack emulato (init_at con base dedicata). Resta condiviso
 * il TEB emulato (fs: fisso): `LastError` e' unico fra i thread, come documentato nel report.
 */
#include <pthread.h>

typedef struct {
    _cpu *cpu;
    uint32_t entry;
    uint32_t arg;
} arm_thread_job;

static void *arm_thread_main(void *p) {
    arm_thread_job *job = (arm_thread_job *) p;
    (void) sc_affinity_apply(1);
    int st = 0;
    cpu_llasm_set_current(job->cpu);
    {
        uint32_t args[1];
        args[0] = job->arg;
        (void) guest_call_at(job->cpu, (void *) (unsigned long) job->entry, 1, args, &st);
    }
    cpu_llasm_set_current(0);
    return 0;
}

/* Crea il thread: `_cpu`, stack emulato e job sono allocati QUI (thread chiamante), cosi'
 * l'allocatore dell'arena e le mmap restano single-threaded. */
extern char reasm_text_lo[], reasm_text_hi[];

#define TH_SLOTS 16
typedef struct { uint32_t h; pthread_t th; int live; } arm_th_slot;
static arm_th_slot g_ths[TH_SLOTS];
static unsigned g_th_serial;
static uint32_t g_stack_next;

uint32_t arm_guest_thread_start(uint32_t fn, uint32_t arg);
int      arm_guest_thread_join(uint32_t handle);     /* 1 = era nostro (join fatto), 0 = no */

static uint32_t thread_spawn(uint32_t fn, uint32_t arg) {
    uint32_t entry = fn;
    uint32_t base;
    _cpu *cpu;
    arm_thread_job *job;
    pthread_t th;
    unsigned slot = TH_SLOTS;
    if (!(fn >= (uint32_t) (unsigned long) reasm_text_lo &&
          fn < (uint32_t) (unsigned long) reasm_text_hi)) {
        void *e = reasm_indirect_resolve(fn);            /* VA guest: passa dalla mappa */
        if (!e) return -4;
        entry = (uint32_t) (unsigned long) e;
    }
    if (g_stack_next >= 4u) return 0;                   /* 4 stack: 0x02000000-0x03000000 */
    base = ARM_STACK_BASE + g_stack_next * ARM_STACK_MAX;
    g_stack_next++;
    {
        unsigned i;
        for (i = 0; i < TH_SLOTS; ++i) if (!g_ths[i].live) { slot = i; break; }
        if (slot == TH_SLOTS) return 0;                 /* tabella piena: rifiuto dichiarato */
    }
    cpu = (_cpu *) arm_alloc_low((uint32_t) sizeof(_cpu), 8);
    if (!cpu) return 0;
    if (cpu_llasm_init_at(cpu, 0x00080000u, base) != 0) return 0;    /* 512 KiB per thread */
    job = (arm_thread_job *) arm_alloc_low((uint32_t) sizeof(arm_thread_job), 8);
    if (!job) return 0;
    job->cpu = cpu;
    job->entry = entry;
    job->arg = arg;
    if (pthread_create(&th, 0, arm_thread_main, job) != 0) return 0;
    g_th_serial = (g_th_serial + 1u) & 0xFFFu;
    g_ths[slot].h = 0x72000000u + g_th_serial;
    g_ths[slot].th = th;
    g_ths[slot].live = 1;
    return g_ths[slot].h;
}

uint32_t arm_guest_thread_start(uint32_t fn, uint32_t arg) { return thread_spawn(fn, arg); }

int arm_guest_thread_join(uint32_t handle) {
    unsigned i;
    for (i = 0; i < TH_SLOTS; ++i) {
        if (g_ths[i].live && g_ths[i].h == handle) {
            (void) pthread_join(g_ths[i].th, 0);
            g_ths[i].live = 0;
            return 1;
        }
    }
    return 0;
}

int arm_guest_call4(uint32_t proc, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
int arm_guest_call4(uint32_t proc, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t args[4];
    args[0] = a0; args[1] = a1; args[2] = a2; args[3] = a3;
    return guest_call(cpu_llasm_current(), proc, 4, args, 0);
}

/* Generic form, for callbacks with other arities (EnumWindows, timer procedures, dialog procs). */
int arm_guest_calln(uint32_t proc, unsigned nargs, const uint32_t *args, int *status);
int arm_guest_calln(uint32_t proc, unsigned nargs, const uint32_t *args, int *status) {
    return guest_call(cpu_llasm_current(), proc, nargs, args, status);
}

/* Call a translated fastcc entry directly from a C shim. Unlike resolving its guest VA,
 * this can safely target an original entry that the dispatcher itself has replaced. The
 * guest_call_at frame (jmp_buf, depth, and saved emulated ESP) is stack/TLS-nested. */
int arm_guest_call_fn(void *entry_fastcc, unsigned nargs, const uint32_t *args, int *status);
int arm_guest_call_fn(void *entry_fastcc, unsigned nargs, const uint32_t *args, int *status) {
    return guest_call_at(cpu_llasm_current(), entry_fastcc, nargs, args, status);
}
