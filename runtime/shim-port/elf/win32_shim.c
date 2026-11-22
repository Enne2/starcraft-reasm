/* sc-reasm/tools/elf/win32_shim.c — API Win32 su libc per il runtime ELF nativo di StarCraft.
 *
 * Perimetro di questa fase: il percorso CRT `start` -> `_WinMain@16` e gli inizializzatori
 * statici. Le API necessarie sono scoperte empiricamente dal log (trampolino UNK del toolkit) e
 * aggiunte qui. Niente windows.h: il runtime e' freestanding (-nostdinc), quindi si dichiarano a
 * mano le funzioni libc usate.
 *
 * REGOLA (D19): una API che non conosciamo NON si finge con uno stub che ritorna — la pila stdcall
 * verrebbe corrotta. Resta fuori tabella: il toolkit la risolve col trampolino che logga ed esce.
 */
#include "win32_shim.h"
#include "win32_shell_dlg.h"
#include "reasm_elf.h"
/* reasm-toolkit declares these hooks stdcall, but the x86 runtime implementations
 * (runtime/arm/elf_support_arm.c) are ordinary C/cdecl. Calling them through the imported
 * declaration makes clang emit stdcall cleanup for a cdecl callee and corrupts the host stack.
 * Keep the ARM declarations/calls unchanged; on i386, call the same symbols via cdecl types. */
#if defined(__i386__)
typedef void *(__attribute__((cdecl)) *reasm_module_cdecl_fn)(const char *);
typedef void *(__attribute__((cdecl)) *reasm_load_cdecl_fn)(const char *);
typedef void *(__attribute__((cdecl)) *reasm_proc_cdecl_fn)(void *, const char *);
static void *reasm_module_handle_cdecl(const char *n) {
    return ((reasm_module_cdecl_fn) reasm_elf_GetModuleHandleA)(n);
}
static void *reasm_load_library_cdecl(const char *n) {
    return ((reasm_load_cdecl_fn) reasm_elf_LoadLibraryA)(n);
}
static void *reasm_get_proc_cdecl(void *m, const char *n) {
    return ((reasm_proc_cdecl_fn) reasm_elf_GetProcAddress)(m, n);
}
#else
#define reasm_module_handle_cdecl reasm_elf_GetModuleHandleA
#define reasm_load_library_cdecl  reasm_elf_LoadLibraryA
#define reasm_get_proc_cdecl      reasm_elf_GetProcAddress
#endif
/* P1 (2026-10-11, supervisore): i backend nativi filesystem di T4 vivono in win32_game.c e
 * portano il prefisso game_. Qui le quattro API legacy ci DELEGANO: senza la delega il resolver
 * dava precedenza a questi stub e il gioco mostrava "File error creating new character"
 * (CreateDirectoryA non impostava ERROR_ALREADY_EXISTS=183 su un path esistente). */
#include "win32_game.h"
#include "game_memory_arm.h"
#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)
#include "x86_helpers.h"   /* Port: emulated TEB (X86_Read/WriteFsDword) */
#include "game_memory_arm.h" /* mapped guest memory validation for SEH records */
#endif

#define S __attribute__((stdcall))

/* --- libc (dichiarata a mano: -ffreestanding -nostdinc) --- */
extern void *malloc(ulong);
extern void free(void *);
extern void *realloc(void *, ulong);
extern ulong malloc_usable_size(void *);
extern void *memcpy(void *, const void *, ulong);
extern void *memset(void *, int, ulong);
extern int memcmp(const void *, const void *, ulong);
extern ulong strlen(const char *);
extern int strcasecmp(const char *, const char *);
extern char *getenv(const char *);
extern char *getcwd(char *, unsigned long);
extern int *__errno_location(void);
extern int strcmp(const char *, const char *);
extern long write(int, const void *, ulong);
extern long read(int, void *, ulong);
extern long pread64(int, void *, ulong, long long);
extern int open(const char *, int, ...);
extern int close(int);
extern int fstat(int, void *);
extern int fsync(int);
extern long lseek(int, long, int);
extern int access(const char *, int);
extern int unlink(const char *);
extern int ftruncate(int, long);
extern long syscall(long, ...);
extern void tzset(void);
extern long timezone;
extern char *tzname[2];
#define unlink_ unlink
extern int usleep(unsigned int);
extern int clock_gettime(int, void *);
extern long time(long *);
extern void *mmap(void *, ulong, int, int, int, long);
extern int munmap(void *, ulong);
extern int mprotect(void *, ulong, int);
extern int madvise(void *, ulong, int);
extern void _exit(int);
extern int pthread_create(ulong *, const void *, void *(*)(void *), void *);
extern int pthread_join(ulong, void **);
extern unsigned long pthread_self(void);
extern void pthread_exit(void *);

void reasm_iat_install(void);
/* reasm_elf_Get{ModuleHandle,ProcAddress}/LoadLibraryA: prototipi in reasm_elf.h (stdcall) */

/* --- PATH Win32 <-> Unix ---
 * Il gioco costruisce i path con GetModuleFileNameA e li concatena con '\\' ("\\Stardat.mpq").
 * Quindi: GetModuleFileNameA deve restituire un path in stile Windows ("C:\\dir\\StarCraft.exe") e
 * i file si aprono traducendo 'C:' e '\\' -> '/'. Senza questa traduzione il gioco cercava
 * "StarCraft.exe\\Stardat.mpq" e mostrava il dialogo fatale "Stardat.mpq" (osservato davvero). */
void reasm_win_path(const char *in, char *out, int n) {
    int i = 0;
    if (in && in[0] && in[1] == ':') in += 2;         /* togli la lettera di unita' "C:" */
    if (!in) { if (n > 0) out[0] = 0; return; }
    while (*in && i < n - 1) { char c = *in++; out[i++] = (c == '\\') ? '/' : c; }
    out[i] = 0;
    /* QEMU -L roots absolute guest paths inside the ARM sysroot. A Windows path
     * rooted at the actual process cwd must stay cwd-relative on the Linux host. */
    if (out[0] == '/') {
        char cwd[260]; unsigned long l;
        if (getcwd(cwd, sizeof cwd)) {
            l = strlen(cwd);
            if (l && !memcmp(out, cwd, l) && (out[l] == '/' || out[l] == 0)) {
                int start = (out[l] == '/') ? (int) l + 1 : (int) l;
                int k = 0;
                while (out[start + k]) { out[k] = out[start + k]; ++k; }
                out[k] = 0;
            }
        }
    }
}
void reasm_unix_to_win(const char *in, char *out, int n) {
    int i = 0;
    if (n < 4) { if (n > 0) out[0] = 0; return; }
    out[i++] = 'C'; out[i++] = ':';
    while (*in && i < n - 2) { char c = *in++; out[i++] = (c == '/') ? '\\' : c; }
    out[i] = 0;
}

/* --- risoluzione CASE-INSENSITIVE dei path (Windows lo e') ---
 * Il gioco apre "Stardat.mpq" mentre su disco il file e' "StarDat.mpq": su Windows non importa,
 * su Linux si'. Si risolve componente per componente con getdents64 e si apre il nome REALE. */
struct __attribute__((packed)) ui_dirent64 { unsigned long long d_ino; long long d_off; unsigned short d_reclen;
                     unsigned char d_type; char d_name[]; };

#if defined(__arm__)
/* ARM port: an i386 `int $0x80` gate does not exist here. glibc provides the getdents64
 * wrapper, and O_DIRECTORY must come from <fcntl.h>: it is 0x4000 on 32-bit ARM, while the
 * literal below (0x10000) is the x86_64 value. Source of truth: the C library of the sysroot. */
#include <fcntl.h>
#include <sys/vfs.h>
#define UI_O_DIRECTORY O_DIRECTORY
/* getdents64 is a raw kernel-structure reader: declared here instead of relying on the
 * feature-test macros of <dirent.h> (the shim does not define _GNU_SOURCE). */
extern int getdents64(int fd, void *buffer, unsigned long count);
static int getdents64_(int fd, void *buf, unsigned int n) {
    return getdents64(fd, buf, (unsigned long) n);
}
#else
#define UI_O_DIRECTORY 0x10000
static int getdents64_(int fd, void *buf, unsigned int n) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(220), "b"(fd), "c"(buf), "d"(n) : "memory");
    return ret;
}
#endif
static int ci_lookup(const char *dir, const char *want, char *out, int n) {
    /* O_DIRECTORY differs across the x86 and ARM Linux ABIs; open the directory
     * read-only and let getdents64 validate it, avoiding a false ENOENT on ARM. */
    int fd = open(dir, 0);
    char buf[2048];
    int got;
    if (fd < 0) return 0;
    while ((got = getdents64_(fd, buf, sizeof buf)) > 0) {
        int off = 0;
        while (off < got) {
            struct ui_dirent64 *d = (struct ui_dirent64 *) (buf + off);
            if (d->d_reclen == 0) break;
            if (strcasecmp(d->d_name, want) == 0) {
                int i = 0;
                while (d->d_name[i] && i < n - 1) { out[i] = d->d_name[i]; ++i; }
                out[i] = 0;
                close(fd);
                return 1;
            }
            off += d->d_reclen;
        }
    }

    close(fd);
    return 0;
}
static int file_trace_enabled(void);
int reasm_open_ci(const char *path, int flags, int mode) {
    char resolved[512], comp[256], absolute[512], cwd[260];
    const char *requested = path;
    int ri = 0, pi, fd;
    if (!path) return open(path, flags, mode);
    fd = open(path, flags, mode);
    if (fd >= 0) {
        if (file_trace_enabled()) {
            reasm_elf_log("[file-open] requested="); reasm_elf_log(requested);
            reasm_elf_log(" native="); reasm_elf_log(path); reasm_elf_log(" cwd=");
            if (getcwd(cwd, sizeof cwd)) reasm_elf_log(cwd); else reasm_elf_log("<getcwd-error>");
            reasm_elf_log(" result=ok\n");
        }
        return fd;
    }
    /* Relative lookups stay relative: in qemu-arm -L, host cwd paths are NOT
     * accessible as guest absolute paths. ci_lookup starts at '.' for them. */
    if (path[0] == '/') { resolved[ri++] = '/'; pi = 1; }
    else { resolved[ri++] = '.'; resolved[ri++] = '/'; pi = 0; }
    while (path[pi]) {
        int ci = 0;
        while (path[pi] && path[pi] != '/') comp[ci++] = path[pi++];
        comp[ci] = 0;
        if (ci) {
            char dir[512], real[256];
            int di;
            for (di = 0; di < ri && di < 511; ++di) dir[di] = resolved[di];
            dir[ri] = 0;
            if (!ci_lookup(dir, comp, real, sizeof real)) {
                /* A CREATE_* open may target a new leaf. Resolve every parent
                 * case-insensitively, but preserve the requested leaf for open(). */
                if (path[pi] || !(flags & 0100)) {
                    if (file_trace_enabled()) {
                        reasm_elf_log("[ci] component miss dir="); reasm_elf_log(dir);
                        reasm_elf_log(" wanted="); reasm_elf_log(comp);
                        reasm_elf_log(" errno="); reasm_elf_log_hex((unsigned) *__errno_location()); reasm_elf_log("\n");
                    }
                    return -1;
                }
                { int k = 0; while (comp[k] && ri < 511) resolved[ri++] = comp[k++]; }
            } else {
                { int k = 0; while (real[k] && ri < 511) resolved[ri++] = real[k++]; }
            }
        }
        if (path[pi] == '/') { if (ri < 511) resolved[ri++] = '/'; ++pi; }
    }
    resolved[ri] = 0;
    fd = open(resolved, flags, mode);
    if (file_trace_enabled()) {
        reasm_elf_log("[file-open] requested="); reasm_elf_log(requested);
        reasm_elf_log(" native="); reasm_elf_log(resolved); reasm_elf_log(" cwd=");
        if (getcwd(cwd, sizeof cwd)) reasm_elf_log(cwd); else reasm_elf_log("<getcwd-error>");
        if (fd >= 0) reasm_elf_log(" result=ok-casefold\n");
        else { reasm_elf_log(" result=fail errno="); reasm_elf_log_hex((unsigned) *__errno_location()); reasm_elf_log("\n"); }
    }
    return fd;
}

/* --- traccia atomica: UNA sola write() per riga (il video ha un thread che logga in parallelo,
 * altrimenti le righe si interlacciano e il log diventa illeggibile) --- */
static int hx_(char *p, unsigned v) {
    static const char h[] = "0123456789abcdef";
    int i;
    p[0] = '0'; p[1] = 'x';
    for (i = 0; i < 8; ++i) p[2 + i] = h[(v >> (28 - 4 * i)) & 0xF];
    return 10;
}
void reasm_trace3(const char *a, unsigned x, const char *b, unsigned y, const char *c, unsigned z) {
    char buf[200];
    int i = 0;
    while (*a && i < 100) buf[i++] = *a++;
    i += hx_(buf + i, x);
    while (*b && i < 130) buf[i++] = *b++;
    i += hx_(buf + i, y);
    while (*c && i < 160) buf[i++] = *c++;
    i += hx_(buf + i, z);
    buf[i++] = '\n';
    write(2, buf, (unsigned long) i);
}
void reasm_trace2(const char *a, unsigned x, const char *b, unsigned y) {
    char buf[160];
    int i = 0;
    while (*a && i < 130) buf[i++] = *a++;
    i += hx_(buf + i, x);
    while (*b && i < 140) buf[i++] = *b++;
    i += hx_(buf + i, y);
    buf[i++] = '\n';
    write(2, buf, (unsigned long) i);
}

/* --- letture dal SISTEMA REALE (il gioco controlla risorse all'avvio) --- */
struct ui_statfs { long type, bsize, blocks, bfree, bavail, files, ffree; long fsid[2];
                   long namelen, frsize, flags, spare[4]; };
#if defined(__arm__)
/* ARM port: glibc statfs() instead of i386 syscall 99. The shim only reads numbers out of the
 * structure, and glibc's layout differs (f_type is 32-bit), so the fields are copied one by one
 * into the caller's ui_statfs instead of aliasing memory. */
static int statfs_(const char *path, struct ui_statfs *b) {
    struct statfs s;
    if (statfs(path, &s) != 0) return -1;
    b->type = (long) s.f_type;
    b->bsize = (long) s.f_bsize;
    b->blocks = (long) s.f_blocks;
    b->bfree = (long) s.f_bfree;
    b->bavail = (long) s.f_bavail;
    b->files = (long) s.f_files;
    b->ffree = (long) s.f_ffree;
    b->fsid[0] = (long) s.f_fsid.__val[0];
    b->fsid[1] = (long) s.f_fsid.__val[1];
    b->namelen = (long) s.f_namelen;
    b->frsize = (long) s.f_frsize;
    b->flags = (long) s.f_flags;
    b->spare[0] = b->spare[1] = b->spare[2] = b->spare[3] = 0;
    return 0;
}
#else
static int statfs_(const char *path, struct ui_statfs *b) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(99), "b"(path), "c"(b) : "memory");
    return ret;
}
#endif
/* conta i processori logici in /proc/cpuinfo (righe "processor") */
static u32 cpu_count_(void) {
    char buf[4096];
    int fd = open("/proc/cpuinfo", 0), n, i, c = 0;
    if (fd < 0) return 1;
    n = (int) read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 1;
    buf[n] = 0;
    for (i = 0; i + 9 < n; ++i)
        if (buf[i] == 'p' && memcmp(buf + i, "processor", 9) == 0) ++c;
    return (u32) (c > 0 ? c : 1);
}
/* kB di /proc/meminfo per una chiave ("MemTotal:", "MemAvailable:") */
static unsigned long meminfo_kb_(const char *key) {
    char buf[4096];
    int fd = open("/proc/meminfo", 0), n, i;
    unsigned long v = 0;
    int kl = (int) strlen(key);
    if (fd < 0) return 0;
    n = (int) read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    for (i = 0; i + kl < n; ++i) {
        if (memcmp(buf + i, key, (ulong) kl) != 0) continue;
        i += kl;
        while (i < n && (buf[i] == ' ' || buf[i] == '\t')) ++i;
        while (i < n && buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (unsigned long) (buf[i] - '0'); ++i; }
        return v;
    }
    return 0;
}

/* --- tipi Win32 minimi --- */
typedef struct { u32 lo, hi; } FILETIME_T;
typedef struct { u16 y, mo, dow, d, h, mi, s, ms; } SYSTEMTIME_T;
typedef struct {
    u32 cb, major, minor, build, platform; char csd[128];
} OSVERSIONINFOA_T;
typedef struct {
    u32 cb; char *reserved, *desktop, *title;
    u32 x, y, xsize, ysize, xchars, ychars, fill;
    u32 flags; u16 show, cres2; u8 *reserved2; void *hin, *hout, *herr;
} STARTUPINFOA_T;
/* MEMORYSTATUS: 8 campi, 32 byte esatti (il gioco ne passa 32: un campo in piu' scriverebbe
 * oltre il buffer del chiamante). */
typedef struct { u32 len, memload, totalphys, availphys, totalpage, availpage,
                     totalvirt, availvirt; } MEMORYSTATUS_T;
/* SYSTEM_INFO: 36 byte ESATTI. Gli ultimi due campi sono WORD (wProcessorLevel/Revision), non
 * DWORD: una struct da 40 byte fa scrivere 4 byte OLTRE il buffer del chiamante e azzera il suo
 * frame pointer salvato -> ebp=0 e crash (osservato: storm 0x1501EFB0 chiama GetSystemInfo su un
 * buffer di 0x24 byte a [ebp-0x24]). */
typedef struct {
    u32 arch, page, minapp, maxapp, activeproc, nproc, proctype, allocgran;
    u16 level, rev;
} SYSTEMINFO_T;
typedef struct { u32 base, allocbase, allocprot, size, state, prot, type; } MEMBASIC_T;

#define INVALID_HANDLE ((void *) -1)
#define H2FD(h) ((int) (ulong) (h) - 1)
#define FD2H(fd) ((void *) (ulong) ((fd) + 1))
#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u

/* --- stato --- */
static void *g_tls[64];
static int g_tls_n = 1;
static char g_cmdline[] = "StarCraft.exe";
static char g_envA[] = "PATH=/usr/local/bin:/usr/bin:/bin\0TEMP=/tmp\0\0";
static char g_modname[] = "StarCraft.exe";
static STARTUPINFOA_T g_si;


