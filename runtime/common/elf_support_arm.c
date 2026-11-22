/* runtime/arm/elf_support_arm.c — logs, module/proc lookup and loader hooks for the ARM runtime.
 *
 * Ported from ~/dev/reasm-toolkit/runtime/elf/{elf_support.c,teb.c}. Both originals are part of
 * the x86 ELF runtime: elf_support.c is already architecture-neutral C (only write(2)/strlen),
 * teb.c is not (set_thread_area, %fs). So the log half is a straight port and the TEB half is
 * re-implemented on top of the emulated TEB from x86_helpers.c.
 *
 * Deliberate behaviour kept from the toolkit:
 *  - never a stub that returns for an unimplemented import (rule D19): log + _exit(3);
 *  - loader_init() with no PE header table logs and returns instead of pretending success.
 */
#include "elf_support_arm.h"
#include "bridge_arm.h"
#include "game_memory_arm.h"
#include "x86_helpers.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

extern void _exit(int);

/* ---- log ------------------------------------------------------------------------------- */

static void put(const char *s) {
    size_t n = strlen(s);
    while (n) {
        ssize_t w = write(2, s, n);
        if (w <= 0) break;
        s += w;
        n -= (size_t) w;
    }
}

static void putu(unsigned int v) {
    char b[12];
    int i = 0;
    if (!v) { (void) write(2, "0", 1); return; }
    while (v) { b[i++] = (char) ('0' + v % 10u); v /= 10u; }
    while (i) (void) write(2, &b[--i], 1);
}

void reasm_elf_log(const char *s) { if (s) put(s); }

void reasm_elf_log_hex(unsigned int v) {
    static const char hx[] = "0123456789ABCDEF";
    char b[10];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; ++i) b[2 + i] = hx[(v >> (28 - 4 * i)) & 0xF];
    (void) write(2, b, sizeof b);
}

void reasm_elf_log_iat(int done, int missing, int total) {
    reasm_elf_log("[reasm-elf] IAT nativa: ");
    putu((unsigned) done);
    reasm_elf_log(" implementate, ");
    putu((unsigned) missing);
    reasm_elf_log(" trampolini (worklist) su ");
    putu((unsigned) total);
    reasm_elf_log(" import\n");
}

void reasm_elf_unimplemented(const char *name, unsigned int caller) {
    reasm_elf_log("[reasm-elf] API NON IMPLEMENTATA: ");
    reasm_elf_log(name ? name : "(null)");
    reasm_elf_log("  (chiamante ");
    reasm_elf_log_hex(caller);
    reasm_elf_log(") -> stop controllato\n");
    _exit(3);
}

/* ---- module / proc --------------------------------------------------------------------- */

#define EXE_BASE   0x00400000u
#define STORM_BASE 0x15000000u

static int name_ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    if (lx > ls) return 0;
    s += ls - lx;
    while (*suffix) {
        char a = *s++, b = *suffix++;
        if (a >= 'A' && a <= 'Z') a = (char) (a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char) (b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

/* System DLLs are not loaded: their entry points are bridge thunks looked up by *name* (see
 * reasm_elf_GetProcAddress), so a module handle only has to be non-zero and distinguishable.
 * The values sit in an unused low window (< 0x80000000) and are never dereferenced. */
static const char *const k_sys_dlls[] = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll", "shell32.dll",
    "imm32.dll", "winmm.dll", "ole32.dll", "ws2_32.dll", "comctl32.dll", "version.dll",
    "ddraw.dll", "dsound.dll", "smackw32.dll", "mss32.dll", "storm.dll"
};
#define SYS_DLL_COUNT ((int) (sizeof k_sys_dlls / sizeof k_sys_dlls[0]))
#define FAKE_BASE 0x7000A000u

static int is_system_dll(const char *n) {
    int i;
    for (i = 0; i < SYS_DLL_COUNT; ++i)
        if (name_ends_with(n, k_sys_dlls[i])) return i + 1;
    return 0;
}

void *reasm_elf_GetModuleHandleA(const char *n) {
    int idx;
    /* One image pair, both at their original VAs. */
    if (!n || !n[0]) return (void *) (uintptr_t) EXE_BASE;
    if (name_ends_with(n, "starcraft.exe")) return (void *) (uintptr_t) EXE_BASE;
    if (name_ends_with(n, "storm.dll")) return (void *) (uintptr_t) STORM_BASE;
    idx = is_system_dll(n);
    if (idx) return (void *) (uintptr_t) (FAKE_BASE + (uint32_t) idx * 0x100u);
    reasm_elf_log("[reasm-arm] GetModuleHandleA di un modulo non presente: ");
    reasm_elf_log(n);
    reasm_elf_log("\n");
    return 0;
}

void *reasm_elf_LoadLibraryA(const char *n) {
    /* Both images are already mapped; "loading" is a lookup. A missing module is fatal rather
     * than a fake handle (D19). */
    void *h = reasm_elf_GetModuleHandleA(n);
    if (!h) reasm_elf_unimplemented(n, (unsigned) (uintptr_t) __builtin_return_address(0));
    return h;
}

/* Parse "ordinal_<hexdll>_<n>" (SRW's MakeOrdinalSymbol) into n. Returns 0 if it does not match. */
static unsigned parse_ordinal_symbol(const char *s) {
    unsigned v = 0;
    if (strncmp(s, "ordinal_", 8) != 0) return 0;
    s += 8;
    while (*s && *s != '_') s++;                 /* skip the hex-encoded DLL name */
    if (*s != '_') return 0;
    s++;
    if (!*s) return 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10u + (unsigned) (*s - '0');
    }
    return v;
}

