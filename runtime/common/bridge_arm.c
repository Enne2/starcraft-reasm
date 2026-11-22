/* runtime/arm/bridge_arm.c — implementations of the Win32 imports in bridge_table.def.
 *
 * Every import has the guest calling convention: on entry r0 = %_cpu; arguments are on the
 * guest stack at [esp + 4*i] (the return EIP has already been popped by arm_bridge_common_c).
 * The result goes in cpu->_eax (cpu->_edx for a 64-bit result), exactly as the x86 callee would
 * have left it. Argument counts are the callee-pop counts from the table — a mismatch shifts
 * esp and breaks the next guest `ret`, so the table is the single source of truth.
 *
 * These bodies deliberately reuse the *semantics* already validated by the x86 shims
 * (sc-reasm/tools/elf/win32_shim.c) while keeping their own, ARM-portable implementation:
 * no %fs, no int 0x80, no inline asm.
 */
#include "bridge_arm.h"
#include "cpu_llasm.h"
#include "x86_helpers.h"
#include "game_memory_arm.h"

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
extern long atol(const char *);
#include <time.h>
#include <pthread.h>

extern int usleep(unsigned int us);
extern void _exit(int);

static uint32_t ms_now(void) {
    struct timespec ts = { 0, 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t) (ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ---- error state (emulated fs:0x34) ---- */
void imp_GetLastError(_cpu *cpu)  { cpu->_eax = X86_ReadFsDword(TEB_OFF_LAST_ERROR); }
void imp_SetLastError(_cpu *cpu)  { X86_WriteFsDword(TEB_OFF_LAST_ERROR, cpu_llasm_arg32(cpu, 0)); }

/* ---- time ---- */
void imp_GetTickCount(_cpu *cpu)  { cpu->_eax = ms_now(); }
void imp_Sleep(_cpu *cpu) {
    uint32_t ms = cpu_llasm_arg32(cpu, 0);
    if (ms) (void) usleep(ms * 1000u);
}

/* ---- thread guest: CreateThread / WaitForSingleObject ---------------------------------
 * ORACLE/contratto: un thread Win32 creato dal gioco riceve `fn(arg)`, e `fn` qui e' un ingresso
 * *tradotto*: indirizzo host dentro il .text del port (quello che la `PUSH loc_<VA>` del lift mette
 * come parametro) con ABI `fastcc void(%_cpu*)`, NON l'ABI C. Lo shim del progetto lo chiama con
 * l'ABI C -> il primo `ldr r4,[r0,#0x10]` dereferenzia l'argomento come `_cpu`
 * (misurato: SIGSEGV a 0x188A0408, arg=0 -> fault 0x10; work/link/REPORT-flash-heap.md §4).
 * Eseguirlo in modo sincrono qui dentro non e' un'alternativa: il corpo di storm aspetta un evento
 * che segnala il chiamante -> deadlock misurato (§4).
 * Quindi il port crea un thread host vero con `_cpu` e stack emulato propri
 * (runtime/arm/guest_call_arm.c, arm_guest_thread_start).
 */
#define TH_HANDLE_BASE 0x72000000u

uint32_t arm_guest_thread_start(uint32_t fn, uint32_t arg);
int      arm_guest_thread_join(uint32_t handle);

void imp_CreateThread(_cpu *cpu) {
    uint32_t fn = cpu_llasm_arg32(cpu, 2), arg = cpu_llasm_arg32(cpu, 3);
    uint32_t idp = cpu_llasm_arg32(cpu, 5);
    uint32_t h = arm_guest_thread_start(fn, arg);
    if (!h) {
        (void) write(2, "[arm-bridge] CreateThread non avviato (ingresso non traducibile o "
                        "risorse esaurite)\n", 76);
        cpu->_eax = 0;
        return;
    }
    if (idp && arm_va_is_mapped(idp, 4)) cpu_llasm_write32(idp, h);
    cpu->_eax = h;
}

/* Gli handle che non sono nostri (eventi, finestre, ...) restano allo shim del progetto.
 * Sul binario i386 lo shim è stdcall (ret $8): dichiararlo cdecl faceva pulire due volte
 * gli argomenti e corrompeva lo stack host del worker MPQ. Il wrapper ARM usa l'AAPCS invariato. */
#if defined(__i386__)
#define SHIM_STDCALL __attribute__((stdcall))
#else
#define SHIM_STDCALL
#endif
extern unsigned SHIM_STDCALL WaitForSingleObject__c(void *h, unsigned ms);

void imp_WaitForSingleObject(_cpu *cpu) {
    uint32_t h = cpu_llasm_arg32(cpu, 0), ms = cpu_llasm_arg32(cpu, 1);
    if ((h & 0xFFFFF000u) == TH_HANDLE_BASE && arm_guest_thread_join(h)) {
        cpu->_eax = 0;                                  /* WAIT_OBJECT_0 */
        return;
    }
    cpu->_eax = WaitForSingleObject__c((void *) (unsigned long) h, ms);
}

/* ---- DialogProc: trampolino C -> guest -------------------------------------------------------
 * ORACLE/contratto: il DialogProc del gioco e' un ingresso *tradotto* (ABI `fastcc void(%_cpu*)`),
 * non una funzione C. Lo shim del progetto lo chiama con l'ABI C da dlg_elf_wndproc
 * (`p(h,msg,wp,lp)`), e il primo `ldrd r2,r3,[r0,#16]` del proc dereferenzia l'hwnd come `_cpu`:
 * misurato SIGBUS con fault 0x00010011 e `procedura tradotta: entry VA 0x00420980`, subito dopo
 * la creazione della finestra (work/link/REPORT-flash-heap.md §9.1). Gli oggetti degli shim
 * precompilati NON contengono patches/0002-guest-callbacks, quindi la correzione si fa sul
 * confine che il port possiede: DialogBoxParamA e' un HAND, il proc lo vede il port per primo e
 * allo shim passa un trampolino C-ABI che invoca il proc con arm_guest_call4 (che accetta sia un
 * VA guest sia un ingresso host e salva/ripristina l'esp del chiamante).
 * Slot LIFO: i dialoghi sono modali; un dialogo annidato prende lo slot successivo e il thunk
 * ricorda il proprio proc (l'hwnd non e' noto quando il dialogo viene creato).
 */
#define DLG_SLOTS 8
static uint32_t g_dlg_proc[DLG_SLOTS];

extern int arm_guest_call4(uint32_t proc, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

#define DLG_THUNK(i)                                                                \
    static int dlg_thunk_##i(unsigned h, unsigned m, unsigned w, unsigned l) {       \
        return arm_guest_call4(g_dlg_proc[i], h, m, w, l);                          \
    }
DLG_THUNK(0) DLG_THUNK(1) DLG_THUNK(2) DLG_THUNK(3)
DLG_THUNK(4) DLG_THUNK(5) DLG_THUNK(6) DLG_THUNK(7)

typedef int (*dlg_thunk_fn)(unsigned, unsigned, unsigned, unsigned);
static const dlg_thunk_fn k_dlg_thunks[DLG_SLOTS] = {
    dlg_thunk_0, dlg_thunk_1, dlg_thunk_2, dlg_thunk_3,
    dlg_thunk_4, dlg_thunk_5, dlg_thunk_6, dlg_thunk_7
};

extern int SHIM_STDCALL DialogBoxParamA__c(void *inst, const char *name, unsigned owner,
                                           dlg_thunk_fn proc, unsigned param);

void imp_DialogBoxParamA(_cpu *cpu) {
    void *inst = (void *) (unsigned long) cpu_llasm_arg32(cpu, 0);
    const char *name = (const char *) (unsigned long) cpu_llasm_arg32(cpu, 1);
    unsigned owner = cpu_llasm_arg32(cpu, 2);
    uint32_t proc = cpu_llasm_arg32(cpu, 3);
    unsigned param = cpu_llasm_arg32(cpu, 4);
    int slot = -1, i, r;
    for (i = 0; i < DLG_SLOTS; ++i) if (!g_dlg_proc[i]) { slot = i; break; }
    if (slot < 0 || !proc) {
        /* Nessuno slot libero o nessun proc: si passa allo shim senza trampolino. E' il
         * comportamento precedente ed e' un limite dichiarato (il proc verrebbe chiamato con
         * l'ABI C): meglio un dialogo senza callback che un fault silenzioso. */
        (void) write(2, "[arm-bridge] DialogBoxParamA senza slot per il trampolino\n", 57);
        r = DialogBoxParamA__c(inst, name, owner,
                               (dlg_thunk_fn) (unsigned long) proc, param);
        cpu->_eax = (uint32_t) r;
        return;
    }
    g_dlg_proc[slot] = proc;
    r = DialogBoxParamA__c(inst, name, owner, k_dlg_thunks[slot], param);
    g_dlg_proc[slot] = 0;
    cpu->_eax = (uint32_t) r;
}

/* ---- identity ---- */
void imp_GetCurrentThreadId(_cpu *cpu)  { cpu->_eax = (uint32_t) (uintptr_t) pthread_self(); }
void imp_GetCurrentProcessId(_cpu *cpu) { cpu->_eax = (uint32_t) getpid(); }
void imp_IsDebuggerPresent(_cpu *cpu)   { cpu->_eax = 0; }
void imp_ExitProcess(_cpu *cpu) {
    uint32_t code = cpu_llasm_arg32(cpu, 0);
    (void) write(2, "[arm-bridge] ExitProcess\n", 25);
    _exit((int) code);
}

/* ---- modules: the executable image is restored at its original base ---- */
void imp_GetModuleHandleA(_cpu *cpu) {
    uint32_t name = cpu_llasm_arg32(cpu, 0);
    if (!name) { cpu->_eax = 0x00400000u; return; }        /* NULL = the executable itself */
    cpu->_eax = arm_va_is_mapped(name, 1) ? 0x00400000u : 0;
}

/* ---- TLS index space (distinct from the emulated fs: segment) ---- */
#define ARM_TLS_SLOTS 64
static void *g_tls[ARM_TLS_SLOTS];
static uint32_t g_tls_next;

void imp_TlsAlloc(_cpu *cpu) {
    if (g_tls_next >= ARM_TLS_SLOTS) { cpu->_eax = 0xFFFFFFFFu; X86_WriteFsDword(TEB_OFF_LAST_ERROR, 0x1Bu); return; }
    g_tls[g_tls_next] = 0;
    cpu->_eax = g_tls_next++;
}
void imp_TlsFree(_cpu *cpu) {
    uint32_t i = cpu_llasm_arg32(cpu, 0);
    if (i < ARM_TLS_SLOTS) g_tls[i] = 0;
    cpu->_eax = 1;
}
void imp_TlsGetValue(_cpu *cpu) {
    uint32_t i = cpu_llasm_arg32(cpu, 0);
    cpu->_eax = (i < ARM_TLS_SLOTS) ? (uint32_t) (uintptr_t) g_tls[i] : 0;
}
void imp_TlsSetValue(_cpu *cpu) {
    uint32_t i = cpu_llasm_arg32(cpu, 0);
    uint32_t v = cpu_llasm_arg32(cpu, 1);
    if (i < ARM_TLS_SLOTS) g_tls[i] = (void *) (uintptr_t) v;
    cpu->_eax = (i < ARM_TLS_SLOTS) ? 1 : 0;
}

/* ---- diagnostics ---- */
void imp_OutputDebugStringA(_cpu *cpu) {
    uint32_t va = cpu_llasm_arg32(cpu, 0);
    if (va && arm_va_is_mapped(va, 1)) {
        const char *s = (const char *) (unsigned long) va;
        (void) write(2, s, strlen(s));       /* the guest string is our address space too */
    }
    cpu->_eax = 0;
}

/* ---- performance counter: writes a 64-bit value through the out pointer ---- */
void imp_QueryPerformanceCounter(_cpu *cpu) {
    uint32_t out = cpu_llasm_arg32(cpu, 0);
    struct timespec ts = { 0, 0 };
    if (out && arm_va_is_mapped(out, 8)) {
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { ts.tv_sec = 0; ts.tv_nsec = 0; }
        cpu_llasm_write32(out, (uint32_t) ts.tv_sec);
        cpu_llasm_write32(out + 4, (uint32_t) ts.tv_nsec);
        cpu->_eax = 1;
    } else {
        cpu->_eax = 0;
    }
}

/* ---- MSVC CRT: dispatcher delle trascendenti (__fpmath) ----
 * Non implementato: la CRT tradotta chiama __fpmath(opcode) — misurato: opcode 0x1 durante
 * l'inizializzazione, risultato scartato — ma la tabella degli opcode vive in msvcrt.dll, che non
 * e' disponibile. Il simbolo e' definito a 0 (guest_abs_zero.S) cosi' la CRT usa il proprio ramo
 * x87 tradotto (loc_404E60). Vedi work/runtime-arm/REPORT.md, sezione sul boot.
 */

/* ---- test-only probe: result = arg*2 + 1, also records what it saw ---- */
static uint32_t g_probe_seen;
void imp_armtest_probe(_cpu *cpu) {
    uint32_t v = cpu_llasm_arg32(cpu, 0);
    g_probe_seen = v;
    cpu->_eax = v * 2u + 1u;
}

/* ---- storm.dll: ___sse2_available_init (VA 0x15040E07) ----
 * Chiamata dall'_initterm del CRT della DLL (IDA `sub_1500810F`, call a 0x1500813D) attraverso la
 * parola a VA 0x15059100 del blob dei dati, che il lift ha convertito in un riferimento diretto
 * a questo simbolo esterno (vedi il commento della ENTRY in bridge_table.def).
 *
 * Non e' uno stub: il corpo e' la semantica dell'originale (IDA 0x15040E07-0x15040ECF: azzera
 * dword_1505FC20 e dword_1505FC1C, ritorna 0 sul ramo "CPUID non disponibile", che e' quello
 * dell'ARM1176) ed e' lo stesso che serve la voce della mappa VA. L'ABI la da' lo stub generato
 * (cdecl, 0 argomenti, nessun pop del callee). */
extern void __sse2_available_init_storm_body(_cpu *cpu);
void imp_crt_unlifted_15040E07(_cpu *cpu) {
    __sse2_available_init_storm_body(cpu);
}

/* ------------------------------------------------------------------------------------------ */
/* storm.dll ordinals: translated in-process, reachable at their original VA                    */

typedef struct { uint32_t ordinal; uint32_t va; } ArmStormOrdinal;
extern const ArmStormOrdinal g_storm_ordinals[];
extern const uint32_t g_storm_ordinals_n;

uint32_t arm_bridge_ordinal_va(unsigned ordinal) {
    uint32_t lo = 0, hi = g_storm_ordinals_n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (g_storm_ordinals[mid].ordinal == (uint32_t) ordinal) return g_storm_ordinals[mid].va;
        if (g_storm_ordinals[mid].ordinal < (uint32_t) ordinal) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* dispatcher                                                                                   */

/* Per-arity marshallers (generated by gen_shims_bridge.py, present only in the game build where
 * the C shims are linked). Weak: the standalone smoke build has no SHIM entries and must still
 * link; if a SHIM entry ever existed without them the dispatcher stops by name. */
unsigned (*arm_shim_caller(unsigned nargs))(void *, _cpu *) __attribute__((weak));
unsigned (*arm_shim_caller_var(void))(void *, _cpu *) __attribute__((weak));

int arm_bridge_find(const char *name) {
    unsigned i;
    if (!name) return -1;
    for (i = 0; i < g_bridge_count; ++i)
        if (strcmp(g_bridge[i].name, name) == 0) return (int) i;
    return -1;
}

const char *arm_bridge_name(unsigned index) { return index < g_bridge_count ? g_bridge[index].name : 0; }
unsigned arm_bridge_nargs(unsigned index) { return index < g_bridge_count ? g_bridge[index].nargs : 0; }
unsigned arm_bridge_kind(unsigned index) { return index < g_bridge_count ? g_bridge[index].kind : 0xFFu; }

unsigned arm_bridge_unk_count(void) {
    unsigned i, n = 0;
    for (i = 0; i < g_bridge_count; ++i) if (g_bridge[i].kind == ARM_BRIDGE_UNK) n++;
    return n;
}

void arm_bridge_dump(void) {
    static const char *kinds[] = { "HAND", "SHIM", "UNK" };
    unsigned i;
    for (i = 0; i < g_bridge_count; ++i) {
        char num[8]; int n = 0, v = (int) g_bridge[i].nargs, k;
        char t[8];
        if (!v) t[n++] = '0';
        while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
        for (k = 0; k < n; ++k) num[k] = t[n - 1 - k];
        num[n] = 0;
        (void) write(2, "[arm-bridge] ", 13);
        (void) write(2, kinds[g_bridge[i].kind], strlen(kinds[g_bridge[i].kind]));
        (void) write(2, " ", 1);
        (void) write(2, g_bridge[i].name, strlen(g_bridge[i].name));
        (void) write(2, " args=", 6);
        (void) write(2, num, (unsigned long) n);
        (void) write(2, "\n", 1);
    }
}

/* An import with no implementation stops here, with its name and the caller: the worklist entry
 * that tells what to implement next (D19). It never returns. */
static void arm_bridge_unk_stop(const char *name, uint32_t caller) {
    (void) write(2, "[arm-bridge] API NON IMPLEMENTATA: ", 34);
    (void) write(2, name, strlen(name));
    (void) write(2, " (ritorno a ", 12);
    {
        static const char hx[] = "0123456789ABCDEF";
        char b[12];
        int i;
        b[0] = '0'; b[1] = 'x';
        for (i = 0; i < 8; ++i) b[2 + i] = hx[(caller >> (28 - 4 * i)) & 0xF];
        b[10] = '\n';
        (void) write(2, b, 11);
    }
    (void) write(2, "[arm-bridge] -> stop controllato (worklist)\n", 42);
    _exit(3);
}

/* SC_BRIDGE_TRACE=1: una riga per import chiamato (nome + return EIP del guest). E' lo strumento
 * che dice *dove* si e' fermato il gioco quando non c'e' un crash ma un comportamento inatteso;
 * limitato per non annegare il log (le prime N chiamate, poi un riepilogo). */
static int g_trace = -1;
static unsigned g_trace_n;
static unsigned g_trace_max = 4000u;   /* SC_BRIDGE_TRACE_N: alzabile per finestre di traccia tardive */

static void trace_args(_cpu *cpu, unsigned nargs) {
    static const char hx[] = "0123456789abcdef";
    char b[48];
    unsigned i, k, p = 0;
    if (g_trace <= 0) return;
    for (i = 0; i < nargs && i < 3; ++i) {
        uint32_t v = cpu_llasm_arg32(cpu, i);
        b[p++] = ' ';
        b[p++] = '0'; b[p++] = 'x';
        for (k = 0; k < 8; ++k) b[p++] = hx[(v >> (28 - 4 * k)) & 0xF];
    }
    b[p++] = '\n';
    (void) write(2, b, (unsigned long) p);
}

static void trace_import(const char *name, uint32_t caller) {
    static const char hx[] = "0123456789abcdef";
    char b[96];
    int i = 0, k;
    if (g_trace < 0) {
        const char *v = getenv("SC_BRIDGE_TRACE");
        g_trace = v ? 1 : 0;
        { const char *n = getenv("SC_BRIDGE_TRACE_N");
          if (n && *n) { long l = atol(n); if (l > 0) g_trace_max = (unsigned) l; } }
    }
    if (!g_trace) return;
    if (++g_trace_n > g_trace_max) return;
    for (k = 0; name[k] && i < 40; ++k) b[i++] = name[k];
    b[i++] = ' ';
    b[i++] = '@';
    b[i++] = ' ';
    b[i++] = '0'; b[i++] = 'x';
    for (k = 0; k < 8; ++k) b[i++] = hx[(caller >> (28 - 4 * k)) & 0xF];
    b[i++] = '\n';
    (void) write(2, b, (unsigned long) i);
}

/* Called by arm_bridge_common (bridge_stubs.S) with the index baked into the stub. */
uint32_t arm_bridge_common_c(_cpu *cpu, uint32_t index) {
    const ArmBridgeEntry *e;
    uint32_t ret;
    unsigned i;

    if (index >= g_bridge_count) {
        (void) write(2, "[arm-bridge] indice stub fuori tabella\n", 38);
        _exit(3);
    }
    e = &g_bridge[index];
    ret = cpu_llasm_pop32(cpu);            /* guest return EIP: the bridge never returns here */
    trace_import(e->name, ret);
    trace_args(cpu, e->nargs);
    switch (e->kind) {
        case ARM_BRIDGE_HAND:
            ((void (*)(_cpu *)) e->fn)(cpu);
            break;
        case ARM_BRIDGE_SHIM: {
            unsigned (*call)(void *, _cpu *) = e->call;
            if (!call) call = e->var ? arm_shim_caller_var() : arm_shim_caller(e->nargs);
            if (!call) {
                (void) write(2, "[arm-bridge] nessun caller per l'arità dichiarata: ", 50);
                (void) write(2, e->name, strlen(e->name));
                (void) write(2, "\n", 1);
                _exit(3);
            }
            cpu->_eax = call(e->fn, cpu);
            break;
        }
        default:
            arm_bridge_unk_stop(e->name, ret);
    }
    for (i = 0; i < e->pops; ++i) (void) cpu_llasm_pop32(cpu);   /* stdcall: callee pops */
    if (g_trace) {
        /* risultato: un'API che restituisce 0 dove il CRT si aspetta un handle/pointer e' la
         * causa piu' comune del passo successivo che va a vuoto */
        static const char hx[] = "0123456789abcdef";
        char b[16];
        int k;
        b[0] = ' '; b[1] = '='; b[2] = ' '; b[3] = '0'; b[4] = 'x';
        for (k = 0; k < 8; ++k) b[5 + k] = hx[(cpu->_eax >> (28 - 4 * k)) & 0xF];
        b[13] = '\n';
        (void) write(2, b, 14);
    }
    return ret;      /* the ARM stub branches here: no host frame per guest->import edge */
}