/* Ricerca di sottostringa senza libc: serve al registro minimo (chiavi del gioco). */
static int shim_contains(const char *hay, const char *needle) {
    int i, j;
    if (!hay || !needle) return 0;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; ++j) { }
        if (!needle[j]) return 1;
    }
    return 0;
}
/* Registro minimo: il gioco legge HKCU\Software\Blizzard Entertainment\Starcraft.
 * Con RegOpenKeyExA che fallisce non arriva MAI a RegQueryValueExA (misurato: 46 open, 0 query),
 * quindi la configurazione di rete resta ai default e la selezione del provider fallisce. */
#define SHIM_HKEY_STARCRAFT ((void *) 0x52450001u)

/* P12: il nostro provider (snp_elf.c) dichiara le proprie capacita' in un caps.dat servito come
 * archivio MPQ in memoria. Storm lo legge aprendo il PERCORSO del modulo, quindi i punti di
 * ingresso file devono riconoscere l'handle virtuale prima di toccare il filesystem. Le funzioni
 * sono deboli: senza snp_elf.c il comportamento resta quello di prima. */
extern void *win32_snp_vfile_open(const char *) __attribute__((weak));
extern int win32_snp_vfile_own(void *) __attribute__((weak));
extern unsigned int win32_snp_vfile_size(void *) __attribute__((weak));
extern long win32_snp_vfile_seek(void *, long, unsigned int) __attribute__((weak));
extern long win32_snp_vfile_read(void *, void *, unsigned int) __attribute__((weak));
extern void win32_snp_vfile_close(void *) __attribute__((weak));

static int g_trace;
/* Host main thread is recorded before guest startup; ExitThread must not _exit() the process
 * when a CreateThread worker returns normally. */
static unsigned long g_main_thread_id;

/* --- TIMER Win32 REALI (consegna sul thread del pump) --------------------------------------------
 * Il gioco usa SetTimer(hwnd,3,20,0x4A6030) con un TIMERPROC (0x4A6030 ha `ret 0x10`, quattro
 * argomenti: HWND, UINT, UINT_PTR, DWORD). Su Windows il proc viene invocato DAL THREAD che ha
 * creato il timer, dentro il suo message loop; quindi qui non si usa un thread separato (sarebbe
 * un'esecuzione fuori contesto, diversa da Windows e non sicura rispetto agli stati di gioco) ma
 * `win32_shim_timers_pump()`, chiamata da host_elf_pump() che gira nel thread del gioco.
 * Semantica: scadenza periodica, `hwnd`/`id`/`proc` conservati, `KillTimer` li rimuove davvero;
 * se `proc` e' NULL si consegna un messaggio WM_TIMER (0x113) al messaggio del gioco tramite
 * PostMessageA, come fa Windows quando il timer appartiene a una finestra. */
#define SHIM_TIMER_MAX 8
#define SHIM_WM_TIMER 0x0113u
typedef struct { unsigned id, ms; void *proc, *hwnd; unsigned long long next_us; int heap; } ShimTimer;
static ShimTimer g_timers[SHIM_TIMER_MAX];
static unsigned g_timer_next_id = 1;
static unsigned g_timer_fires, g_timer_misses;

extern int PostMessageA(void *, unsigned, unsigned, long) __attribute__((weak));
#if defined(__arm__) || defined(__i386__)
extern int arm_guest_call4(unsigned int proc, unsigned int a, unsigned int b,
                           unsigned int c, unsigned int d) __attribute__((weak));
#endif
S u32 GetTickCount(void);   /* definita piu' sotto nella stessa unita' */

static unsigned long long shim_now_us(void) {
    struct { long sec, nsec; } ts;
    extern int clock_gettime(int, void *);
    clock_gettime(1 /*CLOCK_MONOTONIC*/, &ts);
    return (unsigned long long) ts.sec * 1000000ull + (unsigned long long) (ts.nsec / 1000);
}

S unsigned SetTimer(void *hwnd, unsigned id, unsigned ms, void *proc) {
    int i, slot = -1;
    for (i = 0; i < SHIM_TIMER_MAX; ++i) {
        if (g_timers[i].id && g_timers[i].hwnd == hwnd && id && g_timers[i].id == id) { slot = i; break; }
        if (!g_timers[i].id && slot < 0) slot = i;
    }
    if (slot < 0) { if (g_trace) reasm_elf_log("[timer] SetTimer: registro pieno\n"); return 0; }
    if (!id) id = g_timer_next_id++;
    g_timers[slot].id = id; g_timers[slot].ms = ms ? ms : 1; g_timers[slot].proc = proc;
    g_timers[slot].hwnd = hwnd;
    g_timers[slot].next_us = shim_now_us() + (unsigned long long) ms * 1000ull;
    g_timers[slot].heap = 0;
    if (g_trace) { reasm_elf_log("[timer] SetTimer id="); reasm_elf_log_hex(id);
                   reasm_elf_log(" ms="); reasm_elf_log_hex(ms);
                   reasm_elf_log(" proc="); reasm_elf_log_hex((unsigned) (ulong) proc); reasm_elf_log("\n"); }
    return id;
}

S int KillTimer(void *hwnd, unsigned id) {
    int i;
    for (i = 0; i < SHIM_TIMER_MAX; ++i)
        if (g_timers[i].id == id && (g_timers[i].hwnd == hwnd || !hwnd)) {
            if (g_trace) { reasm_elf_log("[timer] KillTimer id="); reasm_elf_log_hex(id);
                           reasm_elf_log(" fires="); reasm_elf_log_hex(g_timers[i].heap); reasm_elf_log("\n"); }
            g_timers[i].id = 0;
            return 1;
        }
    return 1;   /* Windows: timer gia' scaduto/inesistente non e' un errore fatale */
}

/* Chiamata dal pump del gioco (thread corretto). Consegna al massimo un tick per timer per giro,
 * riallineando la scadenza sull'orologio monotono: niente raffiche se un frame e' lento. */
int win32_shim_timers_pump(void) {
    unsigned long long now = shim_now_us();
    int i, fired = 0;
    /* Diagnostica dichiarata: SC_TIMERS=0 sospende la consegna (per isolare un difetto che
     * dipende dal timer). Nessun effetto sul percorso normale. */
    {   extern char *getenv(const char *);
        const char *v = getenv("SC_TIMERS");
        if (v && v[0] == '0' && !v[1]) { return 0; }
    }
    for (i = 0; i < SHIM_TIMER_MAX; ++i) {
        ShimTimer *t = &g_timers[i];
        if (!t->id || now < t->next_us) continue;
        t->next_us = now + (unsigned long long) t->ms * 1000ull;
        ++t->heap; ++g_timer_fires; ++fired;
        if (t->proc) {
            /* TIMERPROC guest: va eseguito col call-out che ripristina CPU/stack emulati.
             * Come i WndProc in win32_ui.c, il call-out accetta sia VA guest sia label tradotte. */
#if defined(__arm__) || defined(__i386__)
            if (arm_guest_call4) {
                arm_guest_call4((unsigned)(ulong)t->proc, (unsigned)(ulong)t->hwnd,
                                SHIM_WM_TIMER, t->id, GetTickCount());
            } else
#endif
            {
                /* Fallback ABI host solo per build prive del runtime guest-call. */
                void (S *cb)(void *, unsigned, unsigned, unsigned) =
                    (void (S *)(void *, unsigned, unsigned, unsigned)) t->proc;
                cb(t->hwnd, SHIM_WM_TIMER, t->id, GetTickCount());
            }
        } else if (PostMessageA) {
            PostMessageA(t->hwnd, SHIM_WM_TIMER, t->id, 0);
        }
    }
    return fired;
}

unsigned win32_shim_timer_fires(void) { return g_timer_fires; }
unsigned win32_shim_timer_misses(void) { return g_timer_misses; }

void win32_shim_init(void) {
    g_main_thread_id = pthread_self();
    g_trace = getenv("REASM_TRACE") != 0;
    memset(&g_si, 0, sizeof g_si);
    g_si.cb = sizeof g_si;
    g_si.show = 1;                                  /* SW_SHOWNORMAL */
}

/* --- heap --- */
S void *GetProcessHeap(void) { return (void *) 0x00010001u; }
S void *HeapCreate(u32 flags, u32 init, u32 max) {
    (void) flags;
    if (g_trace) { reasm_elf_log("[trace] HeapCreate("); reasm_elf_log_hex(init);
                   reasm_elf_log(","); reasm_elf_log_hex(max); reasm_elf_log(")\n"); }
    return (void *) 0x00010001u; }
S int   HeapDestroy(void *h) { (void) h; return 1; }
/* Windows, su pagine fresche, restituisce memoria A ZERO: storm se ne serve (i suoi contenitori
 * assumono campo nullo). glibc malloc puo' restituire memoria riciclata con spazzatura, quindi qui
 * si azzera sempre (costo trascurabile all'avvio). */
S void *HeapAlloc(void *h, u32 flags, u32 size) {
    void *p;
    (void) h; (void) flags;
    p = malloc(size ? size : 1);
    if (p && size) memset(p, 0, size);
    if (g_trace) reasm_trace2("[trace] HeapAlloc ", size, " -> ", (unsigned) (ulong) p);
    return p;
}
S int   HeapFree(void *h, u32 flags, void *p) { (void) h; (void) flags; free(p); return 1; }
S void *HeapReAlloc(void *h, u32 flags, void *p, u32 size) {
    u32 old = p ? (u32) malloc_usable_size(p) : 0;
    void *n;
    (void) h; (void) flags;
    n = realloc(p, size ? size : 1);
    if (n && size > old) memset((char *) n + old, 0, size - old);
    return n;
}
S u32   HeapSize(void *h, u32 flags, void *p) {
    (void) h; (void) flags; return p ? (u32) malloc_usable_size(p) : 0xFFFFFFFFu;
}

/* --- memoria virtuale Win32: arena bassa + tabella ordinata di regioni --- */
#define MEM_COMMIT       0x1000u
#define MEM_RESERVE      0x2000u
#define MEM_DECOMMIT     0x4000u
#define MEM_RELEASE      0x8000u
#define MEM_PRIVATE      0x20000u
#define MEM_MAPPED       0x40000u
#define MEM_FREE         0x10000u
#define MEM_RESERVE_STATE 0x2000u
#define MEM_COMMIT_STATE 0x1000u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_ADDRESS 487u
#define VM_PAGE           0x1000u
#define VM_GRANULARITY    0x10000u
#define VM_ARENA_MAX      0x80000000u
#define VM_REGION_CAP     16384u
#define VM_POOL_CAP       4096u
#define VM_POOL_BUDGET    (24u * 1024u * 1024u)
#define VM_POOL_MARKER    0x504f4f4cu
#define MAP_FAILED        ((void *) -1)
#define MAP_PRIVATE       0x02
#define MAP_ANONYMOUS     0x20
#define MAP_NORESERVE     0x4000
/* MAP_FIXED_NOREPLACE fails instead of unmapping an existing range. Never use MAP_FIXED. */
#define MAP_FIXED_NOREPLACE 0x100000
#define PROT_NONE         0
#define PROT_READ         1
#define PROT_WRITE        2
#define PROT_EXEC         4
#define MADV_DONTNEED     4

/* Opaque glibc i386 mutex; all-zero is PTHREAD_MUTEX_INITIALIZER (the runtime is -nostdinc). */
typedef union { char _s[40]; long _align; } ui_mutex_t;
#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER {{0}}
#endif
extern int pthread_mutex_lock(ui_mutex_t *);
extern int pthread_mutex_unlock(ui_mutex_t *);
S void SetLastError(u32 e);

typedef struct {
    u32 base, size, allocbase, allocprot, state, prot, type;
} VmRegion;
typedef struct { u32 base, size; int prot, shared; } VmMap;
#define VM_REGION_CHUNK 256u
static VmRegion *g_vm_regions; /* cresce a chunk sotto g_vm_mutex; gli indirizzi non escono dal lock */
static u32 g_vm_region_cap, g_vm_n, g_vm_base, g_vm_size, g_vm_used, g_vm_highwater, g_vm_calls;
static int vm_regions_reserve(u32 needed) {
    u32 cap;
    VmRegion *regions;
    if (needed > VM_REGION_CAP) return 0;
    if (needed <= g_vm_region_cap) return 1;
    cap = (g_vm_region_cap + VM_REGION_CHUNK - 1u) & ~(VM_REGION_CHUNK - 1u);
    if (!cap) cap = VM_REGION_CHUNK;
    while (cap < needed) {
        if (cap > VM_REGION_CAP - VM_REGION_CHUNK) { cap = VM_REGION_CAP; break; }
        cap += VM_REGION_CHUNK;
    }
    regions = (VmRegion *)realloc(g_vm_regions, (ulong)cap * sizeof *regions);
    if (!regions) return 0;
    g_vm_regions = regions; g_vm_region_cap = cap;
    return 1;
}
typedef struct { u32 base, size; } VmPoolEntry;
static VmPoolEntry g_vm_pool[VM_POOL_CAP];
static u32 g_vm_pool_n, g_vm_pool_bytes;
/* Invalidates Storm's mapped-range cache when VM permissions or allocations change. */
volatile uint32_t g_vm_generation;
static ui_mutex_t g_vm_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_vm_ready;
static const VmMap g_vm_candidates[] = {
    {0x20000000u,0x40000000u,0,0}, {0x30000000u,0x40000000u,0,0},
    {0x40000000u,0x40000000u,0,0}, {0x20000000u,0x20000000u,0,0},
    {0x40000000u,0x20000000u,0,0}, {0x60000000u,0x10000000u,0,0},
    {0x20000000u,0x10000000u,0,0}
};