void *reasm_elf_GetProcAddress(void *m, const char *n) {
    (void) m;
    if (!n || !n[0]) return 0;
    if (n[0] == '#') {                            /* ordinal import: "#101" */
        unsigned ord = 0;
        const char *p = n + 1;
        for (; *p; ++p) {
            if (*p < '0' || *p > '9') return 0;
            ord = ord * 10u + (unsigned) (*p - '0');
        }
        return (void *) (uintptr_t) arm_bridge_ordinal_va(ord);
    }
    {
        unsigned ord = parse_ordinal_symbol(n);
        if (ord) {
            uint32_t va = arm_bridge_ordinal_va(ord);
            if (va) return (void *) (uintptr_t) va;
        }
        return arm_bridge_lookup_thunk(n);
    }
}

/* Address of the guest-ABI thunk for a name, 0 if there is none. This is what a *guest* may be
 * handed by GetProcAddress: never a C shim symbol, whose AAPCS arguments would be read from
 * registers while the guest pushed them on its own stack. */
void *arm_bridge_lookup_thunk(const char *n) {
    uint32_t i;
    if (!n || !n[0]) return 0;
    for (i = 0; i < g_bridge_symbols_n; ++i)
        if (strcmp(g_bridge_symbols[i].name, n) == 0) return g_bridge_symbols[i].fn;
    return 0;
}

/* ---- loader / TEB hooks ---------------------------------------------------------------- */

/* PE headers to copy at the image VAs. Generated by the toolkit's elf_objects.py
 * (pehdr_table.c) in the x86 pipeline; on ARM the same table is usable as soon as the section
 * objects are produced with `objcopy -O elf32-littlearm -B arm`. Declared here so the port has
 * a single place to plug it in, and so an absent table is reported instead of ignored. */
typedef struct { unsigned int va; unsigned int size; void *blob; } ArmPeHdr;
extern const ArmPeHdr g_pehdrs[] __attribute__((weak));
extern const int g_pehdrs_n __attribute__((weak));

void loader_init(void) {
    int i, n = &g_pehdrs_n ? g_pehdrs_n : 0;
    if (!n) {
        reasm_elf_log("[loader] nessuna tabella di header PE (g_pehdrs assente): "
                      "le pagine delle immagini vanno mappate da chi genera le sezioni\n");
        return;
    }
    for (i = 0; i < n; ++i) {
        unsigned int va = g_pehdrs[i].va;
        unsigned int size = g_pehdrs[i].size ? g_pehdrs[i].size : 0x1000u;
        if (arm_map_at(va, size) != 0) {
            reasm_elf_log("[loader] mappatura della pagina header FALLITA a ");
            reasm_elf_log_hex(va);
            reasm_elf_log("\n");
            continue;
        }
        if (g_pehdrs[i].blob) memcpy((void *) (uintptr_t) va, g_pehdrs[i].blob, size);
    }
}

void teb_init(void) {
    /* On i386 this also loaded %fs via set_thread_area; here it only builds the emulated page. */
    if (arm_teb_init() != 0)
        reasm_elf_log("[loader] TEB emulato non inizializzabile\n");
}

void teb_set_stack(unsigned int stack_base, unsigned int stack_limit) {
    arm_teb_set_stack(stack_base, stack_limit);
}

char *teb_envA(void) {
    /* Same minimal environment block the x86 runtime exposes through the PEB. */
    static char env[] = "PATH=/usr/local/bin:/usr/bin:/bin\0TEMP=/tmp\0\0";
    return env;
}
