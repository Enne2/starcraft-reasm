/* runtime/arm/game_memory.c — fixed-VA guest mappings and the low arena (ARMv6 Linux).
 *
 * See game_memory.h for the "why". Implementation notes worth keeping:
 *  - MAP_FIXED is the only portable way to obtain an exact VA on 32-bit ARM Linux. It silently
 *    replaces an existing mapping, so every call is followed by a pointer check: a mismatch is
 *    reported, never ignored (a wrong VA turns into corrupted guest state much later).
 *  - The arena is a single reservation: bump allocation is enough because runtime structures
 *    (cpu, TEB/PEB, bridge tables) live for the whole process. Guest allocators (storm's
 *    SMemAlloc) are translated code and manage their own heap inside the same VAs.
 *  - No MAP_32BIT: that flag exists only on x86_64. On ARMv6 the address space *is* 32-bit; the
 *    problem is not reachability but the 0x80000000 sign convention (see arm_selftest_low).
 */
#include "game_memory.h"
#include "guest_cpu_helpers.h"

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

extern void  *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
extern void   _exit(int);

#define PROT_RW      3
#define MAP_FIXED_PRIVATE_ANON 0x32   /* MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS */

#define MAX_REGIONS  32

typedef struct { uint32_t base, size; const char *what; } Region;

static Region g_regions[MAX_REGIONS];
static int    g_regions_n;

static uint32_t g_arena_next;
static uint32_t g_arena_end;
#define SHADOW_PAGE_SHIFT 12u
#define SHADOW_PAGE_COUNT (1u << 20)
static uint8_t g_shadow_pages[SHADOW_PAGE_COUNT];

void arm_mem_log(const char *s) {
    if (s) (void) write(2, s, strlen(s));
}

static void put_hex(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char b[10];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; ++i) b[2 + i] = hx[(v >> (28 - 4 * i)) & 0xF];
    (void) write(2, b, sizeof b);
}

void arm_mem_log_hex(const char *prefix, uint32_t v) {
    arm_mem_log(prefix);
    put_hex(v);
    arm_mem_log("\n");
}

static void put_dec(uint32_t v) {
    char b[12];
    int i = 0;
    if (!v) { (void) write(2, "0", 1); return; }
    while (v) { b[i++] = (char) ('0' + v % 10u); v /= 10u; }
    while (i) (void) write(2, &b[--i], 1);
}

static void note_region(uint32_t base, uint32_t size, const char *what) {
    if (g_regions_n >= MAX_REGIONS) return;
    g_regions[g_regions_n].base = base;
    g_regions[g_regions_n].size = size;
    g_regions[g_regions_n].what = what;
    g_regions_n++;
}

int arm_map_at(uint32_t va, uint32_t size) {
    void *p;
    uint32_t page = 0x1000u;
    if (!size) return -1;
    if (va >= ARM_LOW_LIMIT) {
        arm_mem_log("[arm-mem] RIFIUTATO: VA non basso ");
        put_hex(va);
        arm_mem_log("\n");
        return -1;
    }
    size = (size + page - 1) & ~(page - 1);
    if (va + size > ARM_LOW_LIMIT) {
        arm_mem_log("[arm-mem] RIFIUTATO: la mappatura supera 0x80000000\n");
        return -1;
    }
    p = mmap((void *) (unsigned long) va, size, PROT_RW,
             MAP_FIXED_PRIVATE_ANON, -1, 0);
    if (p != (void *) (unsigned long) va) {
        arm_mem_log("[arm-mem] mmap FALLITA a ");
        put_hex(va);
        arm_mem_log(" size ");
        put_hex(size);
        arm_mem_log("\n");
        return -1;
    }
    /* MAP_ANONYMOUS pages are zero-filled by the kernel: a MAP_FIXED over a previously used VA
     * therefore starts clean, and image content is written by the caller afterwards. */
    note_region(va, size, "fixed");
    arm_shadow_set_range(va, size, ARM_SHADOW_GUEST | ARM_SHADOW_R | ARM_SHADOW_W);
    return 0;
}

int arm_arena_init(void) {
    if (g_arena_next) return 0;
    if (arm_map_at(ARM_ARENA_BASE, ARM_ARENA_SIZE) != 0) return -1;
    g_arena_next = ARM_ARENA_BASE;
    g_arena_end = ARM_ARENA_BASE + ARM_ARENA_SIZE;
    return 0;
}

void *arm_alloc_low(uint32_t size, uint32_t align) {
    uint32_t a, p;
    if (!size) return 0;
    if (arm_arena_init() != 0) return 0;
    if (align < 4) align = 4;
    if (align & (align - 1)) {          /* not a power of two */
        arm_mem_log("[arm-mem] allineamento non potenza di due\n");
        return 0;
    }
    a = (g_arena_next + align - 1) & ~(align - 1);
    p = a + ((size + 7u) & ~7u);
    if (p > g_arena_end) {
        arm_mem_log("[arm-mem] arena esaurita (richiesta ");
        put_dec(size);
        arm_mem_log(" byte)\n");
        return 0;
    }
    g_arena_next = p;
    return (void *) (unsigned long) a;
}