static int prot_to_linux(u32 prot) {
    u32 p = prot & 0xffu;
    switch (p) {
    case 1: return PROT_NONE;                         /* PAGE_NOACCESS */
    case 2: return PROT_READ;                         /* PAGE_READONLY */
    case 4: case 8: return PROT_READ | PROT_WRITE;    /* READWRITE / WRITECOPY */
    case 16: return PROT_EXEC;                        /* PAGE_EXECUTE */
    case 32: return PROT_READ | PROT_EXEC;             /* PAGE_EXECUTE_READ */
    case 64: case 128: return PROT_READ|PROT_WRITE|PROT_EXEC;
    default: return -1;
    }
}
static u32 linux_to_prot(int p) {
    if (!(p & (PROT_READ|PROT_WRITE|PROT_EXEC))) return 1;
    if (p & PROT_EXEC) return (p & PROT_WRITE) ? 64u : 32u;
    return (p & PROT_WRITE) ? 4u : 2u;
}
static int vm_round(u32 addr, u32 size, u32 gran, u32 *base, u32 *rounded) {
    u32 b, off, n;
    if (!size || !gran || (gran & (gran-1))) return 0;
    b = addr & ~(gran-1); off = addr-b;
    if (size > 0xffffffffu-off) return 0;
    n = off + size;
    if (n > 0xffffffffu-(gran-1)) return 0;
    n = (n + gran-1) & ~(gran-1);
    if (!n || b > 0xffffffffu-n) return 0;
    *base=b; *rounded=n; return 1;
}
static int vm_init_locked(void) {
    unsigned int i;
    if (g_vm_ready) return 1;
    for (i=0; i<sizeof g_vm_candidates/sizeof g_vm_candidates[0]; ++i) {
        u32 base=g_vm_candidates[i].base, size=g_vm_candidates[i].size;
        void *r=mmap((void *)(ulong)base,(ulong)size,PROT_NONE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_FIXED_NOREPLACE,-1,0);
        if (r==MAP_FAILED) continue;
        if ((u32)(ulong)r != base || base >= VM_ARENA_MAX || size > VM_ARENA_MAX-base) {
            munmap(r,(ulong)size); continue; /* old kernels may ignore NOREPLACE: verify exact address */
        }
        if (!vm_regions_reserve(1)) { munmap(r,(ulong)size); continue; }
        g_vm_base=base; g_vm_size=size; g_vm_n=1; g_vm_used=0; g_vm_highwater=base;
        g_vm_regions[0].base=base; g_vm_regions[0].size=size;
        g_vm_regions[0].allocbase=0; g_vm_regions[0].allocprot=0;
        g_vm_regions[0].state=MEM_FREE; g_vm_regions[0].prot=0; g_vm_regions[0].type=0;
        g_vm_ready=1; return 1;
    }
    return 0;
}
static int vm_find(u32 addr) {
    u32 lo=0, hi=g_vm_n;
    while (lo<hi) {
        u32 mid=lo+(hi-lo)/2;
        if (addr < g_vm_regions[mid].base) hi=mid;
        else if (addr-g_vm_regions[mid].base >= g_vm_regions[mid].size) lo=mid+1;
        else return (int)mid;
    }
    return -1;
}
static int vm_split(u32 addr) {
    int k=vm_find(addr); u32 i, end;
    VmRegion right;
    if (k<0 || addr==g_vm_regions[k].base) return 1;
    end=g_vm_regions[k].base+g_vm_regions[k].size;
    if (addr>=end || g_vm_n>=VM_REGION_CAP || !vm_regions_reserve(g_vm_n+1u)) return 0;
    right=g_vm_regions[k]; right.base=addr; right.size=end-addr;
    g_vm_regions[k].size=addr-g_vm_regions[k].base;
    for (i=g_vm_n; i>(u32)k+1; --i) g_vm_regions[i]=g_vm_regions[i-1];
    g_vm_regions[k+1]=right; ++g_vm_n; return 1;
}
static int vm_same(const VmRegion *a,const VmRegion *b) {
    return a->base+a->size==b->base && a->allocbase==b->allocbase &&
        a->allocprot==b->allocprot && a->state==b->state && a->prot==b->prot && a->type==b->type;
}
static void vm_coalesce(void) {
    u32 i=0,j;
    while (i+1<g_vm_n) {
        if (vm_same(&g_vm_regions[i],&g_vm_regions[i+1])) {
            g_vm_regions[i].size+=g_vm_regions[i+1].size;
            for (j=i+1;j+1<g_vm_n;++j) g_vm_regions[j]=g_vm_regions[j+1];
            --g_vm_n;
        } else ++i;
    }
}
static int vm_split_range(u32 base,u32 size) {
    if (!size || base<g_vm_base || base>=g_vm_base+g_vm_size || size>g_vm_base+g_vm_size-base) return 0;
    if (!vm_split(base) || !vm_split(base+size)) { vm_coalesce(); return 0; }
    return 1;
}
static int vm_range_alloc(u32 base,u32 size,u32 *allocbase) {
    u32 pos=base, end=base+size; int k,first=1;
    while (pos<end) {
        k=vm_find(pos); if (k<0) return 0;
        if (g_vm_regions[k].state==MEM_FREE) return 0;
        if (first) { *allocbase=g_vm_regions[k].allocbase; first=0; }
        else if (g_vm_regions[k].allocbase!=*allocbase) return 0;
        if (g_vm_regions[k].base+g_vm_regions[k].size<=pos) return 0;
        pos=g_vm_regions[k].base+g_vm_regions[k].size;
    }
    return !first;
}
static void vm_mark_range(u32 base,u32 size,u32 state,u32 prot);
static int vm_find_free(u32 requested,u32 size,u32 *base) {
    u32 i;
    if (requested) {
        int k=vm_find(requested); u32 end;
        if (k<0 || g_vm_regions[k].state!=MEM_FREE) return 0;
        if(g_vm_regions[k].type==VM_POOL_MARKER) {
            for(i=0;i<g_vm_pool_n;++i)if(g_vm_pool[i].base==requested&&g_vm_pool[i].size==size){*base=requested;return 1;}
            return 0;
        }
        end=g_vm_regions[k].base+g_vm_regions[k].size;
        if (requested>end || size>end-requested) return 0;
        *base=requested; return 1;
    }
    /* Prefer an exact pooled block: it remains RW and guest-mapped, so reuse is syscall-free. */
    for (i=0;i<g_vm_pool_n;++i) if(g_vm_pool[i].size==size) { *base=g_vm_pool[i].base; return 1; }
    for (i=0;i<g_vm_n;++i) if (g_vm_regions[i].state==MEM_FREE && g_vm_regions[i].type!=VM_POOL_MARKER) {
        u32 b=(g_vm_regions[i].base+VM_GRANULARITY-1)&~(VM_GRANULARITY-1);
        if (b>=g_vm_regions[i].base && size<=g_vm_regions[i].base+g_vm_regions[i].size-b) { *base=b; return 1; }
    }
    return 0;
}
static int vm_pool_take(u32 base,u32 size) {
    u32 i;
    for(i=0;i<g_vm_pool_n;++i) if(g_vm_pool[i].base==base && g_vm_pool[i].size==size) {
        g_vm_pool_bytes-=size;
        g_vm_pool[i]=g_vm_pool[--g_vm_pool_n];
        return 1;
    }
    return 0;
}
static void vm_pool_evict_oldest(void) {
    u32 base,size,i;
    if(!g_vm_pool_n)return;
    base=g_vm_pool[0].base; size=g_vm_pool[0].size;
    for(i=1;i<g_vm_pool_n;++i)g_vm_pool[i-1]=g_vm_pool[i];
    --g_vm_pool_n; g_vm_pool_bytes-=size;
    (void)madvise((void *)(ulong)base,(ulong)size,MADV_DONTNEED);
    (void)mprotect((void *)(ulong)base,(ulong)size,PROT_NONE);
    vm_mark_range(base,size,MEM_FREE,0);
    arm_shadow_set_range(base,size,0);
    ++g_vm_generation;
}
static int vm_pool_add(u32 base,u32 size) {
    u32 i,end=base+size;
    if(!size || size>VM_POOL_BUDGET || g_vm_pool_n>=VM_POOL_CAP)return 0;
    for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end)
        g_vm_regions[i].type=VM_POOL_MARKER;
    g_vm_pool[g_vm_pool_n].base=base;g_vm_pool[g_vm_pool_n].size=size;
    ++g_vm_pool_n;g_vm_pool_bytes+=size;
    while(g_vm_pool_bytes>VM_POOL_BUDGET)vm_pool_evict_oldest();
    return 1;
}
static int vm_reserve_locked(u32 requested,u32 size,u32 allocprot,u32 state,int prot,u32 *outbase,int *pool_reused) {
    u32 base,end,i;
    int pooled=0;
    if(pool_reused)*pool_reused=0;
    if (!vm_find_free(requested,size,&base)) return 0;
    end=base+size;
    if(!vm_split(base) || !vm_split(end)) { vm_coalesce(); return 0; }
    if(state==MEM_COMMIT_STATE) {
        pooled=vm_pool_take(base,size);
        if(!pooled && mprotect((void *)(ulong)base,(ulong)size,prot)!=0)return 0;
        if(pooled)memset((void *)(ulong)base,0,(ulong)size);
        if(pool_reused)*pool_reused=pooled;
    }
    for (i=0;i<g_vm_n;++i) if(g_vm_regions[i].base>=base && g_vm_regions[i].base<end) {
        VmRegion *r=&g_vm_regions[i];
        if (r->state!=MEM_FREE) { vm_coalesce(); if(state==MEM_COMMIT_STATE)mprotect((void*)(ulong)base,(ulong)size,PROT_NONE); return 0; }
        r->allocbase=base; r->allocprot=allocprot; r->state=state;
        r->prot=(state==MEM_COMMIT_STATE)?(u32)linux_to_prot(prot):0; r->type=MEM_PRIVATE;
    }
    g_vm_used+=size; if(end>g_vm_highwater)g_vm_highwater=end;
    if(outbase)*outbase=base;
    vm_coalesce(); return 1;
}
static void vm_mark_range(u32 base,u32 size,u32 state,u32 prot) {
    u32 i,end=base+size;
    for(i=0;i<g_vm_n;++i) if(g_vm_regions[i].base>=base && g_vm_regions[i].base<end) {
        g_vm_regions[i].state=state; g_vm_regions[i].prot=prot;
        if(state==MEM_FREE){g_vm_regions[i].allocbase=0;g_vm_regions[i].allocprot=0;g_vm_regions[i].type=0;}
    }
    vm_coalesce();
}
static int vm_proc_line(char *line,u32 *lo,u32 *hi,int *prot,int *shared) {
    char *p=line; u32 a=0,b=0; int n=0;
    while ((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')) {
        u32 d=(*p>='0'&&*p<='9')?(u32)(*p-'0'):((*p>='a'&&*p<='f')?(u32)(*p-'a'+10):(u32)(*p-'A'+10));
        if(a>0x0fffffffu)return 0;
        a=(a<<4)|d; ++p; ++n;
    }
    if(!n||*p++!='-')return 0;
    n=0;
    while ((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')) {
        u32 d=(*p>='0'&&*p<='9')?(u32)(*p-'0'):((*p>='a'&&*p<='f')?(u32)(*p-'a'+10):(u32)(*p-'A'+10));
        if(b>0x0fffffffu)return 0;
        b=(b<<4)|d; ++p; ++n;
    }
    if(!n||b<=a)return 0;
    while(*p==' ')++p;
    *prot=0; if(p[0]=='r')*prot|=PROT_READ; if(p[1]=='w')*prot|=PROT_WRITE; if(p[2]=='x')*prot|=PROT_EXEC;
    *shared=(p[3]=='s'); *lo=a; *hi=b; return 1;
}
static void vm_mbi_free(MEMBASIC_T *m,u32 base,u32 size) {
    m->base=base;m->allocbase=0;m->allocprot=0;m->size=size;m->state=MEM_FREE;m->prot=0;m->type=0;
}
static void vm_mbi_map(MEMBASIC_T *m,u32 base,u32 size,int prot,int shared) {
    m->base=base;m->allocbase=base;m->allocprot=linux_to_prot(prot);m->size=size;
    m->state=MEM_COMMIT_STATE;m->prot=linux_to_prot(prot);m->type=shared?MEM_MAPPED:MEM_PRIVATE;
}
static int vm_query_proc(u32 addr,MEMBASIC_T *m) {
    char chunk[1024],line[512]; int fd,used=0; long n; u32 prev=0,lo,hi; int prot,shared;
    fd=open("/proc/self/maps",0); if(fd<0)return 0;
    while((n=read(fd,chunk,sizeof chunk))>0) {
        long j;
        for(j=0;j<n;++j) {
            char c=chunk[j];
            if(c!='\n') { if(used<(int)sizeof line-1)line[used++]=c; continue; }
            line[used]=0; used=0;
            if(!vm_proc_line(line,&lo,&hi,&prot,&shared))continue;
            if(addr>=lo && addr<hi){vm_mbi_map(m,lo,hi-lo,prot,shared);close(fd);return 1;}
            if(addr<lo){vm_mbi_free(m,prev,lo-prev);close(fd);return 1;}
            prev=hi;
        }
    }
    close(fd);
    if(prev<=addr){vm_mbi_free(m,prev,0xffffffffu-prev);return 1;}
    return 0;
}
static void vm_trace_hex(char *b,int *i,const char *label,u32 v) {
    const char *p=label; while(*p && *i<230)b[(*i)++]=*p++;
    if(*i<230)*i+=hx_(b+*i,v);
}
static void vm_trace_line(const char *api,u32 addr,u32 size,u32 type,u32 eff,u32 prot,u32 ret,u32 err) {
    char b[256]; int i=0; const char *p; u32 calls=g_vm_calls;
    if(!g_trace)return;
    p=api;while(*p&&i<230)b[i++]=*p++;
    vm_trace_hex(b,&i," addr=",addr);vm_trace_hex(b,&i," size=",size);
    vm_trace_hex(b,&i," type=",type);vm_trace_hex(b,&i," eff=",eff);
    vm_trace_hex(b,&i," protect=",prot);vm_trace_hex(b,&i," ret=",ret);
    vm_trace_hex(b,&i," err=",err);vm_trace_hex(b,&i," used=",g_vm_used);
    vm_trace_hex(b,&i," calls=",calls);b[i++]='\n';write(2,b,(ulong)i);
    if((calls&1023u)==0) {
        i=0;p="[vm] SUMMARY";while(*p)b[i++]=*p++;
        vm_trace_hex(b,&i," base=",g_vm_base);vm_trace_hex(b,&i," span=",g_vm_size);
        vm_trace_hex(b,&i," used=",g_vm_used);vm_trace_hex(b,&i," highwater=",g_vm_highwater);
        vm_trace_hex(b,&i," calls=",calls);b[i++]='\n';write(2,b,(ulong)i);
    }
}

S void *VirtualAlloc(void *address,u32 size,u32 type,u32 protect) {
    u32 a=(u32)(ulong)address,base=0,n=0,eff=type,err=0,ret=0,allocbase=0;
    int lp=prot_to_linux(protect),ok=0,pool_reused=0;
    pthread_mutex_lock(&g_vm_mutex);
    ++g_vm_calls;
    if(!vm_init_locked()) err=ERROR_NOT_ENOUGH_MEMORY;
    else if(!size||lp<0) err=ERROR_INVALID_PARAMETER;
    else if(type & MEM_RESERVE) {
        if(type & ~(MEM_RESERVE|MEM_COMMIT))err=ERROR_INVALID_PARAMETER;
        else if(a) {
            if(!vm_round(a,size,VM_GRANULARITY,&base,&n)||base<g_vm_base||base>=g_vm_base+g_vm_size||n>g_vm_base+g_vm_size-base)err=ERROR_INVALID_ADDRESS;
            else ok=vm_reserve_locked(base,n,protect,(type&MEM_COMMIT)?MEM_COMMIT_STATE:MEM_RESERVE_STATE,lp,&base,&pool_reused);
        } else {
            n=(size+VM_PAGE-1)&~(VM_PAGE-1);
            if(n<size||!vm_reserve_locked(0,n,protect,(type&MEM_COMMIT)?MEM_COMMIT_STATE:MEM_RESERVE_STATE,lp,&base,&pool_reused))err=ERROR_NOT_ENOUGH_MEMORY;
            else ok=1;
        }
        if(!ok&&!err)err=ERROR_NOT_ENOUGH_MEMORY;
    } else if(type==MEM_COMMIT && !a) {
        eff=type|MEM_RESERVE; n=(size+VM_PAGE-1)&~(VM_PAGE-1);
        if(n<size||!vm_reserve_locked(0,n,protect,MEM_COMMIT_STATE,lp,&base,&pool_reused))err=ERROR_NOT_ENOUGH_MEMORY;
        else ok=1;
    } else if(type==MEM_COMMIT && a) {
        if(!vm_round(a,size,VM_PAGE,&base,&n)||!vm_split_range(base,n)||!vm_range_alloc(base,n,&allocbase))err=ERROR_INVALID_ADDRESS;
        else {
            u32 i,end=base+n; int fail=0;
            for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end&&g_vm_regions[i].state==MEM_RESERVE_STATE)
                if(mprotect((void*)(ulong)g_vm_regions[i].base,(ulong)g_vm_regions[i].size,lp)!=0){fail=1;break;}
            if(fail){for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end&&g_vm_regions[i].state==MEM_RESERVE_STATE)mprotect((void*)(ulong)g_vm_regions[i].base,(ulong)g_vm_regions[i].size,PROT_NONE);vm_coalesce();err=ERROR_NOT_ENOUGH_MEMORY;}
            else { for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end&&g_vm_regions[i].state==MEM_RESERVE_STATE){g_vm_regions[i].state=MEM_COMMIT_STATE;g_vm_regions[i].prot=linux_to_prot(lp);}vm_coalesce();ok=1; }
        }
    } else err=ERROR_INVALID_PARAMETER;
    if(ok)ret=base;
    vm_trace_line("[vm] VirtualAlloc",a,size,type,eff,protect,ret,err);
    if(ret && !pool_reused) {
        uint8_t shadow=(type & MEM_COMMIT)?(uint8_t)(ARM_SHADOW_GUEST|lp):0u;
        arm_shadow_set_range(base,n,shadow);
        ++g_vm_generation;
    }
    pthread_mutex_unlock(&g_vm_mutex);
    if(err)SetLastError(err);
    return (void *)(ulong)ret;
}
S int VirtualFree(void *address,u32 size,u32 type) {
    u32 a=(u32)(ulong)address,base=0,n=0,eff=type,err=0,ret=0,allocbase=0,traceprot=0;
    int pool_cached=0;
    pthread_mutex_lock(&g_vm_mutex); ++g_vm_calls;
    if(!vm_init_locked()||!a)err=ERROR_INVALID_ADDRESS;
    else if(type==MEM_RELEASE) {
        int k=vm_find(a); u32 i,total=0;
        if(size||k<0||g_vm_regions[k].state==MEM_FREE||g_vm_regions[k].allocbase!=a)err=ERROR_INVALID_ADDRESS;
        else {
            { int poolable=1;
              for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].allocbase==a) {
                  total+=g_vm_regions[i].size;
                  if(g_vm_regions[i].state!=MEM_COMMIT_STATE||g_vm_regions[i].prot!=4u)poolable=0;
              }
              if(!total)err=ERROR_INVALID_ADDRESS;
              else if(poolable && g_vm_regions[k].allocprot==64u && vm_pool_add(a,total)) {
                pool_cached=1;
                vm_mark_range(a,total,MEM_FREE,0);
                /* vm_mark_range clears the marker: restore it while retaining RW + guest flags. */
                for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=a&&g_vm_regions[i].base<a+total)
                    g_vm_regions[i].type=VM_POOL_MARKER;
                g_vm_used-=total;ret=1;
              } else if(madvise((void*)(ulong)a,(ulong)total,MADV_DONTNEED)!=0||mprotect((void*)(ulong)a,(ulong)total,PROT_NONE)!=0)err=ERROR_INVALID_ADDRESS;
              else { vm_mark_range(a,total,MEM_FREE,0);g_vm_used-=total;arm_shadow_set_range(a,total,0);ret=1; }
            }
        }
        traceprot=(k>=0)?g_vm_regions[k].prot:0;
    } else if(type==MEM_DECOMMIT) {
        if(!vm_round(a,size,VM_PAGE,&base,&n)||!vm_split_range(base,n)||!vm_range_alloc(base,n,&allocbase))err=ERROR_INVALID_ADDRESS;
        else {
            int k=vm_find(base); if(k>=0)traceprot=g_vm_regions[k].prot;
            if(madvise((void*)(ulong)base,(ulong)n,MADV_DONTNEED)!=0||mprotect((void*)(ulong)base,(ulong)n,PROT_NONE)!=0)err=ERROR_INVALID_ADDRESS;
            else {vm_mark_range(base,n,MEM_RESERVE_STATE,0);arm_shadow_set_range(base,n,0);ret=1;}
        }
    } else err=ERROR_INVALID_PARAMETER;
    vm_trace_line("[vm] VirtualFree",a,size,type,eff,traceprot,ret,err);
    if(ret && !pool_cached) ++g_vm_generation;
    pthread_mutex_unlock(&g_vm_mutex); if(err)SetLastError(err); return (int)ret;
}
S int VirtualProtect(void *address,u32 size,u32 newprotect,u32 *oldprotect) {
    u32 a=(u32)(ulong)address,base=0,n=0,allocbase=0,old=0,err=0,ret=0; int lp=prot_to_linux(newprotect);
    pthread_mutex_lock(&g_vm_mutex);++g_vm_calls;
    if(!vm_init_locked()||!a||!size||lp<0)err=ERROR_INVALID_PARAMETER;
    else if(a>=g_vm_base&&a<g_vm_base+g_vm_size) {
        if(!vm_round(a,size,VM_PAGE,&base,&n)||!vm_split_range(base,n)||!vm_range_alloc(base,n,&allocbase))err=ERROR_INVALID_ADDRESS;
        else {
            u32 i,end=base+n;int valid=1;int k=vm_find(base);if(k>=0)old=g_vm_regions[k].prot;
            for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end&&g_vm_regions[i].state!=MEM_COMMIT_STATE)valid=0;
            if(!valid)err=ERROR_INVALID_ADDRESS;
            else if(mprotect((void*)(ulong)base,(ulong)n,lp)!=0)err=ERROR_INVALID_ADDRESS;
            else {for(i=0;i<g_vm_n;++i)if(g_vm_regions[i].base>=base&&g_vm_regions[i].base<end)g_vm_regions[i].prot=linux_to_prot(lp);vm_coalesce();ret=1;}
        }
    } else {
        MEMBASIC_T mbi;
        if(!vm_round(a,size,VM_PAGE,&base,&n)||!vm_query_proc(base,&mbi)||base<mbi.base||n>mbi.base+mbi.size-base)err=ERROR_INVALID_ADDRESS;
        else {old=mbi.prot;if(mprotect((void*)(ulong)base,(ulong)n,lp)!=0)err=ERROR_INVALID_ADDRESS;else ret=1;}
    }
    if(ret&&oldprotect)*oldprotect=old;
    vm_trace_line("[vm] VirtualProtect",a,size,0,0,newprotect,ret,err);
    if(ret) {
        uint8_t guest=arm_shadow_check_range(base,n,ARM_SHADOW_GUEST)?ARM_SHADOW_GUEST:0u;
        arm_shadow_set_range(base,n,(uint8_t)(guest|lp));
        ++g_vm_generation;
    }
    pthread_mutex_unlock(&g_vm_mutex);if(err)SetLastError(err);return (int)ret;
}
S u32 VirtualQuery(void *address,MEMBASIC_T *mbi,u32 len) {
    u32 a=(u32)(ulong)address,err=0,ret=0; int k;
    pthread_mutex_lock(&g_vm_mutex);++g_vm_calls;
    if(!mbi||len<sizeof *mbi)err=ERROR_INVALID_PARAMETER;
    else {
        memset(mbi,0,sizeof *mbi);
        if(!vm_init_locked())err=ERROR_NOT_ENOUGH_MEMORY;
        else if(a>=g_vm_base&&a<g_vm_base+g_vm_size) {
            k=vm_find(a);
            if(k<0)err=ERROR_INVALID_ADDRESS;
            else {VmRegion *r=&g_vm_regions[k];mbi->base=r->base;mbi->allocbase=r->allocbase;mbi->allocprot=r->allocprot;mbi->size=r->size;mbi->state=r->state;mbi->prot=r->prot;mbi->type=r->type;ret=sizeof *mbi;}
        } else if(vm_query_proc(a,mbi))ret=sizeof *mbi;
        else err=ERROR_INVALID_ADDRESS;
    }
    pthread_mutex_unlock(&g_vm_mutex);if(err)SetLastError(err);return ret;
}

