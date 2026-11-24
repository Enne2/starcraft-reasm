#define _GNU_SOURCE
/* runtime/arm/game_main.c — entry point of the ARMv6 StarCraft runtime (starcraft-armv6).
 *
 * What it does, in the order the i386 runtime did it (sc-reasm/tools/elf/main_elf.c), adapted to
 * the LLASM execution model:
 *
 *   1. registers image regions with the memory layer, so `arm_va_is_mapped` accepts guest
 *      accesses into translated data (the addresses come from the generated guest_layout_arm.h);
 *   2. restores the first page of each original PE at its image base (0x00400000 / 0x15000000):
 *      `__ImageBase`-relative code and the shims' resource walkers expect their own module header
 *      there;
 *   3. builds the `_cpu` (low arena), the guest stack and the emulated TEB/PEB;
 *   4. enters the translated CRT `start` (0x00404C21) through `cpu_llasm_run_raw`, which installs
 *      the sentinel return address. From that point the guest runs the whole original startup:
 *      CRT init, `_WinMain@16`, MPQ loading, window creation — every Win32 call is a bridge thunk.
 *   5. if the guest returns (it normally does not: the game runs its own message loop), the
 *      runtime reports where it stopped instead of exiting silently.
 *
 * The Imports are *static*: the images were linked with every import resolved to a bridge thunk,
 * so there is nothing to patch at startup. `arm_bridge_unk_count()` is printed because each UNK
 * entry is a place where the game will stop with a name — the worklist of this port.
 */
#include "bridge.h"
#include "cpu_llasm.h"
#include "elf_support.h"
#include "game_memory.h"
#include "guest_cpu_helpers.h"
#if defined(__i386__)
#include "guest_layout_x86.h"
#else
#include "guest_layout_arm.h"
#endif
#include "indirect_dispatch.h"
#include "thread_affinity.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <stdio.h>

/* Call-out del runtime verso una procedura guest (guest_call.c): risolve il VA con la
 * tabella della dispatch, spinge gli argomenti sullo stack emulato e ritorna al sentinella. */
extern int arm_guest_calln(uint32_t proc, unsigned nargs, const uint32_t *args, int *status);
extern void win32_shim_init(void);

/* Made linkable by the IR post-processing step (build/runtime-arm/game IR files). */
extern void loc_404C21(_cpu *cpu);

/* Generated from the original PE files (gen_game_layout.py --out-pe). */
extern const unsigned char k_pe_exe_hdr[];
extern const uint32_t k_pe_exe_hdr_len;
extern const unsigned char k_pe_storm_hdr[];
extern const uint32_t k_pe_storm_hdr_len;

/* Tabella emessa da runtime/arm/gen_pe_image_bytes.py insieme alle sezioni stesse (una quadrupla
 * per regione: VA, size, nome, flag), quindi registrare qui significa registrare esattamente cio'
 * che e' linkato. Se l'oggetto non e' nel link i simboli valgono 0 (weak) e il ciclo non gira:
 * nessuna regione "registrata" che non sia anche mappata. */
extern const uint32_t k_pe_gaps[] __attribute__((weak));
extern const uint32_t k_pe_gaps_n[] __attribute__((weak));

#define IMAGE_BASE_EXE   0x00400000u
#define IMAGE_BASE_STORM 0x15000000u
#define GUEST_STACK_SIZE 0x00200000u     /* 2 MiB, in the low arena */

static void out(const char *s) { (void) write(1, s, strlen(s)); }

static void out_hex(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char b[11];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; ++i) b[2 + i] = hx[(v >> (28 - 4 * i)) & 0xF];
    b[10] = '\n';
    (void) write(1, b, 11);
}

static void out_dec(unsigned v) {
    char b[12];
    int i = 0;
    if (!v) { (void) write(1, "0", 1); return; }
    while (v) { b[i++] = (char) ('0' + v % 10u); v /= 10u; }
    while (i) (void) write(1, &b[--i], 1);
}

/* Opt-in guest-PC sampler. QEMU user-mode builds an ARM signal context for the
 * emulated process; keep the handler to integer operations and async-safe write/_exit. */
