/* sc-reasm/tools/elf/main_elf.c — main del runtime ELF nativo di StarCraft (fase 2, task N-02).
 *
 * Ordine: init shim -> loader (header PE ai VA delle immagini) -> TEB/PEB -> installazione IAT
 * (135 ordinali storm -> VA in-process, API Win32 -> shim) -> trap di WinMain -> `start` originale
 * (0x404C21). Da li' gira il codice del gioco ai suoi VA di sempre, senza Wine e senza DLL Windows.
 */
#include "teb.h"
#include "reasm_elf.h"
#include "win32_shim.h"
#include "dlg_elf.h"
extern int input_elf_selftest(void) __attribute__((weak));

typedef struct { const char *dll; const char *name; unsigned int slot; } ReasmImport;
extern const ReasmImport g_imports[];
extern const int g_imports_n;
void reasm_iat_install(void);
unsigned int reasm_storm_ordinal_va(int ord);
void *win32_storm_override(int ord);   /* storm_ext.c */
void *win32_shim_lookup_dll(const char *dll, const char *name); /* T3 extension */

extern int mprotect(void *, unsigned long, int);
extern int memcmp(const void *, const void *, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern int atoi(const char *);
extern char *getenv(const char *);
extern void (*signal(int, void (*)(int)))(int);
extern void _exit(int);

#define ENTRY_VA    0x00404C21u      /* start (CrtStartup) */
#define STORM_ENTRY_VA 0x1503F6A0u   /* _DllMainCRTStartup di storm (da storm/pipeline.json) */
#define WINMAIN_VA  0x004E0AE0u      /* _WinMain@16 */
#define STORM_BASE  0x15000000u      /* al di sotto: IAT dell'eseguibile */

static const unsigned char WINMAIN_PROLOGUE[7] = { 0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08 };
unsigned int g_winmain_va = WINMAIN_VA;
extern void winmain_trap(void);
extern void reasm_call_on_stack(void *stack_top, void (*fn)(void));

/* Stack BASSO (sotto 0x80000000). Win32 a 32 bit assume che gli indirizzi utente siano < 0x80000000:
 * storm codifica "contenitore vuoto" come ~ptr, che e' negativo solo in quel caso. Sullo stack Linux
 * (0xffffcxxx) il sentinella diventa positivo e il codice scorre un contenitore inesistente ->
 * dereferenzia 0xDDDDDDDD. Verificato con un watchpoint sul campo scritto a 0x1500316A. */
#define LOW_STACK_BASE 0x08000000u
#define LOW_STACK_SIZE (8u * 1024u * 1024u)
extern void *mmap(void *, unsigned long, int, int, int, long);

static int is_storm_ord(const char *dll, const char *name) {
    return name && name[0] == '#' && dll && (dll[0] == 's' || dll[0] == 'S');
}

/* Risolutore degli slot IAT (debole in iat_native.c: qui la definizione forte). */
void *reasm_import_lookup(const char *dll, const char *name) {
    if (is_storm_ord(dll, name)) {
        /* Override di progetto (p.es. salto dei filmati assenti): l'originale resta comunque
         * chiamabile e viene invocato per primo dall'override stesso. */
        void *ov = win32_storm_override(atoi(name + 1));
        if (ov) return ov;
        return (void *) (unsigned long) reasm_storm_ordinal_va(atoi(name + 1));
    }
    if (name && name[0] == '#') return 0;               /* ordinale di una DLL non storm: non noto */
    return win32_shim_lookup_dll(dll, name);
}

static void putdec(unsigned int v) {
    char b[12];
    int i = 0;
    if (!v) { reasm_elf_log("0"); return; }
    while (v) { b[i++] = (char) ('0' + v % 10u); v /= 10u; }
    while (i) { char c = b[--i]; char s[2]; s[0] = c; s[1] = 0; reasm_elf_log(s); }
}

/* Riepilogo IAT: ogni slot deve avere un target (storm in-process, shim Win32, o trampolino UNK
 * dichiarato: questi ultimi loggano ed escono, mai uno stub che ritorna). */
static void iat_summary(void) {
    int i, exe = 0, exe_storm = 0, exe_shim = 0, exe_tramp = 0;
    int st = 0, st_storm = 0, st_shim = 0, st_tramp = 0, zero = 0;
    for (i = 0; i < g_imports_n; ++i) {
        const char *nm = g_imports[i].name;
        int is_exe = g_imports[i].slot < STORM_BASE;
        int kind;
        if (!*(void **) (unsigned long) g_imports[i].slot) ++zero;
        if (is_storm_ord(g_imports[i].dll, nm) && reasm_storm_ordinal_va(atoi(nm + 1)))
            kind = win32_storm_override(atoi(nm + 1)) ? 1 : 0;  /* storm (o override di progetto) */
        else if (!is_storm_ord(g_imports[i].dll, nm) && win32_shim_lookup_dll(g_imports[i].dll, nm))
            kind = 1;                                     /* shim Win32 del progetto */
        else
            kind = 2;                                     /* trampolino UNK (stop dichiarato) */
        if (is_exe) {
            ++exe;
            if (kind == 0) ++exe_storm; else if (kind == 1) ++exe_shim; else ++exe_tramp;
        } else {
            ++st;
            if (kind == 0) ++st_storm; else if (kind == 1) ++st_shim; else ++st_tramp;
        }
    }
    reasm_elf_log("[reasm-elf] IAT exe:   ");
    putdec((unsigned) exe); reasm_elf_log("/"); putdec((unsigned) exe);
    reasm_elf_log(" slot risolti ("); putdec((unsigned) exe_storm);
    reasm_elf_log(" ordinali storm in-process + "); putdec((unsigned) exe_shim);
    reasm_elf_log(" shim Win32; "); putdec((unsigned) exe_tramp);
    reasm_elf_log(" trampolini UNK dichiarati; ");
    putdec((unsigned) (exe - exe_tramp)); reasm_elf_log(" coperti)\n");
    reasm_elf_log("[reasm-elf] IAT storm: ");
    putdec((unsigned) st); reasm_elf_log("/"); putdec((unsigned) st);
    reasm_elf_log(" slot risolti ("); putdec((unsigned) (st_storm + st_shim));
    reasm_elf_log(" coperti + "); putdec((unsigned) st_tramp); reasm_elf_log(" trampolini UNK dichiarati)\n");
    if (zero) { reasm_elf_log("[reasm-elf] ATTENZIONE: "); putdec((unsigned) zero); reasm_elf_log(" slot NULL\n"); }
    else reasm_elf_log("[reasm-elf] nessuno slot IAT lasciato a NULL\n");
}

/* Diagnostica: su segnale, stampa gli indirizzi di CODICE presenti sullo stack (catena di
 * chiamate); senza questo un crash e' solo "eip=0" e non dice nulla. */
static void crash_handler(int sig) {
    unsigned long *sp;
    int i, shown = 0;
#if defined(__arm__)
    sp = (unsigned long *) &sp;   /* ARM port: address of a local = current stack pointer */
#else
    __asm__ volatile ("movl %%esp, %0" : "=r"(sp));
#endif
    reasm_elf_log("[reasm-elf] SEGNALE ");
    putdec((unsigned) sig);
    reasm_elf_log(": indirizzi di codice sullo stack:\n");
    for (i = 0; i < 400 && shown < 12; ++i) {
        unsigned long v = sp[i];
        if ((v >= 0x401000ul && v < 0x4FE000ul) || (v >= 0x15001000ul && v < 0x15069000ul) ||
            (v >= 0x15471000ul && v < 0x1547F000ul)) {
            reasm_elf_log("   "); reasm_elf_log_hex((unsigned) v); reasm_elf_log("\n");
            ++shown;
        }
    }
    _exit(9);
}

static void install_winmain_trap(void) {
    unsigned char *p = (unsigned char *) (unsigned long) WINMAIN_VA;
    unsigned long page = (unsigned long) p & ~0xFFFul;
    if (memcmp(p, WINMAIN_PROLOGUE, sizeof WINMAIN_PROLOGUE) != 0) {
        reasm_elf_log("[reasm-elf] WinMain: prologo INATTESO -> trap non installato\n");
        return;
    }
    if (mprotect((void *) page, 0x1000, 7) != 0) {
        reasm_elf_log("[reasm-elf] WinMain: mprotect fallita -> trap non installato\n");
        return;
    }
    p[0] = 0xE9;
    *(int *) (p + 1) = (int) ((unsigned long) &winmain_trap - (WINMAIN_VA + 5));
    p[5] = 0x90;
    p[6] = 0x90;
    reasm_elf_log("[reasm-elf] trap di _WinMain@16 installato (prologo auto-verificato)\n");
}

static void storm_dllmain(void) {
    int (__attribute__((stdcall)) *dllmain)(void *, unsigned long, void *) =
        (int (__attribute__((stdcall)) *)(void *, unsigned long, void *)) (unsigned long) STORM_ENTRY_VA;
    int r = dllmain((void *) 0x15000000u, 1 /*DLL_PROCESS_ATTACH*/, 0);
    reasm_elf_log("[reasm-elf] DllMain di storm -> ");
    putdec((unsigned) r);
    reasm_elf_log("\n");
}
static void call_start(void) { ((void (*)(void)) (unsigned long) ENTRY_VA)(); }

int main(void) {
    /* Identita' di build PRIMA di tutto: se il codice e' cambiato dopo la compilazione questo
     * binario e' obsoleto e si rifiuta di partire (exit 3). Vedi tools/elf/build_guard.c e, per la
     * deroga dichiarata, SC_ALLOW_STALE=1. SC_BUILD_CHECK_ONLY=1 fa solo la verifica. */
    {
        extern int build_guard_check(int);
        extern char *getenv(const char *);
        int check_only = getenv("SC_BUILD_CHECK_ONLY") && getenv("SC_BUILD_CHECK_ONLY")[0] == '1';
        int stale = build_guard_check(check_only);
        if (check_only) return stale;
        if (stale) return 3;
    }
    reasm_elf_log("[reasm-elf] StarCraft — runtime ELF nativo (fase 2, nessuna DLL Windows)\n");
    reasm_elf_log("[reasm-elf] TLS: tls_dir_va=0 -> nessuna directory TLS da collocare\n");
    signal(11 /*SIGSEGV*/, crash_handler);
    signal(4 /*SIGILL*/, crash_handler);
    win32_shim_init();
    loader_init();
    teb_init();
    reasm_iat_install();
    iat_summary();
    if (getenv("SC_DLG_INPUT_DEMO")) return dlg_elf_input_demo();
    if (getenv("SC_DLG_INPUT_SELFTEST")) return dlg_elf_input_selftest();
    if (getenv("SC_DLG_SELFTEST")) return dlg_elf_selftest();
    if (getenv("SC_INPUT_SELFTEST")) return input_elf_selftest ? input_elf_selftest() : 2;
    /* stack basso per storm e per il gioco */
    {
        void *st = mmap((void *) (unsigned long) LOW_STACK_BASE, LOW_STACK_SIZE, 3, 0x32, -1, 0);
        if (st != (void *) (unsigned long) LOW_STACK_BASE) {
            reasm_elf_log("[reasm-elf] mmap dello stack basso FALLITA\n");
        } else {
            reasm_elf_log("[reasm-elf] stack basso a 0x08000000, 8 MiB (indirizzi < 0x80000000)\n");
        }
        teb_set_stack(LOW_STACK_BASE + LOW_STACK_SIZE, LOW_STACK_BASE);
    }
    install_winmain_trap();
    /* Emulazione del lavoro del loader per la DLL: DllMain(hinst, DLL_PROCESS_ATTACH, NULL).
     * Senza questa chiamata storm non inizializza il proprio heap/CRT: SMemAlloc fallisce con
     * "SMemAlloc() : error 125" (osservato davvero). */
    reasm_elf_log("[reasm-elf] DllMain di storm (DLL_PROCESS_ATTACH, hinst=0x15000000)\n");
    reasm_call_on_stack((void *) (unsigned long) (LOW_STACK_BASE + LOW_STACK_SIZE), storm_dllmain);
    /* Crypt table MPQ (globale di storm a base+0x5E5E4): il DllMain di storm la costruisce con un
     * seed NON standard (0x10000100) e la guardia di #266/#268 - SFileOpenArchive/SFileOpenFileEx -
     * la ricostruisce con il seed STANDARD (0x00100001) SOLO se il puntatore e' zero. Con la tabella
     * sbagliata nessun file si trova negli MPQ ("font\font.gid" non trovato -> Data File Error).
     * Stesso fix del percorso PE (reasm-toolkit tools/runtime_main.c, doc sc-reasm/04). */
    {
        volatile unsigned int *ct = (volatile unsigned int *) (unsigned long) (0x15000000u + 0x5E5E4u);
        if (*ct) {
            reasm_elf_log("[reasm-elf] crypt table di storm non standard: azzero il puntatore\n");
            *ct = 0;
        } else {
            reasm_elf_log("[reasm-elf] crypt table gia' vuota: la costruira' #266/#268 col seed standard\n");
        }
    }
    reasm_elf_log("[reasm-elf] chiamo start (0x00404C21)\n");
    reasm_call_on_stack((void *) (unsigned long) (LOW_STACK_BASE + LOW_STACK_SIZE), call_start);
    reasm_elf_log("[reasm-elf] start e' tornato (uscita normale)\n");
    return 0;
}