/* --- avvio / ambiente --- */
S char *GetCommandLineA(void) { return g_cmdline; }
S char *GetEnvironmentStrings(void) { return g_envA; }
/* Il CRT legge l'ambiente anche in WIDE; un blocco vuoto faceva fallire l'init con R6009
 * "not enough space for environment" (trappola pagata in LOLA). Si converte il blocco ANSI. */
extern char *teb_envA(void);
S u16 *GetEnvironmentStringsW(void) {
    static u16 wenv[512];
    if (!wenv[0]) {
        char *a = teb_envA(); int i = 0, j = 0;
        while ((a[i] || a[i + 1]) && j < 500) {
            wenv[j++] = (u16) (u8) a[i++];
            if (!a[i - 1]) { /* separatore */ }
        }
        wenv[j] = 0; wenv[j + 1] = 0;
    }
    return wenv;
}
S int   FreeEnvironmentStringsA(char *p) { (void) p; return 1; }
S int   FreeEnvironmentStringsW(u16 *p) { (void) p; return 1; }
S void  GetStartupInfoA(STARTUPINFOA_T *si) { if (si) *si = g_si; }
S int   GetVersionExA(OSVERSIONINFOA_T *v) {
    if (!v) return 0;
    v->major = 4; v->minor = 10; v->build = 1998;   /* Win98: ambiente nativo del gioco */
    v->platform = 1;                                 /* VER_PLATFORM_W32_WINDOWS */
    v->csd[0] = 0;
    return 1;
}
S u32   GetVersion(void) { return 0x04000000u; }     /* 4.0 */
S void  GetSystemInfo(SYSTEMINFO_T *si) {
    u32 n = cpu_count_();
    if (g_trace) reasm_elf_log("[trace] GetSystemInfo\n");
    if (!si) return;
    memset(si, 0, sizeof *si);
    si->arch = 0;                                  /* PROCESSOR_ARCHITECTURE_INTEL */
    si->page = 0x1000;
    si->minapp = 0x00010000u;
    si->maxapp = 0x7FFFFFFFu;
    si->activeproc = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    si->nproc = n;
    /* Il gioco considera "CPU vecchia" se dwProcessorType < 0x1E6 (486): 586 = Pentium. */
    si->proctype = 586;
    si->allocgran = 0x10000;
    si->level = 6;
    si->rev = 0x0303;
}
S void  GlobalMemoryStatus(MEMORYSTATUS_T *m) {
    if (g_trace) reasm_elf_log("[trace] GlobalMemoryStatus\n");
    if (!m) return;
    unsigned long tot = meminfo_kb_("MemTotal:");
    unsigned long av = meminfo_kb_("MemAvailable:");
    unsigned long swap = meminfo_kb_("SwapTotal:");
    memset(m, 0, sizeof *m);
    m->len = sizeof *m;                              /* 32, come Windows */
    if (!tot) tot = 512ul * 1024;                    /* fallback 512 MB */
    if (!av) av = tot / 2;
    m->totalphys = (u32) (tot * 1024ul);
    m->availphys = (u32) (av * 1024ul);
    m->totalpage = (u32) ((tot + swap) * 1024ul);
    m->availpage = (u32) ((av + swap) * 1024ul);
    m->totalvirt = 2048u << 20;
    m->availvirt = (2u << 30) - m->totalphys;
    m->memload = (u32) (100ul - (av * 100ul) / (tot ? tot : 1));
}
S void *GetModuleHandleA(const char *n) {
    unsigned ra = (unsigned) (ulong) __builtin_return_address(0);
    void *r;
    if (n) {
        unsigned int h = win32_ui_module_handle(n);
        r = h ? (void *) (ulong) h : reasm_module_handle_cdecl(n);
    } else {
        r = reasm_module_handle_cdecl(n);
    }
    if (g_trace) {
        reasm_elf_log("[trace] GetModuleHandleA("); reasm_elf_log(n ? n : "NULL");
        reasm_elf_log(") da "); reasm_elf_log_hex(ra); reasm_elf_log(" -> ");
        reasm_elf_log_hex((unsigned) (ulong) r); reasm_elf_log("\n");
    }
    return r;
}
static char g_modpath[320];
S u32   GetModuleFileNameA(void *m, char *b, u32 n) {
    u32 i = 0;
    (void) m;
    if (!g_modpath[0]) {
        char cwd[260];
        int l;
        if (!getcwd(cwd, sizeof cwd)) { cwd[0] = '/'; cwd[1] = 0; }
        reasm_unix_to_win(cwd, g_modpath, sizeof g_modpath);
        l = (int) strlen(g_modpath);
        if (l < (int) sizeof g_modpath - 16) {
            const char *nm = "\\StarCraft.exe";
            while (*nm) g_modpath[l++] = *nm++;
            g_modpath[l] = 0;
        }
    }
    while (g_modpath[i] && i + 1 < n) { b[i] = g_modpath[i]; ++i; }
    b[i] = 0;
    return i;
}
/* P9: smackw32.dll nativa (smack_elf.c). Dichiarazioni DEBOLI: se il modulo non è nel link, o se
 * SC_SMACK != 1, i puntatori sono NULL e il comportamento resta quello di prima. */
extern void *win32_smack_load_library(const char *) __attribute__((weak));
extern void *win32_smack_get_proc(void *, const char *) __attribute__((weak));
/* AUDIO (com_dsound.c, TASK-audio 2026-10-11): stesso schema. Misurato: il gioco chiede l'audio a
 * ogni avvio (sub_4BCB70: LoadLibraryA("dsound.dll") + GetProcAddress("DirectSoundCreate")); senza
 * questi due hook la risoluzione ritornava 0 e l'init audio abortiva. */
extern void *win32_dsound_load_library(const char *) __attribute__((weak));
extern void *win32_dsound_get_proc(void *, const char *) __attribute__((weak));
extern void com_dsound_note_resolve(unsigned int) __attribute__((weak));
/* P8: in-process standard.snp provider; battle.snp remains a resource-only module with no exports. */
extern void *win32_snp_load_library(const char *) __attribute__((weak));
extern void *win32_snp_get_proc(void *, const char *) __attribute__((weak));
S void *LoadLibraryA(const char *n) {
    unsigned int h;
    if (!n) return 0;
    if (win32_smack_load_library) {          /* P9: PRIMA del contenitore di risorse: se forniamo
                                              * smackw32.dll nativa, quel percorso non deve vincere. */
        void *s = win32_smack_load_library(n);
        if (s) return s;
    }
    if (win32_dsound_load_library) {         /* AUDIO: dsound.dll nativa, prima del contenitore */
        void *s = win32_dsound_load_library(n);
        if (s) return s;
    }
    if (win32_snp_load_library) {
        void *s = win32_snp_load_library(n);
        if (s) return s;
    }
    {   char up[300];
        reasm_win_path(n, up, sizeof up);
        h = win32_ui_load_library(up);
    }
    if (h) return (void *) (ulong) h;
    return reasm_load_library_cdecl(n);
}
static int is_snp_export_name(const char *n) {
    return n && ((n[0] == 'S' && n[1] == 'n' && n[2] == 'p' && n[3] == 'Q' &&
                  n[4] == 'u' && n[5] == 'e' && n[6] == 'r' && n[7] == 'y' && n[8] == 0) ||
                 (n[0] == 'S' && n[1] == 'n' && n[2] == 'p' && n[3] == 'B' &&
                  n[4] == 'i' && n[5] == 'n' && n[6] == 'd' && n[7] == 0));
}
S void *GetProcAddress(void *m, const char *n) {
    void *p;
    /* SnpQuery/SnpBind are module exports, not global Win32 imports. Resolve them only for the
     * native standard.snp module and hand the game the generated guest-ABI bridge thunk. */
    if (win32_snp_get_proc) { p = win32_snp_get_proc(m, n); if (p) return p; }
    if (is_snp_export_name(n)) return 0;
#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)
    /* Translated guest code must call through a guest-ABI thunk, not the C symbol. This is true
     * on both ARM and i386: even though i386 stdcall uses the same stack ABI, LLASM entries take
     * %_cpu in ECX and need the runtime bridge to marshal guest-stack arguments. */
    { extern void *arm_bridge_lookup_thunk(const char *) __attribute__((weak));
      if (arm_bridge_lookup_thunk) { p = arm_bridge_lookup_thunk(n); if (p) return p; } }
#endif
    if (win32_smack_get_proc) {
        p = win32_smack_get_proc(m, n);
        if (p) return p;
    }
    if (win32_dsound_get_proc) {              /* AUDIO: DirectSoundCreate/Create8 */
        p = win32_dsound_get_proc(m, n);
        if (p) {
            /* Il chiamante della risoluzione e' il call-site del gioco (atteso 0x004BB64C):
             * e' il dato che distingue "risolta ma mai chiamata" da "risolta e chiamata". */
            if (com_dsound_note_resolve)
                com_dsound_note_resolve((unsigned int) (ulong) __builtin_return_address(0));
            return p;
        }
    }
    p = win32_ui_get_proc((ulong) m, n);              /* ddraw/dsound: shim linkati, non import */
    if (p) return p;
    return reasm_get_proc_cdecl(m, n);
}
S int   FreeLibrary(void *m) { (void) m; return 1; }

/* --- errori / TLS --- */
static unsigned int g_err26_gets, g_err26_sets;
S u32 GetLastError(void) {
#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)
    /* Port runtime: use the emulated guest TEB, never Linux host %fs (notably on worker threads). */
    u32 v = X86_ReadFsDword(0x34);
#else
    u32 v; __asm__ volatile ("movl %%fs:0x34, %0" : "=r"(v));
#endif
    if (g_trace && v == 0x26 && __sync_fetch_and_add(&g_err26_gets, 1) < 16) reasm_trace2("[err26] GetLastError caller=", (u32)(ulong)__builtin_return_address(0), " value=", v);
    return v;
}
S void SetLastError(u32 e) {
#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)
    X86_WriteFsDword(0x34, (uint32_t) e);
#else
    __asm__ volatile ("movl %0, %%fs:0x34" :: "r"(e));
#endif
    if (g_trace && e == 0x26 && __sync_fetch_and_add(&g_err26_sets, 1) < 16) reasm_trace2("[err26] SetLastError caller=", (u32)(ulong)__builtin_return_address(0), " value=", e);
}
S u32  TlsAlloc(void) { return (u32) g_tls_n++; }
S int  TlsFree(u32 i) { if (i < 64) g_tls[i] = 0; return 1; }
S void *TlsGetValue(u32 i) { return i < 64 ? g_tls[i] : 0; }
S int  TlsSetValue(u32 i, void *v) { if (i < 64) g_tls[i] = v; return 1; }

/* --- sincronizzazione --- */
S void InitializeCriticalSection(void *cs);
S int  InitializeCriticalSectionAndSpinCount(void *cs, u32 spin);
S void EnterCriticalSection(void *cs);
S void LeaveCriticalSection(void *cs);
S void DeleteCriticalSection(void *cs);
S i32  InterlockedExchange(void *t, i32 v) { return __sync_lock_test_and_set((i32 *) t, v); }
S i32  InterlockedIncrement(void *t) { return __sync_add_and_fetch((i32 *) t, 1); }
S i32  InterlockedDecrement(void *t) { return __sync_sub_and_fetch((i32 *) t, 1); }
/* --- SEZIONI CRITICHE REALI ---
 * storm importa Initialize/Enter/Leave/DeleteCriticalSection e le usa per proteggere i suoi heap
 * mentre il thread asincrono di lettura MPQ gira in parallelo al thread principale. Con Enter/Leave
 * no-op i due thread corrompevano l'heap: SIGSEGV in SMemFree (0x1502065E, thread di lettura,
 * lettura a 2 byte PRIMA di un blocco). CRITICAL_SECTION in Windows e' 24 byte e RICORSIVA per
 * thread: qui i 24 byte allocati dal chiamante restano intatti e lo stato (mutex + proprietario +
 * conteggio) sta in una tabella indicizzata dall'indirizzo della sezione. Nessuna API di Windows
 * viene simulata con un no-op: D19 vale anche qui. */
typedef union { char _s[64]; long long _a; } ui_cond_t;
extern int pthread_mutex_lock(ui_mutex_t *);
extern int pthread_mutex_unlock(ui_mutex_t *);
extern int pthread_cond_wait(ui_cond_t *, ui_mutex_t *);
extern int pthread_cond_timedwait(ui_cond_t *, ui_mutex_t *, const void *);
extern int pthread_cond_broadcast(ui_cond_t *);
extern int clock_gettime(int, void *);

typedef struct { void *cs; int used; unsigned long owner; int count; ui_mutex_t m; } UiCs;
#define CS_CAP 256u
#define CS_HASH_CAP 512u
#define CS_TOMBSTONE ((UiCs *)(ulong)1)
static UiCs g_cs[CS_CAP];
static UiCs *g_cs_hash[CS_HASH_CAP];

static u32 cs_hash(void *cs) {
    ulong x = (ulong) cs;
    x ^= x >> 17; x *= (ulong) 0xed5ad4bbu;
    x ^= x >> 11; x *= (ulong) 0xac4c1b51u;
    x ^= x >> 15;
    return (u32)x & (CS_HASH_CAP - 1u);
}

/* La ricerca comune legge solo gli slot atomici: nessun lock globale né scan del pool. */
static UiCs *cs_find(void *cs) {
    u32 i, n = cs_hash(cs);
    for (i = 0; i < CS_HASH_CAP; ++i) {
        UiCs *e = __atomic_load_n(&g_cs_hash[(n + i) & (CS_HASH_CAP - 1u)], __ATOMIC_RELAXED);
        if (!e) return 0;
        if (e != CS_TOMBSTONE && __atomic_load_n(&e->cs, __ATOMIC_RELAXED) == cs) return e;
    }
    return 0;
}