#define SC_PROF_SLOTS 4096u
#define SC_PROF_LIMIT 1000u
#define SC_PROF_TOP 20u
typedef struct { volatile uint32_t pc, lr, sp, fp, hits; } ScProfSlot;
static ScProfSlot *g_sc_prof;
static volatile uint32_t g_sc_prof_samples;
static volatile sig_atomic_t g_sc_prof_done, g_sc_prof_exit;
#include <pthread.h>
static const char k_prof_hex[] = "0123456789ABCDEF";
static void prof_hex(uint32_t v) {
    char b[11]; unsigned i;
    b[0]='0'; b[1]='x';
    for (i=0;i<8;i++) b[i+2]=k_prof_hex[(v>>(28-4*i))&15u];
    b[10]='\n'; (void)write(2,b,sizeof b);
}
static void prof_dec(uint32_t v) {
    char b[11]; unsigned n=0;
    do { b[n++]=(char)('0'+v%10u); v/=10u; } while(v && n<sizeof b);
    while(n) (void)write(2,&b[--n],1);
}
static void sc_prof_handler(int sig, siginfo_t *info, void *context) {
    ucontext_t *uc=(ucontext_t *)context;
#if defined(__i386__)
    uint32_t pc=(uint32_t)uc->uc_mcontext.gregs[REG_EIP];
    uint32_t lr=0; /* x86 has no link register */
    uint32_t sp=(uint32_t)uc->uc_mcontext.gregs[REG_ESP];
    uint32_t fp=(uint32_t)uc->uc_mcontext.gregs[REG_EBP];
#elif defined(__arm__)
    uint32_t pc=(uint32_t)uc->uc_mcontext.arm_pc;
    uint32_t lr=(uint32_t)uc->uc_mcontext.arm_lr;
    uint32_t sp=(uint32_t)uc->uc_mcontext.arm_sp, fp=(uint32_t)uc->uc_mcontext.arm_fp;
#else
#error unsupported runtime architecture
#endif
    uint32_t i=(pc^(pc>>12))&(SC_PROF_SLOTS-1u), n;
    (void)sig; (void)info;
    if (g_sc_prof_done) return;
    for (n=0;n<SC_PROF_SLOTS;n++,i=(i+1u)&(SC_PROF_SLOTS-1u)) {
        if (!g_sc_prof[i].hits || g_sc_prof[i].pc==pc) {
            g_sc_prof[i].pc=pc; g_sc_prof[i].lr=lr; g_sc_prof[i].sp=sp; g_sc_prof[i].fp=fp; ++g_sc_prof[i].hits; break;
        }
    }
    n=++g_sc_prof_samples;
    if (n<SC_PROF_LIMIT) return;
    g_sc_prof_done=1;
    { struct itimerval stop={{0,0},{0,0}}; (void)setitimer(sig==SIGPROF?ITIMER_PROF:ITIMER_REAL,&stop,0); }
}
static void *sc_prof_report_thread(void *unused) {
    uint32_t n; unsigned rank,j;
    FILE *f; char line[512];
    (void)unused;
    while(!g_sc_prof_done) (void)usleep(10000);
    n=g_sc_prof_samples;
    (void)write(2,"[SC_PROF] samples=",18); prof_dec(n); (void)write(2," top PC / LR / hits / percent\n",30);
    for(rank=0;rank<SC_PROF_TOP;rank++) {
        uint32_t best=0; unsigned bi=SC_PROF_SLOTS;
        for(j=0;j<SC_PROF_SLOTS;j++) if(g_sc_prof[j].hits>best) { best=g_sc_prof[j].hits; bi=j; }
        if(bi==SC_PROF_SLOTS) break;
        (void)write(2,"PC=",3); prof_hex(g_sc_prof[bi].pc);
        (void)write(2,"LR=",3); prof_hex(g_sc_prof[bi].lr);
        (void)write(2,"SP=",3); prof_hex(g_sc_prof[bi].sp);
        (void)write(2,"FP=",3); prof_hex(g_sc_prof[bi].fp);
        (void)write(2,"hits=",5); prof_dec(best); (void)write(2," pct=",5);
        prof_dec((best*10000u)/n); (void)write(2,"\n",1);
        if(g_sc_prof[bi].pc>=0x40856000u && g_sc_prof[bi].pc<0x409c2000u) {
            FILE *mem=fopen("/proc/self/mem","rb"); uint32_t saved_lr=0;
            if(mem && fseek(mem,(long)(g_sc_prof[bi].sp+100u),SEEK_SET)==0 && fread(&saved_lr,4,1,mem)==1) {
                (void)write(2,"saved-LR@SP+100=",16); prof_hex(saved_lr);
            }
            if(mem) fclose(mem);
        }
        g_sc_prof[bi].hits=0;
    }
    f=fopen("/proc/self/maps","r");
    if(f) {
        (void)write(2,"[SC_PROF] mappings near libc and sampled return PCs:\n",52);
        while(fgets(line,sizeof line,f)) {
            unsigned long lo=0,hi=0;
            if(sscanf(line,"%lx-%lx",&lo,&hi)==2 && ((hi>0x40800000ul && lo<0x41000000ul) || (hi>0x1A000000ul && lo<0x1B000000ul)))
                (void)write(2,line,strlen(line));
        }
        fclose(f);
    } else (void)write(2,"[SC_PROF] cannot read /proc/self/maps\n",39);
    if(g_sc_prof_exit) _exit(0);
    return 0;
}
static void sc_prof_start(void) {
    const char *mode=getenv("SC_PROF"), *ex=getenv("SC_PROF_EXIT");
    struct sigaction sa; struct itimerval it; int sig;
    if(!mode || !mode[0]) return;
    g_sc_prof=(ScProfSlot *)calloc(SC_PROF_SLOTS,sizeof *g_sc_prof);
    if(!g_sc_prof) return;
    sig=(mode[0]=='w' && mode[1]=='a' && mode[2]=='l' && mode[3]=='l' && !mode[4])?SIGALRM:SIGPROF;
    g_sc_prof_exit=ex && ex[0]=='1' && !ex[1];
    memset(&sa,0,sizeof sa); sa.sa_sigaction=sc_prof_handler; sa.sa_flags=SA_SIGINFO; sigemptyset(&sa.sa_mask);
    if(sigaction(sig,&sa,0)!=0) return;
    { unsigned long tid=0; if(pthread_create(&tid,0,sc_prof_report_thread,0)!=0) return; }
    memset(&it,0,sizeof it); it.it_value.tv_sec=3; it.it_interval.tv_usec=10000;
    (void)setitimer(sig==SIGPROF?ITIMER_PROF:ITIMER_REAL,&it,0);
    (void)write(2,"[SC_PROF] sampling enabled\n",27);
}