uint32_t arm_arena_used(void) { return g_arena_next ? (g_arena_next - ARM_ARENA_BASE) : 0; }
uint32_t arm_arena_free(void) { return g_arena_next ? (g_arena_end - g_arena_next) : 0; }

int arm_va_is_low(uint32_t va) { return va != 0 && va < ARM_LOW_LIMIT; }

int arm_register_region(uint32_t base, uint32_t size, const char *what) {
    if (!size || base + size < base) return -1;
    if (base >= ARM_LOW_LIMIT) {
        arm_mem_log_hex("[arm-mem] regione rifiutata oltre 0x80000000: ", base);
        return -1;
    }
    if (g_regions_n >= MAX_REGIONS) {
        arm_mem_log("[arm-mem] troppe regioni registrate\n");
        return -1;
    }
    note_region(base, size, what);
    arm_shadow_set_range(base, size, ARM_SHADOW_GUEST | ARM_SHADOW_R | ARM_SHADOW_W);
    return 0;
}

void arm_shadow_set_range(uint32_t base, uint32_t size, uint8_t perm) {
    arm_shadow_add_range(base, size, perm);
    if (!size) return;
    {
        uint64_t end = (uint64_t)base + size;
        uint32_t first = base >> SHADOW_PAGE_SHIFT;
        uint32_t last = (uint32_t)((end - 1u) >> SHADOW_PAGE_SHIFT);
        uint32_t i;
        if (end > 0x100000000ull) return;
        for (i = first; i <= last; ++i) __atomic_store_n(&g_shadow_pages[i], perm, __ATOMIC_RELAXED);
    }
}

void arm_shadow_add_range(uint32_t base, uint64_t size, uint8_t perm) {
    uint64_t end = (uint64_t)base + size;
    uint32_t first, last, i;
    if (!size || end > 0x100000000ull) return;
    first = base >> SHADOW_PAGE_SHIFT;
    last = (uint32_t)((end - 1u) >> SHADOW_PAGE_SHIFT);
    for (i = first; i <= last; ++i) __atomic_fetch_or(&g_shadow_pages[i], perm, __ATOMIC_RELAXED);
}

void arm_shadow_clear_host(void) {
    uint32_t i;
    for (i = 0; i < SHADOW_PAGE_COUNT; ++i) {
        uint8_t old = __atomic_load_n(&g_shadow_pages[i], __ATOMIC_RELAXED);
        __atomic_store_n(&g_shadow_pages[i], old & ARM_SHADOW_GUEST, __ATOMIC_RELAXED);
    }
}

int arm_shadow_check_range(uint32_t base, uint32_t size, uint8_t required) {
    uint64_t end = (uint64_t)base + (size ? size : 1u);
    uint32_t i, last;
    if (end > 0x100000000ull) return 0;
    i = base >> SHADOW_PAGE_SHIFT;
    last = (uint32_t)((end - 1u) >> SHADOW_PAGE_SHIFT);
    for (; i <= last; ++i)
        if ((__atomic_load_n(&g_shadow_pages[i], __ATOMIC_RELAXED) & required) != required) return 0;
    return 1;
}

int arm_va_is_mapped(uint32_t va, uint32_t len) {
    uint32_t span = len ? len : 1u;
    uint32_t end = va + span, page, last;
    if (va >= ARM_LOW_LIMIT || end < va || end > ARM_LOW_LIMIT) return 0;
    page = va >> SHADOW_PAGE_SHIFT;
    last = (end - 1u) >> SHADOW_PAGE_SHIFT;
    for (; page <= last; ++page)
        if ((__atomic_load_n(&g_shadow_pages[page], __ATOMIC_RELAXED) & ARM_SHADOW_GUEST) == 0) return 0;
    return 1;
}

int arm_selftest_low(void) {
    int bad = 0;
    uint32_t probes[4];
    int i;
    probes[0] = ARM_ARENA_BASE;
    probes[1] = ARM_ARENA_BASE + ARM_ARENA_SIZE - 8;
    probes[2] = ARM_STACK_BASE;
    probes[3] = 0x00400000u;
    for (i = 0; i < 4; ++i) {
        uint32_t v = probes[i];
        if (v >= ARM_LOW_LIMIT) { bad++; continue; }
        /* ~v must be negative when interpreted as int32 (this is the storm.dll contract). */
        if ((int32_t) (~v) >= 0) bad++;
    }
    if (bad) {
        arm_mem_log("[arm-mem] SELFTEST FALLITO: ~ptr non e' negativo in ");
        put_dec((uint32_t) bad);
        arm_mem_log(" casi\n");
        return -1;
    }
    return 0;
}