static UiCs *cs_of(void *cs, int create) {
    UiCs *e;
    u32 i, n;
    if ((e = cs_find(cs)) != 0 || !create) return e;
    for (;;) {
        int k;
        for (k = 0; k < (int)CS_CAP; ++k) {
            int expected = 0;
            if (__atomic_compare_exchange_n(&g_cs[k].used, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                e = &g_cs[k];
                __atomic_store_n(&e->cs, cs, __ATOMIC_RELAXED);
                __atomic_store_n(&e->owner, 0ul, __ATOMIC_RELAXED);
                __atomic_store_n(&e->count, 0, __ATOMIC_RELAXED);
                n = cs_hash(cs);
                for (i = 0; i < CS_HASH_CAP; ++i) {
                    UiCs **slot = &g_cs_hash[(n + i) & (CS_HASH_CAP - 1u)];
                    UiCs *value = __atomic_load_n(slot, __ATOMIC_RELAXED);
                    if (value && value != CS_TOMBSTONE && __atomic_load_n(&value->cs, __ATOMIC_RELAXED) == cs) {
                        __atomic_store_n(&e->cs, (void *)0, __ATOMIC_RELAXED);
                        __atomic_store_n(&e->used, 0, __ATOMIC_RELEASE);
                        return value;
                    }
                    if (!value || value == CS_TOMBSTONE) {
                        UiCs *expected_slot = value;
                        if (__atomic_compare_exchange_n(slot, &expected_slot, e, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return e;
                        if (expected_slot && expected_slot != CS_TOMBSTONE &&
                            __atomic_load_n(&expected_slot->cs, __ATOMIC_RELAXED) == cs) {
                            __atomic_store_n(&e->cs, (void *)0, __ATOMIC_RELAXED);
                            __atomic_store_n(&e->used, 0, __ATOMIC_RELEASE);
                            return expected_slot;
                        }
                    }
                }
                __atomic_store_n(&e->cs, (void *)0, __ATOMIC_RELAXED);
                __atomic_store_n(&e->used, 0, __ATOMIC_RELEASE);
                return 0;
            }
        }
        return 0; /* pool pieno: stessa semantica precedente */
    }
}

S void InitializeCriticalSection(void *cs) { (void) cs_of(cs, 1); }
S int  InitializeCriticalSectionAndSpinCount(void *cs, u32 spin) { (void) spin; return cs_of(cs, 1) != 0; }
S void DeleteCriticalSection(void *cs) {
    UiCs *e = cs_find(cs);
    if (e) {
        u32 n = cs_hash(cs), i;
        for (i = 0; i < CS_HASH_CAP; ++i) {
            UiCs **slot = &g_cs_hash[(n + i) & (CS_HASH_CAP - 1u)];
            UiCs *value = __atomic_load_n(slot, __ATOMIC_RELAXED);
            if (!value) break;
            if (value == e) {
                UiCs *expected = e;
                if (__atomic_compare_exchange_n(slot, &expected, CS_TOMBSTONE, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                    __atomic_store_n(&e->cs, (void *)0, __ATOMIC_RELAXED);
                    __atomic_store_n(&e->used, 0, __ATOMIC_RELEASE);
                }
                break;
            }
        }
    }
}
S void EnterCriticalSection(void *cs) {
    UiCs *e = cs_of(cs, 1);
    unsigned long me = pthread_self();
    if (!e) return;
    if (__atomic_load_n(&e->owner, __ATOMIC_RELAXED) == me) {
        __atomic_add_fetch(&e->count, 1, __ATOMIC_RELAXED); return;
    }
    pthread_mutex_lock(&e->m);
    __atomic_store_n(&e->owner, me, __ATOMIC_RELAXED);
    __atomic_store_n(&e->count, 1, __ATOMIC_RELAXED);
}
S void LeaveCriticalSection(void *cs) {
    UiCs *e = cs_of(cs, 0);
    if (!e) return;
    if (__atomic_load_n(&e->owner, __ATOMIC_RELAXED) == pthread_self() &&
        __atomic_load_n(&e->count, __ATOMIC_RELAXED) > 1) {
        __atomic_sub_fetch(&e->count, 1, __ATOMIC_RELAXED); return;
    }
    __atomic_store_n(&e->count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&e->owner, 0ul, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&e->m);
}

/* --- EVENTI REALI ---
 * storm legge gli MPQ con un thread asincrono: il worker segnala un EVENTO e il lettore aspetta.
 * Con SetEvent finto e WaitForSingleObject che non aspetta, il lettore vedeva dati incompleti
 * ("ERROR_HANDLE_EOF" -> dialogo fatale "font\font.gid") e il worker dormiva 250 s.
 * pthread_mutex_t/cond_t di glibc sono inizializzabili a ZERO (PTHREAD_*_INITIALIZER), quindi si
 * dichiarano come union opache sovradimensionate (si resta -nostdinc). */

typedef struct UiEv {
    unsigned refs;
    int signaled, manual, closed;
    void *h;
    ui_mutex_t m;
    ui_cond_t c;
    struct UiEv *next;
} UiEv;
static ui_mutex_t g_ev_lock;
static UiEv *g_ev;
static unsigned g_ev_next = 0x5E000001u;
extern int pthread_mutex_destroy(ui_mutex_t *);
extern int pthread_cond_destroy(ui_cond_t *);

static UiEv *ev_get(void *h) {
    UiEv *e;
    pthread_mutex_lock(&g_ev_lock);
    for (e = g_ev; e && e->h != h; e = e->next) { }
    if (e) ++e->refs;
    pthread_mutex_unlock(&g_ev_lock);
    return e;
}
static void ev_put(UiEv *e) {
    int dispose = 0;
    pthread_mutex_lock(&g_ev_lock);
    if (--e->refs == 0 && e->closed) dispose = 1;
    pthread_mutex_unlock(&g_ev_lock);
    if (dispose) { pthread_cond_destroy(&e->c); pthread_mutex_destroy(&e->m); free(e); }
}
static UiEv *ev_remove(void *h) {
    UiEv **p, *e;
    pthread_mutex_lock(&g_ev_lock);
    for (p = &g_ev; *p && (*p)->h != h; p = &(*p)->next) { }
    e = *p;
    if (e) { *p = e->next; ++e->refs; } /* close's own lifetime reference */
    pthread_mutex_unlock(&g_ev_lock);
    return e;
}
S void *CreateEventA(void *sa, int manual, int init, const char *name) {
    UiEv *e;
    unsigned handle;
    (void) sa; (void) name;
    e = malloc(sizeof *e);
    if (!e) { SetLastError(8 /*ERROR_NOT_ENOUGH_MEMORY*/); return 0; }
    memset(e, 0, sizeof *e);
    e->manual = manual != 0; e->signaled = init != 0;
    pthread_mutex_lock(&g_ev_lock);
    if (g_ev_next >= 0x5F000000u) {
        pthread_mutex_unlock(&g_ev_lock); free(e);
        SetLastError(8 /*ERROR_NOT_ENOUGH_MEMORY*/); return 0;
    }
    handle = g_ev_next++;
    e->h = (void *) (unsigned long) handle; e->next = g_ev; g_ev = e;
    pthread_mutex_unlock(&g_ev_lock);
    if (g_trace || file_trace_enabled()) {
        static unsigned created; ++created;
        if (created <= 17) {
            reasm_elf_log("[evt] CreateEvent #"); reasm_elf_log_hex(created); reasm_elf_log(" handle=");
            reasm_elf_log_hex(handle); reasm_elf_log(" manual="); reasm_elf_log_hex((unsigned) e->manual);
            reasm_elf_log(" init="); reasm_elf_log_hex((unsigned) e->signaled); reasm_elf_log("\n");
        }
    }
    return e->h;
}
S int SetEvent(void *h) {
    UiEv *e = ev_get(h);
    if (!e) { SetLastError(6 /*ERROR_INVALID_HANDLE*/); return 0; }
    pthread_mutex_lock(&e->m); e->signaled = 1;
    pthread_cond_broadcast(&e->c); pthread_mutex_unlock(&e->m);
    ev_put(e); return 1;
}
S int ResetEvent(void *h) {
    UiEv *e = ev_get(h);
    if (!e) { SetLastError(6); return 0; }
    pthread_mutex_lock(&e->m); e->signaled = 0; pthread_mutex_unlock(&e->m);
    ev_put(e); return 1;
}
/* ATTENZIONE (trappola reale): storm legge gli MPQ con un THREAD e aspetta il completamento con
 * WaitForSingleObject. Ritornare subito senza aspettare faceva leggere al thread principale dati
 * incompleti -> "ERROR_HANDLE_EOF" (0x26) e dialogo fatale "font\font.gid". Qui si aspetta davvero
 * (pthread_join sui thread creati da CreateThread). */
static struct { void *h; unsigned long tid; int used; } g_th[16];
static int g_th_n;
S u32 WaitForSingleObject(void *h, u32 ms) {
    int i;
    for (i = 0; i < g_th_n; ++i) {
        if (g_th[i].used && g_th[i].h == h) {
            pthread_join(g_th[i].tid, 0);
            g_th[i].used = 0;
            return 0;                                    /* WAIT_OBJECT_0 */
        }
    }
    {
        UiEv *e = ev_get(h);
        if (e) {
            u32 r;
            pthread_mutex_lock(&e->m);
            if (!e->signaled && !e->closed) {
                if (ms == 0xFFFFFFFFu) {
                    while (!e->signaled && !e->closed) pthread_cond_wait(&e->c, &e->m);
                } else if (ms) {
                    struct { long s, ns; } ts;
                    clock_gettime(0 /*CLOCK_REALTIME*/, &ts);
                    ts.s += (long) (ms / 1000u);
                    ts.ns += (long) (ms % 1000u) * 1000000L;
                    if (ts.ns >= 1000000000L) { ts.ns -= 1000000000L; ++ts.s; }
                    while (!e->signaled && !e->closed) {
                        if (pthread_cond_timedwait(&e->c, &e->m, &ts) != 0) break;
                    }
                }
            }
            r = e->closed ? 0xFFFFFFFFu : (e->signaled ? 0u : 0x102u);
            if (e->signaled && !e->manual) e->signaled = 0;
            pthread_mutex_unlock(&e->m); ev_put(e);
            if (g_trace || file_trace_enabled()) {
                reasm_elf_log("[evt] Wait handle="); reasm_elf_log_hex((unsigned long) h);
                reasm_elf_log(" ms="); reasm_elf_log_hex(ms); reasm_elf_log(" result="); reasm_elf_log_hex(r); reasm_elf_log("\n");
            }
            if (r == 0xFFFFFFFFu) SetLastError(6);
            return r;
        }
    }
    /* Handle non gestito: su Windows WaitForSingleObject fallisce (WAIT_FAILED). */
    if (g_trace || file_trace_enabled()) {
        reasm_elf_log("[evt] Wait INVALID handle="); reasm_elf_log_hex((unsigned long) h);
        reasm_elf_log(" ms="); reasm_elf_log_hex(ms); reasm_elf_log("\n");
    }
    SetLastError(6);
    return 0xFFFFFFFFu;
}
/* ATTENZIONE (causa radice del crash 0x004D4DCA / 0x0040B74C dopo Start, 2026-10-27):
 * sub_47ae30 (0x47AE30) e sub_47af30 (0x47AF30) accodano centinaia di SFileReadFile asincrone
 * con OVERLAPPED per caricare tutti i GRP delle unita' (incluso #584 a 0x51D7F0 = 0x51CED0+584*4),
 * poi chiamano sub_4d29d0 (0x4D29D0) che attende il completamento a blocchi di 63 handle con
 * WaitForMultipleObjects(n, handles, TRUE, 50) (0x4D2A38) prima di chiudere gli handle e rilocare
 * in-place gli offset dei frame GRP ([grp+0x0A+i*8] += grp, ciclo 0x47AEFC..0x47AF21).
 * Con lo stub che ritornava 0 subito, il ciclo di rilocazione girava su buffer ancora a zero
 * (wFrames == 0 -> rilocazione saltata) e il worker MPQ (0x1501633B) copiava i byte grezzi del
 * GRP DOPO, lasciando l'offset grezzo 0x0000000E al posto del puntatore rilocato. */
static u32 ticks_ms(void);
static unsigned g_wfmo_anomalies;
unsigned win32_shim_wfmo_anomalies(void) { return g_wfmo_anomalies; }
S u32 WaitForMultipleObjects(u32 n, void **h, int all, u32 ms) {
    typedef u32 (S *wait1_fn)(void *, u32);
#if defined(__arm__) || defined(__i386__)
    /* ARM usa import statici; su i386 lo slot Storm a 0x15045264 contiene un RVA guest
     * (nel fault osservato: 0x00056F24), non un puntatore host chiamabile. In entrambe le ABI
     * chiama direttamente lo shim locale per eventi/handle nativi e riconosci qui gli handle
     * thread guest creati da arm_guest_thread_start. */
    wait1_fn w1 = WaitForSingleObject;
    extern int arm_guest_thread_join(u32 handle) __attribute__((weak));
#else
    wait1_fn w1 = *(wait1_fn *) 0x15045264u;
#endif
    u32 start, i;
    /* Gate diagnostico dichiarato: SC_WFMO_STUB=1 riproduce il comportamento pre-fix (ritorno
     * immediato WAIT_OBJECT_0). Serve solo all'A/B documentato; nessun effetto se non impostato. */
    {   extern char *getenv(const char *);
        static int stub = -1;
        if (stub < 0) { const char *v = getenv("SC_WFMO_STUB"); stub = (v && v[0] == '1' && !v[1]); }
        if (stub) return 0u;
    }
#if !defined(__arm__) && !defined(__i386__)
    if (!w1) w1 = WaitForSingleObject;
#endif
    if (!n || n > 64u || !h) { SetLastError(87); return 0xFFFFFFFFu; }
    start = ticks_ms();
    if (all) {
        for (i = 0; i < n; ++i) {
            u32 rem = 0xFFFFFFFFu, r;
            if (ms != 0xFFFFFFFFu) {
                u32 el = ticks_ms() - start;
                rem = (el >= ms) ? 0u : (ms - el);
            }
#if defined(__arm__) || defined(__i386__)
            if ((uintptr_t) h[i] >= 0x72000000u && (uintptr_t) h[i] < 0x73000000u &&
                arm_guest_thread_join && arm_guest_thread_join((u32) (uintptr_t) h[i]))
                r = 0u;
            else
#endif
                r = w1(h[i], rem);
            /* Un ritorno diverso da WAIT_TIMEOUT (0x102) fa considerare al gioco il gruppo
             * "completato" (0x4D2A3A confronta solo 0x102): va quindi tracciato. */
            if (r != 0u) {
                if (getenv("REASM_TRACE")) reasm_trace2("[wfmo] gruppo NON completato n=", n, " ret=", r);
                ++g_wfmo_anomalies;
                return r;
            }
        }
        if (getenv("REASM_TRACE")) reasm_trace2("[wfmo] all n=", n, " ret=0", 0);
        return 0u;
    }
    for (;;) {
        for (i = 0; i < n; ++i) {
            u32 r;
#if defined(__arm__) || defined(__i386__)
            if ((uintptr_t) h[i] >= 0x72000000u && (uintptr_t) h[i] < 0x73000000u &&
                arm_guest_thread_join && arm_guest_thread_join((u32) (uintptr_t) h[i]))
                r = 0u;
            else
#endif
                r = w1(h[i], 0u);
            if (r == 0u) return i;
            if (r == 0xFFFFFFFFu) return 0xFFFFFFFFu;
        }
        if (ms != 0xFFFFFFFFu && (ticks_ms() - start) >= ms) return 0x102u;
        usleep(1000u);
    }
}
/* --- THREAD CON STACK BASSO E TEB FINTO ---
 * storm crea un thread di lettura asincrona degli MPQ. Due vincoli che su Windows sono impliciti
 * e su Linux NO:
 *  (1) STACK BASSO. Windows a 32 bit ha lo spazio utente sotto 0x80000000 e storm codifica
 *      "contenitore vuoto" come ~ptr, decidendo dalla negativita' del valore. Lo stack di un
 *      pthread nasce in alto (osservato esp=0xF78A5264 nel thread di lettura): il sentinella
 *      diventa positivo, storm scorre un contenitore inesistente e libera puntatori spuri ->
 *      SIGSEGV NON deterministico dentro SMemFree (visti 0x1502065E, 0x1501EE7B, 0x150049CC) nel
 *      solo thread di lettura. E' la stessa trappola gia' documentata per il thread principale
 *      (reasm_call_on_stack a 0x08000000): qui si alloca lo stack a 0x09000000+.
 *  (2) TEB/PEB. Il codice di storm legge fs:[0x30] (PEB) e fs:[0x34] (LastError): in un pthread
 *      %fs punta al TLS di glibc, quindi si installa il TEB finto anche qui (teb_init). */
#define TH_STACK_BASE 0x09000000u
#define TH_STACK_SIZE (1u * 1024u * 1024u)
#define TH_STACK_END  0x10000000u
static unsigned int g_th_stack = TH_STACK_BASE;

extern int pthread_attr_init(void *);
extern int pthread_attr_setstack(void *, void *, ulong);
extern int pthread_attr_destroy(void *);
extern void teb_init(void);
typedef union { char _s[64]; long long _a; } ui_attr_t;

static struct { void *(*fn)(void *); void *arg; } g_thjob[16];
static int g_thjob_n;

static void *th_tramp(void *p) {
    int i;
    void *(*fn)(void *) = 0;
    void *a = 0;
    for (i = 0; i < g_thjob_n; ++i)
        if ((void *) &g_thjob[i] == p) { fn = g_thjob[i].fn; a = g_thjob[i].arg; break; }
    if (!fn) return 0;
    teb_init();                       /* fs:[0x30]=PEB, fs:[0x34]=LastError in QUESTO thread */
    return fn(a);
}

S void *CreateThread(void *sa, u32 stack, void *fn, void *arg, u32 flags, u32 *id) {
    ulong th;

    void *stk;
    ui_attr_t at;
    int slot = -1, i;
    unsigned int nb = stack ? ((stack + 0xFFFFu) & ~0xFFFFu) : TH_STACK_SIZE;
    (void) sa; (void) flags;
    if (nb < 0x10000u) nb = 0x10000u;
    nb = (nb + 0xFFFu) & ~0xFFFu;
    for (i = 0; i < g_thjob_n; ++i) if (!g_thjob[i].fn) { slot = i; break; }
    if (slot < 0) { if (g_thjob_n >= 16) return 0; slot = g_thjob_n++; }
    stk = mmap((void *) (ulong) g_th_stack, nb, 3, 0x32, -1, 0);   /* MAP_FIXED|PRIV|ANON */
    if (stk == MAP_FAILED) { stk = 0; th = 0; }
    else {
        g_th_stack = (g_th_stack + nb + 0xFFFu) & ~0xFFFu;
        if (g_th_stack >= TH_STACK_END) g_th_stack = TH_STACK_BASE;   /* riuso circolare */
        pthread_attr_init(&at);
        pthread_attr_setstack(&at, stk, nb);
        g_thjob[slot].fn = (void *(*)(void *)) fn;
        g_thjob[slot].arg = arg;
        if (pthread_create(&th, &at, th_tramp, &g_thjob[slot]) != 0) { g_thjob[slot].fn = 0; th = 0; }
        pthread_attr_destroy(&at);
    }
    if (!th) { if (g_trace) reasm_elf_log("[trace] CreateThread non riuscito\n"); return 0; }
    if (g_trace) { reasm_elf_log("[trace] CreateThread stack="); reasm_elf_log_hex((unsigned) (ulong) stk);
                   reasm_elf_log(" size="); reasm_elf_log_hex(nb); reasm_elf_log("\n"); }
    if (id) *id = (u32) th;
    if (g_th_n < 16) { g_th[g_th_n].h = (void *) th; g_th[g_th_n].tid = th;
                       g_th[g_th_n].used = 1; ++g_th_n; }
    return (void *) th;
}
S void Sleep(u32 ms) { if (ms) usleep(ms * 1000u); }

/* --- tempo --- */
static u32 ticks_ms(void) {
    struct { long s, ns; } ts;
    clock_gettime(1 /*CLOCK_MONOTONIC*/, &ts);
    return (u32) (ts.s * 1000 + ts.ns / 1000000);
}
S u32 GetTickCount(void) { return ticks_ms(); }
S int QueryPerformanceFrequency(long long *f) { if (f) *f = 1000000LL; return 1; }
S int QueryPerformanceCounter(long long *c) { if (c) *c = (long long) ticks_ms() * 1000LL; return 1; }
S void GetSystemTimeAsFileTime(FILETIME_T *ft) {
    unsigned long long v;
    if (!ft) return;
    v = (unsigned long long) (time(0) + 11644473600LL) * 10000000ULL;
    ft->lo = (u32) v; ft->hi = (u32) (v >> 32);
}
static void civil_from_days(long z, SYSTEMTIME_T *st) {
    long era, doe, yoe, y, doy, mp, d, m;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    st->y = (u16) (y + (m <= 2));
    st->mo = (u16) m;
    st->d = (u16) d;
}
S void GetLocalTime(SYSTEMTIME_T *st) {
    long t = time(0);
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->dow = (u16) ((t / 86400 + 4) % 7);            /* 1970-01-01 = giovedi' (4) */
    st->h = (u16) ((t / 3600) % 24);
    st->mi = (u16) ((t / 60) % 60);
    st->s = (u16) (t % 60);
    civil_from_days(t / 86400, st);
}
S int FileTimeToSystemTime(const FILETIME_T *ft, SYSTEMTIME_T *st) {
    unsigned long long v;
    long days, rem;
    if (!ft || !st) return 0;
    v = ((unsigned long long) ft->hi << 32 | ft->lo) / 10000000ULL;   /* secondi dal 1601 */
    v -= 11644473600ULL;
    days = (long) (v / 86400); rem = (long) (v % 86400);
    memset(st, 0, sizeof *st);
    civil_from_days(days, st);
    st->h = (u16) (rem / 3600); st->mi = (u16) ((rem / 60) % 60); st->s = (u16) (rem % 60);
    return 1;
}
S int FileTimeToLocalFileTime(const FILETIME_T *in, FILETIME_T *out) { if (in && out) *out = *in; return 1; }
S int CompareFileTime(const FILETIME_T *a, const FILETIME_T *b) {
    unsigned long long x = a ? ((unsigned long long) a->hi << 32 | a->lo) : 0;
    unsigned long long y = b ? ((unsigned long long) b->hi << 32 | b->lo) : 0;
    return x < y ? -1 : (x > y ? 1 : 0);
}
/* TIME_ZONE_INFORMATION: bias + two UTF-16 names + two SYSTEMTIME + daylight bias. */
S u32 GetTimeZoneInformation(void *tzi) {
    unsigned char *p = (unsigned char *) tzi;
    u32 bias;
    const char *std, *dst;
    unsigned i;
    if (!p) return 0xFFFFFFFFu;
    tzset();
    memset(p, 0, 172);
    bias = (u32) (timezone / 60);
    *(u32 *) p = bias;
    std = tzname[0] ? tzname[0] : "UTC";
    dst = tzname[1] ? tzname[1] : std;
    for (i = 0; std[i] && i < 31; ++i) ((u16 *)(p + 4))[i] = (u8) std[i];
    for (i = 0; dst[i] && i < 31; ++i) ((u16 *)(p + 4 + 64 + 16))[i] = (u8) dst[i];
    *(i32 *)(p + 168) = 0; /* no trustworthy per-zone DST delta in the freestanding ABI */
    return (tzname[1] && tzname[0] && strcmp(tzname[0], tzname[1])) ? 2u : 0u;
}

/* --- console/file --- */
#define FILE_READAHEAD_SIZE 65536u
#define FILE_CACHE_CAP 256u
typedef struct {
    void *handle; void *buf; long long buf_off, logical_pos; u32 buf_len;
    int active, enabled; ui_mutex_t mutex;
} FileReadCache;
static FileReadCache g_file_cache[FILE_CACHE_CAP];
static u32 g_file_cache_pread_calls;
static ui_mutex_t g_file_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static FileReadCache *file_cache_lock_handle(void *h) {
    u32 i; FileReadCache *c=0;
    pthread_mutex_lock(&g_file_cache_lock);
    for(i=0;i<FILE_CACHE_CAP;++i)if(g_file_cache[i].active&&g_file_cache[i].handle==h) {
        c=&g_file_cache[i];pthread_mutex_lock(&c->mutex);break;
    }
    pthread_mutex_unlock(&g_file_cache_lock);return c;
}
static void file_cache_unlock(FileReadCache *c) { if(c)pthread_mutex_unlock(&c->mutex); }
static void file_cache_register(void *h,int enabled) {
    u32 i,mode;long pos;unsigned char st[128];int fd=H2FD(h);
    if(!enabled||fd<=2||fstat(fd,st)!=0)return;
    /* Linux 32-bit stat ABI: st_mode is at byte offset 16. Exclude devices and pipes. */
    memcpy(&mode,st+16,sizeof mode);if((mode&0170000u)!=0100000u)return;
    pos=lseek(fd,0,1);if(pos<0)return;
    pthread_mutex_lock(&g_file_cache_lock);
    for(i=0;i<FILE_CACHE_CAP;++i)if(!g_file_cache[i].active) {
        FileReadCache *c=&g_file_cache[i];c->handle=h;c->buf=0;c->buf_off=-1;c->buf_len=0;
        c->logical_pos=pos;c->enabled=1;c->active=1;pthread_mutex_unlock(&g_file_cache_lock);return;
    }
    pthread_mutex_unlock(&g_file_cache_lock);
}
static void file_cache_invalidate(FileReadCache *c) { if(c){c->buf_len=0;c->buf_off=-1;} }
static int file_cache_close(void *h) {
    FileReadCache *c=file_cache_lock_handle(h);int fd;if(!c)return 0;
    fd=H2FD(h);if(c->buf)free(c->buf);c->buf=0;c->buf_len=0;c->active=0;c->enabled=0;c->handle=0;
    file_cache_unlock(c);if(fd>2&&fd<100000)close(fd);return 1;
}
S void *GetStdHandle(u32 n) {
    if (n == (u32) -10) return FD2H(0);
    if (n == (u32) -11) return FD2H(1);
    if (n == (u32) -12) return FD2H(2);
    return 0;
}
S int SetStdHandle(u32 n, void *h) { (void) n; (void) h; return 1; }
S u32 GetFileType(void *h) { (void) h; return 2; }   /* FILE_TYPE_CHAR */
S u32 SetHandleCount(u32 n) { return n; }
S int WriteFile(void *h, const void *buf, u32 n, u32 *written, void *ov) {
    long r;FileReadCache *c;
    (void) ov;c=file_cache_lock_handle(h);if(c)file_cache_invalidate(c);
    r = write(H2FD(h), buf, n);
    if (written) *written = r > 0 ? (u32) r : 0;
    file_cache_unlock(c);return r >= 0;
}
S int ReadFile(void *h, void *buf, u32 n, u32 *got, void *ov) {
    long r;FileReadCache *c;
    (void) ov;
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) {
        r=win32_snp_vfile_read(h,buf,n);if(got)*got=r>0?(u32)r:0;return r>=0;
    }
    c=file_cache_lock_handle(h);
    if(c&&c->enabled) {
        u32 done=0;int fd=H2FD(h);long long pos0=c->logical_pos;
        if(!n){if(got)*got=0;file_cache_unlock(c);return 1;}
        if(n>FILE_READAHEAD_SIZE) {
            file_cache_invalidate(c);__sync_fetch_and_add(&g_file_cache_pread_calls,1);r=pread64(fd,buf,n,c->logical_pos);
            if(r<0){if(got)*got=0;file_cache_unlock(c);return 0;}
            if(r>0)c->logical_pos+=r;done=r>0?(u32)r:0;
        } else while(done<n) {
            long long off=c->logical_pos-c->buf_off;
            if(c->buf&&off>=0&&(unsigned long long)off<c->buf_len) {
                u32 avail=c->buf_len-(u32)off,copy=n-done<avail?n-done:avail;
                memcpy((char*)buf+done,(char*)c->buf+(u32)off,copy);
                c->logical_pos+=copy;done+=copy;continue;
            }
            if(!c->buf)c->buf=malloc(FILE_READAHEAD_SIZE);
            if(!c->buf){file_cache_unlock(c);return 0;}
            __sync_fetch_and_add(&g_file_cache_pread_calls,1);r=pread64(fd,c->buf,FILE_READAHEAD_SIZE,c->logical_pos);
            if(r<0){if(got)*got=done;file_cache_unlock(c);return done?1:0;}
            c->buf_off=c->logical_pos;c->buf_len=(u32)r;if(!r)break;
        }
        if(got)*got=done;
        if(g_trace)reasm_trace3("[trace] ReadFile pos=",(unsigned)pos0," n=",n," ret=",(unsigned)(ulong)__builtin_return_address(0));
        if(g_trace)reasm_trace2("           -> letti ",done,"",0);
        file_cache_unlock(c);return 1;
    }
    file_cache_unlock(c);
    {   long pos0=lseek(H2FD(h),0,1);r=read(H2FD(h),buf,n);if(got)*got=r>0?(u32)r:0;
        if(g_trace)reasm_trace3("[trace] ReadFile pos=",(unsigned)pos0," n=",n," ret=",(unsigned)(ulong)__builtin_return_address(0));
        if(g_trace)reasm_trace2("           -> letti ",(unsigned)r,"",0);
    }
    return r>=0;
}
static int filemap_close(void *h);
S int CloseHandle(void *h) {
    int i;
    if (filemap_close(h)) return 1;
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) { win32_snp_vfile_close(h); return 1; }
    if(file_cache_close(h))return 1;
    for (i = 0; i < g_th_n; ++i)
        if (g_th[i].used && g_th[i].h == h) { g_th[i].used = 0; return 1; }
    {   UiEv *e = ev_remove(h);
        if (e) {
            if (g_trace || file_trace_enabled()) {
                reasm_elf_log("[evt] CloseEvent handle="); reasm_elf_log_hex((unsigned long) h);
                reasm_elf_log(" refs="); reasm_elf_log_hex(e->refs); reasm_elf_log("\n");
            }
            pthread_mutex_lock(&e->m); e->closed = 1; pthread_cond_broadcast(&e->c); pthread_mutex_unlock(&e->m);
            ev_put(e);
            return 1;
        }
    }
    { int fd = H2FD(h); if (fd > 2 && fd < 100000) close(fd); }
    return 1;
}
S int FlushFileBuffers(void *h) {
    int fd;
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) return 0;
    fd = H2FD(h);
    return fd >= 0 && fsync(fd) == 0;
}
S int SetEndOfFile(void *h) {
    int fd; long pos;FileReadCache *c;
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) return 0;
    c=file_cache_lock_handle(h);if(c)file_cache_invalidate(c);
    fd=H2FD(h);pos=lseek(fd,0,1);file_cache_unlock(c);
    return fd>=0&&pos>=0&&ftruncate(fd,pos)==0;
}
S long SetFilePointer(void *h, long lo, long *hi, u32 method) {
    long r;FileReadCache *c;
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) return win32_snp_vfile_seek(h, lo, method);
    c=file_cache_lock_handle(h);
    if(c&&c->enabled) {
        long long base,delta=lo,target;int fd=H2FD(h);
        if(hi)delta=(long long)(i32)(*hi)*4294967296LL+(u32)lo;
        if(method==0)base=0;
        else if(method==1)base=c->logical_pos;
        else if(method==2) {
            long old=lseek(fd,0,1),end=lseek(fd,0,2);
            if(old>=0)(void)lseek(fd,old,0);
            if(end<0){file_cache_unlock(c);return -1;}base=end;
        } else {file_cache_unlock(c);return -1;}
        target=base+delta;if(target<0){file_cache_unlock(c);return -1;}
        c->logical_pos=target;r=(long)(u32)target;if(hi)*hi=(long)(u32)((unsigned long long)target>>32);
        if(g_trace)reasm_trace2("[trace] SetFilePointer lo=",(unsigned)lo," -> ",(unsigned)r);
        file_cache_unlock(c);return r;
    }
    file_cache_unlock(c);
    r=lseek(H2FD(h),lo,(int)method);
    if(g_trace)reasm_trace2("[trace] SetFilePointer lo=",(unsigned)lo," -> ",(unsigned)r);
    return r;
}
S u32 GetFileSize(void *h, u32 *hi) {
    int fd = H2FD(h); long cur = lseek(fd, 0, 1), end = lseek(fd, 0, 2);
    if (win32_snp_vfile_own && win32_snp_vfile_own(h)) { if (hi) *hi = 0; return win32_snp_vfile_size(h); }
    lseek(fd, cur, 0);
    if (hi) *hi = 0;
    if (g_trace) reasm_trace2("[trace] GetFileSize -> ", (unsigned) end, "", 0);
    return end < 0 ? 0xFFFFFFFFu : (u32) end;
}
static int file_trace_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) { char *v = getenv("REASM_TRACE"); enabled = v && *v ? 1 : 0; }
    return enabled;
}
S void *CreateFileA(const char *name, u32 acc, u32 share, void *sa, u32 disp, u32 flags, void *tmpl) {
    int oflags = 0, fd;
    (void) share; (void) sa; (void) flags; (void) tmpl;
    if (win32_snp_vfile_open) {
        void *v = win32_snp_vfile_open(name);
        if (v) return v;                          /* solo il nostro standard.snp (caps.dat) */
    }
    if ((acc & GENERIC_READ) && (acc & GENERIC_WRITE)) oflags = 2;
    else if (acc & GENERIC_WRITE) oflags = 1;
    if (disp == 1) oflags |= 0100 | 0200;            /* CREATE_NEW = O_CREAT | O_EXCL */
    else if (disp == 2) oflags |= 0100 | 01000;      /* CREATE_ALWAYS */
    else if (disp == 4) oflags |= 0100;              /* OPEN_ALWAYS */
    if (disp == 5) oflags |= 01000;                  /* TRUNCATE_EXISTING */
    {   char up[300];
        reasm_win_path(name, up, sizeof up);
        fd = reasm_open_ci(up, oflags, 0666);
        if (file_trace_enabled()) {
            char cwd[260];
            reasm_elf_log("[file] guest="); reasm_elf_log(name ? name : "?");
            reasm_elf_log(" native="); reasm_elf_log(up); reasm_elf_log(" cwd=");
            if (getcwd(cwd, sizeof cwd)) reasm_elf_log(cwd); else reasm_elf_log("<getcwd-error>");
            reasm_elf_log(" fd="); reasm_elf_log_hex((unsigned) fd);
            if (fd < 0) { reasm_elf_log(" errno="); reasm_elf_log_hex((unsigned) *__errno_location()); }
            reasm_elf_log("\n");
        }
    }
    if(fd<0)return INVALID_HANDLE;
    { void *h=FD2H(fd);
      file_cache_register(h,(acc&GENERIC_READ)&&!(acc&GENERIC_WRITE)&&disp==3);
      return h;
    }
}
/* Case-insensitive ANCHE sui file, non solo sulle directory: il gioco chiede gli attributi di
 * "\StarCraft.mpq" (C maiuscola) mentre su disco e' "Starcraft.mpq". Con access() (sensibile alle
 * maiuscole) il gioco concludeva che l'archivio non c'e' e NON lo apriva; dentro StarCraft.mpq
 * stanno i glue/PCX dell'interfaccia, quindi poi falliva con il dialogo fatale
 * "glue\PalRt\tEffect.pcx". */