/* --- emulazione del loader per storm.dll: DllMain(hinst, DLL_PROCESS_ATTACH, NULL) ---------
 *
 * ORACOLO (storm.orig.dll, DB build/lift-full/ida/storm.lift.dll.i64):
 *   0x1503F6A0  DllEntryPoint: reason==1 -> InitializeCriticalSection x4 a 0x1505BDC0, poi
 *               0x1501F710 (SMemInit: 3 heap interni, 256 critical section a 0x1505C880,
 *               dword_1505E490 = 1), poi 0x1500671E (DllMain del CRT -> 0x1500F110).
 *   0x15020ED0  SMemAlloc: se dword_1505E490 == 0 -> storm_461(0x8510007D, "SMemAlloc()"):
 *               è esattamente l'errore 125 osservato prima di questa chiamata. Senza il DllMain
 *               la DLL non inizializza il proprio heap: non è un difetto del traduttore.
 *   0x1500F15F  il DllMain del CRT costruisce la tabella crypt MPQ (dword_1505E5E4) col seed
 *               0x10000100 (NON standard); 0x15017E29 (SFileOpenArchive) la ricostruisce col seed
 *               standard 0x00100001 SOLO se il puntatore è zero -> va azzerato dopo il DllMain,
 *               come fa già il runtime i386 dello stesso progetto (sc-reasm tools/elf/main_elf.c,
 *               stesso errore e stesso fix).
 */