S u32 GetFileAttributesA(const char *name) {
    char up[300];
    int fd;
    reasm_win_path(name, up, sizeof up);
    if (file_trace_enabled()) {
        char b[360]; int i = 0; const char *s = "[attr] GetFileAttributesA(";
        while (*s && i < 330) b[i++] = *s++;
        if (name) { const char *q = name; while (*q && i < 330) b[i++] = *q++; }
        b[i++] = ')'; b[i++] = '\n'; b[i] = 0;
        reasm_elf_log(b);
    }
    fd = reasm_open_ci(up, 0 | UI_O_DIRECTORY, 0);          /* directory */
    if (fd >= 0) { close(fd); if (file_trace_enabled()) { reasm_elf_log("[attr] result=directory native="); reasm_elf_log(up); reasm_elf_log("\n"); } return 0x10u; }
    fd = reasm_open_ci(up, 0, 0);                           /* file, senza distinzione maiuscole */
    if (fd >= 0) { close(fd); if (file_trace_enabled()) { reasm_elf_log("[attr] result=file native="); reasm_elf_log(up); reasm_elf_log("\n"); } return 0x80u; }
    if (file_trace_enabled()) { reasm_elf_log("[attr] result=missing native="); reasm_elf_log(up); reasm_elf_log(" errno="); reasm_elf_log_hex((unsigned) *__errno_location()); reasm_elf_log("\n"); }
    return 0xFFFFFFFFu;
}
S int SetFileAttributesA(const char *n, u32 a) { char up[300]; (void) a; reasm_win_path(n, up, sizeof up); return access(up, 0) == 0 || GetFileAttributesA(n) != 0xFFFFFFFFu; }
S int CreateDirectoryA(const char *n, void *sa) { return game_CreateDirectoryA(n, sa); }
S int DeleteFileA(const char *n) { char up[300]; reasm_win_path(n, up, sizeof up); return unlink_(up) == 0; }
S u32 GetDriveTypeA(const char *root) { (void) root; return 3; /* DRIVE_FIXED */ }
S u32 GetLogicalDriveStringsA(u32 n, char *buf) {
    if (buf && n >= 4) { memcpy(buf, "C:\\", 4); return 4; }
    return 0;
}
/* Il gioco richiede >= 20 MB liberi su C: (sub_4E0440, 0x4E04CC: freec*spc/1024*bps/1024 >= 20).
 * I valori vengono dal filesystem REALE via statfs: "C:\" -> "/" (o la dir corrente). */
S int GetDiskFreeSpaceA(const char *root, u32 *spc, u32 *bps, u32 *freec, u32 *total) {
    struct ui_statfs fs;
    char up[300];
    const char *p;
    memset(&fs, 0, sizeof fs);
    reasm_win_path(root ? root : "/", up, sizeof up);
    p = up[0] ? up : "/";
    if (statfs_(p, &fs) != 0) { if (statfs_("/", &fs) != 0) return 0; }
    if (fs.bsize <= 0) fs.bsize = 4096;
    if (spc) *spc = 8;
    if (bps) *bps = 512;
    {   unsigned long long unit = (unsigned long long) fs.bsize;   /* byte per blocco */
        unsigned long long clus = 4096ull;                        /* 8 * 512 */
        if (freec) *freec = (u32) (((unsigned long long) fs.bavail * unit) / clus);
        if (total) *total = (u32) (((unsigned long long) fs.blocks * unit) / clus); }
    if (g_trace) { reasm_elf_log("[trace] GetDiskFreeSpaceA("); reasm_elf_log(p);
                   reasm_elf_log(") bavail="); reasm_elf_log_hex((unsigned) fs.bavail);
                   reasm_elf_log("\n"); }
    return 1;
}
S void *FindFirstFileA(const char *spec, void *data) { return game_FindFirstFileA(spec, (GameFindData *) data); }
S int FindNextFileA(void *h, void *data) { return game_FindNextFileA(h, (GameFindData *) data); }
S int FindClose(void *h) { return game_FindClose(h); }
S int GetComputerNameA(char *buf, u32 *n) {
    const char *s = "FRIGATE"; u32 i = 0;
    while (s[i] && (!n || i + 1 < *n)) { buf[i] = s[i]; ++i; }
    buf[i] = 0; if (n) *n = i;
    return 1;
}
S int SetEnvironmentVariableA(const char *n, const char *v) { (void) n; (void) v; return 1; }
S u32 FormatMessageA(u32 flags, const void *src, u32 id, u32 lang, char *buf, u32 n, void *args) {
    (void) flags; (void) src; (void) id; (void) lang; (void) args;
    if (buf && n) buf[0] = 0;
    return 0;
}
S char *lstrcpynA(char *dst, const char *src, int n) {
    int i = 0;
    if (n <= 0) return dst;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
    return dst;
}
S i32 MulDiv(i32 a, i32 b, i32 c) { return c ? (i32) (((long long) a * b) / c) : -1; }
S int IsBadReadPtr(const void *p, u32 n) { (void) p; (void) n; return 0; }
S int IsBadWritePtr(void *p, u32 n) { (void) p; (void) n; return 0; }