#define STORM_ENTRY_VA     0x1503F6A0u   /* DllEntryPoint */
#define STORM_CRYPT_PTR_VA 0x1505E5E4u   /* dword_1505E5E4: puntatore alla tabella crypt MPQ */

static void storm_loader_init(void) {
    uint32_t args[3];
    int st = 0, r;
    uint32_t ct;

    args[0] = IMAGE_BASE_STORM;
    args[1] = 1u;                        /* DLL_PROCESS_ATTACH */
    args[2] = 0u;                        /* lpReserved */
    out("[game] DllMain di storm (DLL_PROCESS_ATTACH, hinst=0x15000000)\n");
    r = arm_guest_calln(STORM_ENTRY_VA, 3, args, &st);
    if (st != 0) {
        out("[game] DllMain di storm NON eseguito (status ");
        if (st < 0) { out("-"); out_dec((unsigned) -st); } else out_dec((unsigned) st);
        out(")\n");
        return;
    }
    out("[game] DllMain di storm -> ");
    out_dec((unsigned) r);
    out("\n");

    ct = cpu_llasm_read32(STORM_CRYPT_PTR_VA);
    if (ct) {
        out("[game] tabella crypt di storm non standard a ");
        out_hex(ct);
        out("[game] azzerata: la ricostruisce SFileOpenArchive col seed standard\n");
        cpu_llasm_write32(STORM_CRYPT_PTR_VA, 0u);
    } else {
        out("[game] crypt table di storm gia' vuota\n");
    }
}

int main(void) {
    _cpu *cpu;
    unsigned i;

    /* Affinità opzionale: nessuna modifica al default se SC_AFFINITY è unset. */
    (void) sc_affinity_apply(0);

    /* Inizializza stato globale dello shim (REASM_TRACE, STARTUPINFO); deve precedere
     * qualunque chiamata Win32 del guest. Gli altri init del runtime x86 hanno equivalenti
     * ARM espliciti: loader_init è sostituito dalla mappatura PE/regioni qui sotto, TEB da
     * cpu_llasm_create, e gli IAT sono risolti staticamente nei bridge. */
    win32_shim_init();
    sc_prof_start();
    out("[game] runtime nativo: immagini tradotte, import statici\n");

    /* 1) image regions known to the memory layer */
    for (i = 0; i < GUEST_REGION_COUNT; ++i) {
        if (arm_register_region(k_guest_regions[i].base, k_guest_regions[i].size,
                                k_guest_regions[i].what) != 0)
            out("[game] ATTENZIONE: regione non registrata\n");
    }

    /* 1b) byte dell'immagine PE che il lift non colloca (.rsrc, .reloc, buchi di .text/.rdata).
     * Non sono un extra: su Windows ogni sezione e' presente per ceil(VirtualSize/0x1000) pagine
     * (byte del file fino a RawSize, zeri oltre), e la directory risorse dell'eseguibile vive a
     * 0x006DE000 — senza, il walker dello shim legge fuori da ogni mappatura (0x006DE00C). */
    if (k_pe_gaps && k_pe_gaps_n) {
        unsigned n = k_pe_gaps_n[0];
        for (i = 0; i < n; ++i) {
            if (arm_register_region(k_pe_gaps[4 * i], k_pe_gaps[4 * i + 1],
                                    (const char *) (uintptr_t) k_pe_gaps[4 * i + 2]) != 0)
                out("[game] ATTENZIONE: byte PE non registrati\n");
        }
        out("[game] byte PE ai VA originali: ");
        out_dec(n);
        out(" regioni\n");
    } else {
        out("[game] ATTENZIONE: nessuna regione di byte PE nel link (.rsrc non mappata)\n");
    }

    /* 2) PE headers at the image bases */
    if (arm_map_at(IMAGE_BASE_EXE, 0x1000u) == 0) {
        memcpy((void *) (unsigned long) IMAGE_BASE_EXE, k_pe_exe_hdr, k_pe_exe_hdr_len);
        arm_register_region(IMAGE_BASE_EXE, 0x1000u, "exe.pehdr");
        out("[game] header PE exe a ");
        out_hex(IMAGE_BASE_EXE);
    }
    if (arm_map_at(IMAGE_BASE_STORM, 0x1000u) == 0) {
        memcpy((void *) (unsigned long) IMAGE_BASE_STORM, k_pe_storm_hdr, k_pe_storm_hdr_len);
        arm_register_region(IMAGE_BASE_STORM, 0x1000u, "storm.pehdr");
        out("[game] header PE storm a ");
        out_hex(IMAGE_BASE_STORM);
    }

    /* 3) CPU + guest stack + emulated TEB/PEB */
    cpu = cpu_llasm_create(GUEST_STACK_SIZE);
    if (!cpu) { out("[game] _cpu non creabile (arena bassa)\n"); return 2; }
    /* Le due regioni di codice del guest (guest_layout_arm.h: exe.code, storm.code) servono
     * all'handler di crash per stampare la catena di chiamate sullo stack emulato. */
    {
        uint32_t lo[2] = { 0, 0 }, hi[2] = { 0, 0 };
        int k = 0;
        for (i = 0; i < GUEST_REGION_COUNT && k < 2; ++i) {
            const char *w = k_guest_regions[i].what;
            if (w[0] && w[strlen(w) - 1] == 'e' && strstr(w, ".code")) {
                lo[k] = k_guest_regions[i].base;
                hi[k] = k_guest_regions[i].base + k_guest_regions[i].size;
                ++k;
            }
        }
        cpu_llasm_set_code_ranges(lo[0], hi[0], lo[1], hi[1]);
    }
    if (cpu_llasm_install_crash_handler(cpu) != 0)
        out("[game] ATTENZIONE: handler di crash non installato\n");

    out("[game] cpu=");
    out_hex((uint32_t) (uintptr_t) cpu);
    out("[game] TEB=");
    out_hex(arm_teb_base());
    out("[game] PEB=");
    out_hex(arm_peb_base());
    out("[game] bridge: ");
    out_dec(g_bridge_count);
    out(" import, ");
    out_dec(arm_bridge_unk_count());
    out(" UNK dichiarati\n");
    /* ESPERIMENTO (diagnosi): la CRT si ferma in una routine di registrazione atexit che scrive
     * attraverso i marcatori __onexitbegin/__onexitend (0x6DD684/0x6DD688) senza inizializzarli.
     * Se li si punta a un buffer valido il giro riprende: serve a isolare la causa (marcatori non
     * inizializzati) dal resto. */
    if (getenv("SC_ATEXIT_FIX")) {
        void *scratch = arm_alloc_low(4096, 8);
        if (scratch) {
            uint32_t end = (uint32_t) (uintptr_t) scratch + 4096u;
            cpu_llasm_write32(0x006DD684u, end);
            cpu_llasm_write32(0x006DD688u, end);
            out("[game] SC_ATEXIT_FIX: marcatori onexit puntati al buffer dell'arena\n");
        }
    }

    out("[game] entry: start() del CRT a 0x00404C21\n");

    /* 3b) limiti del nostro codice per la dispatch dei salti indiretti (indirect_dispatch.c).
     * Senza questo passo un `bx` verso un indirizzo guest non tradotto e' un SIGILL muto. */
    reasm_dispatch_init();

    /* 3c) loader semantics per storm.dll (DllMain), poi la tabella crypt MPQ.
     * Va PRIMA della entry del CRT: è quello che fa il loader di Windows al LoadLibrary. */
    storm_loader_init();

    /* 4) run the translated startup */
    if (cpu_llasm_run_raw(cpu, loc_404C21) == 0) {
        /* The sentinel fired: the guest executed its final `ret` (normally the game loops in its
         * own message pump and never comes back). */
        out("[game] il guest e' tornato al sentinella — stop\n");
        out("[game] eax=");
        out_hex(cpu->_eax);
    } else {
        out("[game] l'entry e' tornata senza consumare il sentinella (pila sbilanciata)\n");
        return 3;
    }
    return 0;
}