/* --- locale / conversioni --- */
/* --- locale / conversioni ---
 * PORTATO da ~/dev/lola-recomp/runtime/shim/win32/{shim_core,shim_crt}.c: stesse trappole gia'
 * pagate la' (R6009 per GetEnvironmentStringsW vuoto, GetStringTypeW a 4 argomenti, modalita'
 * QUERY di WideCharToMultiByte). Non si reinventa. */
static const char *locale_str(u32 t) {
    switch (t) {
    case 0x0001: return "0409";                 /* LOCALE_ILANGUAGE */
    case 0x0002: return "English_United States.1252";
    case 0x0004: return ".";                    /* LOCALE_SDECIMAL */
    case 0x000E: return ",";                    /* LOCALE_STHOUSAND */
    case 0x0010: return ":";                    /* LOCALE_STIME */
    case 0x001F: return "MM/dd/yyyy";
    case 0x0020: return "dddd, MMMM dd, yyyy";
    case 0x0024: return "English";
    case 0x0038: return "January"; case 0x0039: return "February"; case 0x003A: return "March";
    case 0x003B: return "April";   case 0x003C: return "May";      case 0x003D: return "June";
    case 0x003E: return "July";    case 0x003F: return "August";   case 0x0040: return "September";
    case 0x0041: return "October"; case 0x0042: return "November"; case 0x0043: return "December";
    case 0x005C: return "en-US";
    case 0x1004: return "1252";
    case 0x1005: return "437";
    case 0x1009: return "1";
    default: return "";
    }
}
S u32 GetACP(void) { return 1252; }
S u32 GetOEMCP(void) { return 437; }
S u32 GetUserDefaultLCID(void) { return 0x0409; }
S u32 GetUserDefaultLangID(void) { return 0x0409; }
S int GetCPInfo(u32 cp, u8 *info) {
    (void) cp;
    if (!info) return 0;
    memset(info, 0, 20);
    *(u32 *) info = 1;
    info[4] = '?';
    return 1;
}
S int IsDBCSLeadByte(u8 b) { (void) b; return 0; /* GetACP() is Windows-1252 in this shim. */ }
S int GetLocaleInfoA(u32 l, u32 t, char *b, int n) {
    const char *s = locale_str(t); int i; (void) l;
    if (!b || n <= 0) return (int) strlen(s) + 1;      /* query della dimensione */
    for (i = 0; s[i] && i < n - 1; ++i) b[i] = s[i];
    b[i] = 0;
    return i + 1;
}
S int GetLocaleInfoW(u32 l, u32 t, u16 *b, int n) {
    const char *s = locale_str(t); int i; (void) l;
    if (!b || n <= 0) return (int) strlen(s) + 1;
    for (i = 0; s[i] && i < n - 1; ++i) b[i] = (u16) (u8) s[i];
    b[i] = 0;
    return i + 1;
}
S int EnumSystemLocalesA(int (__attribute__((stdcall)) *cb)(char *), u32 f) {
    (void) f; if (cb) cb("00000410"); return 1;
}
S int IsValidLocale(u32 l, u32 f) { (void) l; (void) f; return 1; }
S int IsValidCodePage(u32 c) { (void) c; return 1; }
/* ATTENZIONE: il CRT chiama questa API in modalita' QUERY (lpMultiByteStr NULL) per la dimensione;
 * restituire 0 faceva fallire la conversione dell'ambiente (R6009). Con cbMultiByte = -1 la stringa
 * e' NUL-terminata e il NUL va contato. */
S int WideCharToMultiByte(u32 cp, u32 f, const u16 *w, int n, char *o, int on, const char *d, int *u) {
    int i, need; (void) cp; (void) f; (void) d; (void) u;
    if (!w) return 0;
    if (n < 0) { n = 0; while (w[n]) ++n; ++n; }
    need = n;
    if (!o || on <= 0) return need;
    for (i = 0; i < need && i < on; ++i) o[i] = (w[i] < 128) ? (char) w[i] : '?';
    return i;
}
S int MultiByteToWideChar(u32 cp, u32 f, const char *s, int n, u16 *w, int wn) {
    int i, need; (void) cp; (void) f;
    if (!s) return 0;
    if (n < 0) { n = 0; while (s[n]) ++n; ++n; }
    need = n;
    if (!w || wn <= 0) return need;
    for (i = 0; i < need && i < wn; ++i) w[i] = (u16) (u8) s[i];
    return i;
}
S int GetStringTypeA(u32 l, u32 t, const char *s, int n, u16 *o) {
    (void) l; (void) t;
    if (!s || !o) return 0;
    if (n < 0) n = (int) strlen(s) + 1;
    while (n-- > 0) { char c = *s++; *o++ = (c >= '0' && c <= '9') ? 4 : 1; }
    return 1;
}
/* TRAPPOLA: GetStringTypeW ha 4 argomenti (dwInfoType, lpSrcStr, cchSrc, lpCharType): la variante W
 * NON ha la LCID. Con 5 argomenti fa `ret 20` invece di `ret 16` -> pila sfasata di 4 byte. */
S int GetStringTypeW(u32 t, const u16 *s, int n, u16 *o) {
    int i; (void) t;
    if (!s || !o) return 0;
    if (n < 0) { n = 0; while (s[n]) ++n; }
    for (i = 0; i < n; ++i) {
        u16 c = s[i];
        if (c >= '0' && c <= '9') o[i] = 0x0004;
        else if (c == ' ' || c == '\t') o[i] = 0x4008;
        else if (c >= 'A' && c <= 'Z') o[i] = 0x0101;
        else if (c >= 'a' && c <= 'z') o[i] = 0x0102;
        else if (c < 0x20) o[i] = 0x0020;
        else o[i] = 0x0010;
    }
    return 1;
}
S int LCMapStringA(u32 l, u32 f, const char *s, int n, char *d, int dn) {
    int i; (void) l;
    if (!s || !d) return 0;
    if (n < 0) n = (int) strlen(s) + 1;
    if (n > dn) return 0;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if ((f & 0x200u) && c >= 'a' && c <= 'z') c = (char) (c - 32);
        else if ((f & 0x100u) && c >= 'A' && c <= 'Z') c = (char) (c + 32);
        d[i] = c;
    }
    return n;
}
S int LCMapStringW(u32 l, u32 f, const u16 *s, int n, u16 *d, int dn) {
    int i; (void) l;
    if (!s || !d) return 0;
    if (n < 0) { n = 0; while (s[n]) ++n; ++n; }
    if (n > dn) return 0;
    for (i = 0; i < n; ++i) {
        u16 c = s[i];
        if ((f & 0x200u) && c >= 'a' && c <= 'z') c = (u16) (c - 32);
        else if ((f & 0x100u) && c >= 'A' && c <= 'Z') c = (u16) (c + 32);
        d[i] = c;
    }
    return n;
}
S int CompareStringA(u32 l, u32 f, const char *a, int na, const char *b, int nb) {
    int r; (void) l; (void) f;
    if (na < 0) na = (int) strlen(a) + 1;
    if (nb < 0) nb = (int) strlen(b) + 1;
    r = memcmp(a, b, (ulong) (na < nb ? na : nb));
    if (r == 0) r = na - nb;
    return r < 0 ? 1 : (r > 0 ? 3 : 2);
}
S int CompareStringW(u32 l, u32 f, const u16 *a, int na, const u16 *b, int nb) {
    return CompareStringA(l, f, (const char *) a, na * 2, (const char *) b, nb * 2);
}
S int GetDateFormatA(u32 l, u32 f, const void *st, const char *fmt, char *out, int cch) {
    const u16 *s = st; int i = 0; (void) l; (void) f; (void) fmt;
    if (!st || !out || cch < 11) return 0;
    out[i++] = (char) ('0' + (s[3] / 10)); out[i++] = (char) ('0' + (s[3] % 10)); out[i++] = '/';
    out[i++] = (char) ('0' + (s[1] / 10)); out[i++] = (char) ('0' + (s[1] % 10)); out[i++] = '/';
    out[i++] = (char) ('0' + (s[0] / 1000)); out[i++] = (char) ('0' + ((s[0] / 100) % 10));
    out[i++] = (char) ('0' + ((s[0] / 10) % 10)); out[i++] = (char) ('0' + (s[0] % 10));
    out[i] = 0;
    return i + 1;
}
S int GetTimeFormatA(u32 l, u32 f, const void *st, const char *fmt, char *out, int cch) {
    const u16 *s = st; int i = 0; (void) l; (void) f; (void) fmt;
    if (!st || !out || cch < 9) return 0;
    out[i++] = (char) ('0' + (s[4] / 10)); out[i++] = (char) ('0' + (s[4] % 10)); out[i++] = ':';
    out[i++] = (char) ('0' + (s[5] / 10)); out[i++] = (char) ('0' + (s[5] % 10)); out[i++] = ':';
    out[i++] = (char) ('0' + (s[6] / 10)); out[i++] = (char) ('0' + (s[6] % 10));
    out[i] = 0;
    return i + 1;
}
S void GetSystemTime(SYSTEMTIME_T *st) { if (st) memset(st, 0, sizeof *st); }
S int SystemTimeToFileTime(const SYSTEMTIME_T *st, FILETIME_T *ft) {
    (void) st; if (ft) { ft->lo = 0; ft->hi = 0; } return 1;
}
S u32 GetCurrentDirectoryA(u32 n, char *buf) {
    char cwd[260];
    if (!buf || n < 4) return 0;
    if (!getcwd(cwd, sizeof cwd)) { cwd[0] = '/'; cwd[1] = 0; }
    reasm_unix_to_win(cwd, buf, (int) n);
    return (u32) strlen(buf);
}
S u32 GetFullPathNameA(const char *name, u32 n, char *buf, char **filepart) {
    u32 l = name ? (u32) strlen(name) : 0;
    if (filepart) *filepart = 0;
    if (!buf || n <= l) return l + 1;
    memcpy(buf, name, l); buf[l] = 0;
    return l;
}
S u32 GetTempPathA(u32 n, char *buf) {
    const char *s = "/tmp/"; u32 l = 5;
    if (!buf || n <= l) return l + 1;
    memcpy(buf, s, l + 1);
    return l;
}
S u32 GetTempFileNameA(const char *path, const char *pre, u32 uniq, char *buf) {
    (void) path; (void) pre; (void) uniq;
    if (buf) { const char *s = "/tmp/reasm.tmp"; memcpy(buf, s, 15); }
    return 0;
}
S int SetThreadPriority(void *h, int p) { (void) h; (void) p; return 1; }
S void *GetCurrentThread(void) { return (void *) -2; }
S int GetExitCodeProcess(void *h, u32 *code) { (void) h; if (code) *code = 0; return 1; }
S int FreeResource(void *r) { (void) r; return 0; }
#define MAP_SLOTS 32
#define VIEW_SLOTS 64
#define PROT_READ_ 1
#define PROT_WRITE_ 2
#define MAP_SHARED_ 1
#define MAP_PRIVATE_ 2
#define MAP_FAILED_ ((void *) -1)
typedef struct { u32 magic; int fd; u32 prot, size; } FileMap;
typedef struct { void *addr; u32 size; } FileView;
static FileMap g_maps[MAP_SLOTS];
static FileView g_views[VIEW_SLOTS];
#define FILEMAP_MAGIC 0x464D4150u
S void *CreateFileMappingA(void *f, void *sa, u32 prot, u32 hi, u32 lo, const char *name) {
    int fd, i; long cur, end; FileMap *m;
    (void) sa; (void) name;
    if (win32_snp_vfile_own && win32_snp_vfile_own(f)) return 0; /* standard.snp è un vfile, non un fd */
    fd = H2FD(f);
    if (fd < 0) return 0;
    cur = lseek(fd, 0, 1); end = lseek(fd, 0, 2);
    if (end < 0 || lseek(fd, cur, 0) < 0) return 0;
    for (i = 0; i < MAP_SLOTS; ++i) if (!g_maps[i].magic) break;
    if (i == MAP_SLOTS) return 0;
    m = &g_maps[i]; m->magic = FILEMAP_MAGIC; m->fd = fd;
    m->prot = (prot == 4 /* PAGE_READWRITE */ || prot == 8 /* PAGE_WRITECOPY */) ? 3 : 1;
    m->size = hi || lo ? lo : (u32) end;
    if (hi) { m->magic = 0; return 0; } /* >4GiB mapping is outside this 32-bit port */
    return m;
}
static int filemap_close(void *h) {
    int i;
    for (i = 0; i < MAP_SLOTS; ++i) if ((void *) &g_maps[i] == h && g_maps[i].magic == FILEMAP_MAGIC) {
        g_maps[i].magic = 0; return 1;
    }
    return 0;
}
S void *MapViewOfFile(void *handle, u32 acc, u32 hi, u32 lo, u32 n) {
    FileMap *m = 0; int prot = 0, flags, i; u32 len; void *p;
    for (i = 0; i < MAP_SLOTS; ++i)
        if ((void *) &g_maps[i] == handle && g_maps[i].magic == FILEMAP_MAGIC) { m = &g_maps[i]; break; }
    if (!m || hi) return 0;
    if (acc & 4) prot |= PROT_READ_;
    if (acc & 2) prot |= PROT_READ_ | PROT_WRITE_;
    if (acc & 1) prot |= PROT_READ_;
    if (!acc) prot = (int) m->prot;
    if ((prot & 2) && !(m->prot & 2)) return 0;
    len = n ? n : (m->size > lo ? m->size - lo : 0);
    if (!len || (unsigned long long) lo + len > m->size) return 0;
    for (i = 0; i < VIEW_SLOTS; ++i) if (!g_views[i].addr) break;
    if (i == VIEW_SLOTS) return 0;
    flags = (m->prot & 2) ? MAP_SHARED_ : MAP_PRIVATE_;
    p = mmap(0, len, (prot & 2 ? PROT_READ_ | PROT_WRITE_ : PROT_READ_), flags, m->fd, (long) lo);
    if (p == MAP_FAILED_) return 0;
    g_views[i].addr = p; g_views[i].size = len;
    return p;
}
S int UnmapViewOfFile(const void *p) {
    int i; for (i = 0; i < VIEW_SLOTS; ++i) if (g_views[i].addr == p) {
        int r = munmap(g_views[i].addr, g_views[i].size) == 0;
        g_views[i].addr = 0; g_views[i].size = 0; return r;
    }
    return 0;
}
S int GetVolumeInformationA(const char *root, char *label, u32 labellen, u32 *serial,
                            u32 *maxcomplen, u32 *flags, char *fsname, u32 fsnamelen) {
    (void) root;
    if (label && labellen >= 4) { memcpy(label, "SC", 3); }
    if (serial) *serial = 0x12345678u;
    if (maxcomplen) *maxcomplen = 255;
    if (flags) *flags = 0;
    if (fsname && fsnamelen >= 7) { memcpy(fsname, "FAT32", 6); }
    return 1;
}

/* --- chiusura --- */
S void ExitProcess(u32 code) { _exit((int) code); }
S int  TerminateProcess(void *h, u32 code) { (void) h; _exit((int) code); return 1; }
S void ExitThread(u32 code) {
    unsigned long self = pthread_self();
    if (g_main_thread_id && self != g_main_thread_id) {
        /* Both ARM bridge workers (imp_CreateThread -> arm_thread_main) and the legacy shim's
         * th_tramp workers are pthreads. pthread_exit terminates only this host thread; a join
         * through arm_guest_thread_join or the shim WaitForSingleObject then observes completion. */
        pthread_exit((void *) (ulong) code);
        __builtin_unreachable();
    }
    /* ExitThread on the host main thread must not leave the process running without its entry. */
    _exit((int) code);
}
S void *GetCurrentProcess(void) { return (void *) -1; }
S u32 GetCurrentProcessId(void) { return 1; }
S u32 GetCurrentThreadId(void) {
#if defined(__linux__) && defined(__i386__)
    return (u32) syscall(224 /* SYS_gettid on Linux i386 */);
#elif defined(__linux__) && defined(__arm__)
    return (u32) syscall(224 /* SYS_gettid on Linux ARM */);
#else
    return (u32) pthread_self();
#endif
}
S void SetConsoleCtrlHandler(void *fn, int add) { (void) fn; (void) add; }
S void *SetUnhandledExceptionFilter(void *fn) { (void) fn; return 0; }

/* This port registers one top-level game window in win32_ui. Resolve its existing UI accessors
 * instead of maintaining a divergent HWND registry here. */
typedef u32 (S *ui_get_hwnd_fn)(void);
typedef int (S *ui_get_text_fn)(u32, char *, int);
static u32 winproc_main_hwnd(void) {
    ui_get_hwnd_fn f = (ui_get_hwnd_fn) win32_ui_lookup("GetForegroundWindow");
    u32 h = f ? f() : 0;
    if (!h) { f = (ui_get_hwnd_fn) win32_ui_lookup("GetActiveWindow"); h = f ? f() : 0; }
    return h;
}
static int winproc_match(u32 hwnd, const char *cls, const char *title) {
    char b[4096]; ui_get_text_fn f;
    if (cls) {
        f = (ui_get_text_fn) win32_ui_lookup("GetClassNameA");
        if (!f || f(hwnd, b, sizeof b) <= 0 || strcasecmp(b, cls)) return 0;
    }
    if (title) {
        f = (ui_get_text_fn) win32_ui_lookup("GetWindowTextA");
        if (!f || f(hwnd, b, sizeof b) < 0 || strcasecmp(b, title)) return 0;
    }
    return 1;
}
S u32 FindWindowA(const char *cls, const char *title) {
    u32 h = winproc_main_hwnd(); return h && winproc_match(h, cls, title) ? h : 0;
}
S u32 FindWindowExA(u32 parent, u32 after, const char *cls, const char *title) {
    u32 h = winproc_main_hwnd();
    /* The backend has a single top-level game window and no enumerated child HWNDs here. */
    if (parent || after || !h) return 0;
    return winproc_match(h, cls, title) ? h : 0;
}
S u32 GetLastActivePopup(u32 hwnd) { return hwnd; }
S u32 WaitForInputIdle(void *process, u32 milliseconds) {
    (void) process; (void) milliseconds; return 0; /* WAIT_OBJECT_0: no child process to wait for */
}
extern int arm_guest_calln(u32 proc, unsigned nargs, const u32 *args, int *status) __attribute__((weak));
S int EnumWindows(u32 callback, long param) {
    u32 h = winproc_main_hwnd(), args[2]; int status = 0, result;
    if (!h) return 1;
    if (!callback || !arm_guest_calln) return 0;
    args[0] = h; args[1] = (u32) param;
    result = arm_guest_calln(callback, 2, args, &status);
    if (status != 0) return 0;
    return result != 0;
}
static void winproc_log_seh_chain(void) {
#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)
    u32 record = X86_ReadFsDword(0), depth;
    reasm_elf_log("[win32-seh] fs:[0]="); reasm_elf_log_hex(record);
    for (depth = 0; record != 0xffffffffu && depth < 16; ++depth) {
        u32 next, handler;
        if ((record & 3u) || !arm_va_is_mapped(record, 8)) {
            reasm_elf_log(" record=<unmapped/unaligned>\n"); return;
        }
        next = X86_ReadMemProcedure(record, 4);
        handler = X86_ReadMemProcedure(record + 4, 4);
        reasm_elf_log(" ["); reasm_elf_log_hex(record); reasm_elf_log("] next=");
        reasm_elf_log_hex(next); reasm_elf_log(" handler="); reasm_elf_log_hex(handler);
        if (next == record) { reasm_elf_log(" <cycle>\n"); return; }
        record = next;
    }
    reasm_elf_log(record == 0xffffffffu ? " <end>\n" : " <depth-limit>\n");
#else
    reasm_elf_log(" fs:[0]=<native-host-TLS; emulated TEB unavailable>\n");
#endif
}
static void winproc_log_exception(const char *api, u32 code, u32 flags, u32 a, u32 b, u32 c, u32 d) {
    reasm_elf_log("[win32-seh] "); reasm_elf_log(api); reasm_elf_log(" code="); reasm_elf_log_hex(code);
    reasm_elf_log(" flags="); reasm_elf_log_hex(flags); reasm_elf_log(" args=");
    reasm_elf_log_hex(a); reasm_elf_log(","); reasm_elf_log_hex(b); reasm_elf_log(",");
    reasm_elf_log_hex(c); reasm_elf_log(","); reasm_elf_log_hex(d); reasm_elf_log("\n");
    winproc_log_seh_chain();
}
S void RaiseException(u32 code, u32 flags, u32 nargs, const u32 *args) {
    winproc_log_exception("RaiseException", code, flags, nargs,
                          args && nargs > 0 ? args[0] : 0,
                          args && nargs > 1 ? args[1] : 0,
                          args && nargs > 2 ? args[2] : 0);
    reasm_elf_log("[win32-seh] unhandled guest exception; fail-closed exit 70\n");
    _exit(70);
}
S void RtlUnwind(void *target_frame, void *target_ip, void *exception_record, void *return_value) {
    winproc_log_exception("RtlUnwind", 0, 0, (u32)(ulong)target_frame,
                          (u32)(ulong)target_ip, (u32)(ulong)exception_record,
                          (u32)(ulong)return_value);
    reasm_elf_log("[win32-seh] guest SEH unwind unsupported; fail-closed exit 70\n");
    _exit(70);
}
S i32 UnhandledExceptionFilter(void *info) { (void) info; return 1; }  /* EXCEPTION_EXECUTE_HANDLER */
S int CreateProcessA(const char *app, char *cmd, void *pa, void *ta, int inh, u32 flags,
                     void *env, const char *dir, void *si, void *pi) {
    (void) app; (void) cmd; (void) pa; (void) ta; (void) inh; (void) flags;
    (void) env; (void) dir; (void) si; (void) pi;
    return 0;
}

/* --- advapi32 (minimo per il percorso CRT) --- */
S int GetUserNameA(char *buf, u32 *n) {
    const char *s = "enne2"; u32 i = 0;
    while (s[i] && i + 1 < *n) { buf[i] = s[i]; ++i; }
    buf[i] = 0; *n = i;
    return 1;
}
S long RegOpenKeyExA(void *key, const char *sub, u32 opt, u32 acc, void **out) {
    if (g_trace) { reasm_elf_log("[reg] RegOpenKeyExA sub="); reasm_elf_log(sub?sub:"(null)"); reasm_elf_log("\n"); }
    (void) key; (void) opt; (void) acc;
    if (out) *out = 0;
    if (sub && shim_contains(sub, "Blizzard Entertainment\\Starcraft")) {
        if (out) *out = SHIM_HKEY_STARCRAFT;
        return 0;                                   /* ERROR_SUCCESS: chiave esistente (vuota) */
    }
    return 2;                                       /* ERROR_FILE_NOT_FOUND per tutto il resto */
}
S long RegCloseKey(void *key) { (void) key; return 0; }
/* Il gioco legge HKCU\Software\Starcraft\ForceLowMem/ForceHighMem: senza registro i default
 * valgono. Queste API devono fallire in modo PULITO (ERROR_FILE_NOT_FOUND = 2), non essere
 * trampolini UNK (altrimenti il run si ferma su "API NON IMPLEMENTATA"). */
S long RegQueryValueExA(void *key, const char *name, u32 *reserved, u32 *type, u8 *data, u32 *len) {
    if (g_trace) { reasm_elf_log("[reg] RegQueryValueExA key="); reasm_elf_log_hex((unsigned)(ulong)key);
        reasm_elf_log(" name="); reasm_elf_log(name?name:"(null)"); reasm_elf_log("\n"); }
    (void) key; (void) name; (void) reserved; (void) type; (void) data; (void) len;
    return 2;
}
S long RegCreateKeyExA(void *key, const char *sub, u32 rsv, char *cls, u32 opt, u32 sam,
                       void *sa, void **out, u32 *disp) {
    if (g_trace) { reasm_elf_log("[reg] RegCreateKeyExA sub="); reasm_elf_log(sub?sub:"(null)"); reasm_elf_log("\n"); }
    (void) key; (void) sub; (void) rsv; (void) cls; (void) opt; (void) sam; (void) sa; (void) disp;
    if (out) *out = 0;
    return 2;
}
S long RegEnumKeyExA(void *key, u32 idx, char *name, u32 *nlen, u32 *rsv, char *cls, u32 *clen,
                     void *last) {
    (void) key; (void) idx; (void) name; (void) nlen; (void) rsv; (void) cls; (void) clen; (void) last;
    return 2;
}
S long RegQueryInfoKeyA(void *key, char *cls, u32 *clen, u32 *rsv, u32 *subn, u32 *submax,
                        u32 *clsmax, u32 *valn, u32 *valmax, u32 *secmax, u32 *lenmax, void *last) {
    (void) key; (void) cls; (void) clen; (void) rsv; (void) subn; (void) submax; (void) clsmax;
    (void) valn; (void) valmax; (void) secmax; (void) lenmax; (void) last;
    return 2;
}
S long RegSetValueExA(void *key, const char *name, u32 rsv, u32 type, const u8 *data, u32 len) {
    if (g_trace) { reasm_elf_log("[reg] RegSetValueExA name="); reasm_elf_log(name?name:"(null)");
        reasm_elf_log(" type="); reasm_elf_log_hex(type); reasm_elf_log(" len="); reasm_elf_log_hex(len);
        if (data && len && (type == 1 || type == 2)) { reasm_elf_log(" data=");
            { u32 i; for (i = 0; i < len && i < 64; ++i) { char c = (char) data[i]; char b[2]; b[0] = c ? c : '.'; b[1] = 0; if (c) reasm_elf_log(b); } }
        }
        reasm_elf_log("\n"); }
    (void) key; (void) name; (void) rsv; (void) type; (void) data; (void) len;
    return 2;
}
S long RegFlushKey(void *key) { (void) key; return 0; }
S long RegDeleteValueA(void *key, const char *name) { (void) key; (void) name; return 2; }
S int InitializeAcl(void *acl, u32 size, u32 rev) { (void) acl; (void) size; (void) rev; return 1; }
S int AddAccessAllowedAce(void *acl, u32 rev, u32 mask, void *sid) {
    (void) acl; (void) rev; (void) mask; (void) sid; return 1;
}
S int AddAccessDeniedAce(void *acl, u32 rev, u32 mask, void *sid) {
    (void) acl; (void) rev; (void) mask; (void) sid; return 1;
}
S int AllocateAndInitializeSid(void *id, u8 nsub, u32 a1, u32 a2, u32 a3, u32 a4,
                               u32 a5, u32 a6, u32 a7, u32 a8, void **sid) {
    (void) id; (void) nsub; (void) a1; (void) a2; (void) a3; (void) a4; (void) a5;
    (void) a6; (void) a7; (void) a8; if (sid) *sid = (void *) 1;
    return 1;
}
S void *FreeSid(void *sid) { (void) sid; return 0; }
S int OpenProcessToken(void *proc, u32 acc, void **tok) { (void) proc; (void) acc; if (tok) *tok = (void *) 1; return 1; }
S int GetTokenInformation(void *tok, u32 cls, void *buf, u32 len, u32 *ret) {
    (void) tok; (void) cls; (void) buf; (void) len; if (ret) *ret = 0; return 0;
}

/* --- user32/gdi32: solo il minimo che il gioco tocca prima di WinMain --- */
S int MessageBoxA(void *hwnd, const char *text, const char *cap, u32 type) {
    (void) hwnd; (void) type;
    reasm_elf_log("[win32] MessageBoxA: "); reasm_elf_log(cap ? cap : "");
    reasm_elf_log(" / "); reasm_elf_log(text ? text : ""); reasm_elf_log("\n");
    return 1;                                        /* IDOK */
}
S void OutputDebugStringA(const char *s) {
    reasm_elf_log("[win32] OutputDebugStringA: ");
    reasm_elf_log(s ? s : "(null)");
    reasm_elf_log("\n");
}
/* Dimensioni dello schermo: DEVONO essere quelle reali della modalita' video (canvas 640x480),
 * non un 640x640 inventato. Il gioco le usa per calcolare la dimensione della finestra
 * (CreateWindowEx 640x640 con il bug), il RECT client, il ClipCursor e le soglie di edge-scroll del
 * mouse: con una altezza sbagliata il cursore finisce in una fascia che il gioco considera "bordo
 * inferiore" e la visuale scrolla da sola. */
extern unsigned char *host_elf_canvas(int *, int *, int *) __attribute__((weak));
S int GetSystemMetrics(int idx) {
    int w = 640, h = 480, pitch = 0;
    if (host_elf_canvas && host_elf_canvas(&w, &h, &pitch) == 0 && w > 0 && h > 0) { }
    else { w = 640; h = 480; }
    switch (idx) {
    case 0:  return w;                        /* SM_CXSCREEN */
    case 1:  return h;                        /* SM_CYSCREEN */
    case 78: return w;                        /* SM_CXVIRTUALSCREEN */
    case 79: return h;                        /* SM_CYVIRTUALSCREEN */
    default: return 0;
    }
}
S u32 GetSysColor(int idx) { (void) idx; return 0x00C0C0C0u; }

/* --- tabella dei nomi coperti --- */
typedef struct { const char *name; void *fn; } ShimEnt;
#define E(n) { #n, (void *) n }
static const ShimEnt g_shim[] = {
    E(GetProcessHeap), E(HeapCreate), E(HeapDestroy), E(HeapAlloc), E(HeapFree),
    E(HeapReAlloc), E(HeapSize),
    E(VirtualAlloc), E(VirtualFree), E(VirtualProtect), E(VirtualQuery),
    E(GetCommandLineA), E(GetEnvironmentStrings), E(GetEnvironmentStringsW),
    E(FreeEnvironmentStringsA), E(FreeEnvironmentStringsW),
    E(GetStartupInfoA), E(GetVersionExA), E(GetVersion), E(GetSystemInfo),
    E(GlobalMemoryStatus),
    E(GetModuleHandleA), E(GetModuleFileNameA), E(LoadLibraryA), E(GetProcAddress), E(FreeLibrary),
    E(GetLastError), E(SetLastError),
    E(TlsAlloc), E(TlsFree), E(TlsGetValue), E(TlsSetValue),
    E(InitializeCriticalSection), E(InitializeCriticalSectionAndSpinCount),
    E(DeleteCriticalSection), E(EnterCriticalSection), E(LeaveCriticalSection),
    E(InterlockedExchange), E(InterlockedIncrement), E(InterlockedDecrement),
    E(CreateEventA), E(SetEvent), E(ResetEvent), E(WaitForSingleObject), E(WaitForMultipleObjects),
    E(CreateThread), E(Sleep),
    E(GetTickCount), E(QueryPerformanceFrequency), E(QueryPerformanceCounter),
    E(GetSystemTimeAsFileTime), E(GetLocalTime), E(FileTimeToSystemTime),
    E(FileTimeToLocalFileTime), E(CompareFileTime), E(GetTimeZoneInformation),
    E(GetStdHandle), E(SetStdHandle), E(GetFileType), E(SetHandleCount),
    E(WriteFile), E(ReadFile), E(CloseHandle), E(FlushFileBuffers), E(SetEndOfFile),
    E(SetFilePointer), E(GetFileSize), E(CreateFileA), E(GetFileAttributesA),
    E(SetFileAttributesA), E(CreateDirectoryA), E(DeleteFileA), E(GetDriveTypeA),
    E(GetLogicalDriveStringsA), E(GetDiskFreeSpaceA),
    E(FindFirstFileA), E(FindNextFileA), E(FindClose),
    E(GetComputerNameA), E(SetEnvironmentVariableA), E(FormatMessageA), E(lstrcpynA),
    E(MulDiv), E(IsBadReadPtr), E(IsBadWritePtr),
    E(GetACP), E(GetOEMCP), E(GetCPInfo), E(IsDBCSLeadByte),
    E(MultiByteToWideChar), E(WideCharToMultiByte),
    E(GetStringTypeA), E(GetStringTypeW), E(LCMapStringA), E(LCMapStringW),
    E(CompareStringA), E(CompareStringW), E(GetLocaleInfoA), E(GetLocaleInfoW),
    E(IsValidLocale), E(IsValidCodePage), E(EnumSystemLocalesA), E(GetUserDefaultLCID),
    E(GetUserDefaultLangID), E(GetSystemTime), E(SystemTimeToFileTime),
    E(GetCurrentDirectoryA), E(GetFullPathNameA), E(GetTempPathA), E(GetTempFileNameA),
    E(SetThreadPriority), E(GetCurrentThread), E(GetExitCodeProcess), E(FreeResource),
    E(CreateFileMappingA), E(MapViewOfFile), E(UnmapViewOfFile), E(GetVolumeInformationA),
    E(GetDateFormatA), E(GetTimeFormatA),
    E(ExitProcess), E(TerminateProcess), E(ExitThread), E(GetCurrentProcess),
    E(GetCurrentProcessId), E(GetCurrentThreadId), E(SetConsoleCtrlHandler),
    E(EnumWindows), E(FindWindowA), E(FindWindowExA), E(GetLastActivePopup), E(WaitForInputIdle),
    E(RaiseException), E(RtlUnwind),
    E(GetSaveFileNameA), E(GetOpenFileNameA), E(SHGetSpecialFolderLocation),
    E(SHGetPathFromIDListA), E(ShellExecuteA), E(FindExecutableA),
    E(SetUnhandledExceptionFilter), E(UnhandledExceptionFilter), E(CreateProcessA),
    E(GetUserNameA), E(RegOpenKeyExA), E(RegCloseKey), E(RegQueryValueExA),
    E(SetTimer), E(KillTimer),
    E(RegCreateKeyExA), E(RegEnumKeyExA), E(RegQueryInfoKeyA), E(RegSetValueExA),
    E(RegFlushKey), E(RegDeleteValueA), E(InitializeAcl),
    E(AddAccessAllowedAce), E(AddAccessDeniedAce), E(AllocateAndInitializeSid), E(FreeSid),
    E(OpenProcessToken), E(GetTokenInformation),
    E(OutputDebugStringA), E(MessageBoxA), E(GetSystemMetrics), E(GetSysColor),
};
#undef E

/* T3 extension ABI: another module may DEFINE this NULL-terminated weak table.
 * No registration call, constructors or edits to this file are required.
 * Built-in implementations retain precedence; dll==NULL is legacy name-only lookup.
 * P1 (2026-10-11): il tipo e' ora quello dichiarato in win32_game.h (tre campi identici:
 * dll/name/fn) per evitare due typedef concorrenti sullo stesso simbolo. */
typedef Win32GameApi Win32ExtraApi;
extern const Win32ExtraApi win32_extra_apis[] __attribute__((weak));

void *win32_shim_lookup_dll(const char *dll, const char *name) {
    unsigned i;
    void *fn;
    if (!name) return 0;
    if (name[0] == '#') return 0;                     /* ordinali: solo storm */
    for (i = 0; i < sizeof g_shim / sizeof g_shim[0]; ++i)
        if (strcasecmp(g_shim[i].name, name) == 0) return g_shim[i].fn;
    fn = win32_ui_lookup(name);
    if (fn) return fn;
    if (win32_extra_apis) {
        const Win32ExtraApi *e;
        for (e = win32_extra_apis; e->name; ++e)
            if ((!dll || !e->dll || !strcasecmp(dll, e->dll)) &&
                !strcasecmp(name, e->name)) return e->fn;
    }
    return 0;
}

void *win32_shim_lookup(const char *name) {
    return win32_shim_lookup_dll(0, name);
}

const char *const *win32_shim_names(void) {
    static const char *names[sizeof g_shim / sizeof g_shim[0] + 1];
    static int done;
    unsigned i;
    if (!done) {
        for (i = 0; i < sizeof g_shim / sizeof g_shim[0]; ++i) names[i] = g_shim[i].name;
        names[sizeof g_shim / sizeof g_shim[0]] = 0;
        done = 1;
    }
    return names;
}
