#define _GNU_SOURCE
#include "../../runtime/common/indirect_dispatch.h"
#include "../../runtime/common/game_memory.h"
#include "../../runtime/common/cpu_llasm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Standalone microbench: the resolver's translated table and CPU/memory helpers are fixtures. */
void bench_target(void);
const uint32_t reasm_va_va[] = { 0x00401000u };
void *const reasm_va_fn[] = { (void *)(uintptr_t)&bench_target };
const uint32_t reasm_va_map_n = 1;
char reasm_text_lo[1], reasm_text_hi[1];
static uint64_t read_calls;
volatile uint32_t g_vm_generation;
extern int proc_range(uint32_t, uint32_t, int);
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
static const char host_rodata[] = "shadow page table benchmark";
static char host_data[128];
void bench_target(void) {}
_cpu *cpu_llasm_current(void) { return 0; }
uint32_t cpu_llasm_read32(uint32_t a) { (void)a; ++read_calls; return 0; }

static double now_sec(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static void report(const char *name, double start, uint64_t n) {
    double elapsed = now_sec() - start;
    printf("%s calls=%llu seconds=%.9f Mops=%.3f ns/call=%.3f\n", name,
           (unsigned long long)n, elapsed, (double)n / elapsed / 1e6,
           elapsed * 1e9 / (double)n);
}
int main(int argc, char **argv) {
    uint64_t n = argc > 1 ? strtoull(argv[1], 0, 0) : 50000000ull, i;
    volatile uintptr_t sink = 0;
    double t;
    if (arm_register_region(0x10000000u, 0x2000u, "bench") != 0) return 2;
    if (reasm_indirect_resolve(0x00401000u) != (void *)(uintptr_t)&bench_target) return 3;
    t = now_sec();
    for (i = 0; i < n; ++i) sink += (uintptr_t)reasm_indirect_resolve(0x00401000u);
    report("dispatch_hit", t, n);
    t = now_sec();
    for (i = 0; i < n; ++i) sink += (unsigned)arm_va_is_mapped(0x10000020u, 64u);
    report("arm_va_is_mapped", t, n);
    if (!proc_range((uint32_t)(uintptr_t)host_rodata, sizeof host_rodata, 0)) return 5;
    t = now_sec();
    for (i = 0; i < n; ++i) sink += (unsigned)proc_range((uint32_t)(uintptr_t)host_rodata, sizeof host_rodata, 0);
    report("proc_range_rodata_read", t, n);
    if (proc_range((uint32_t)(uintptr_t)host_rodata, sizeof host_rodata, 1)) return 6;
    if (!proc_range((uint32_t)(uintptr_t)host_data, sizeof host_data, 1)) return 7;
    t = now_sec();
    for (i = 0; i < n; ++i) sink += (unsigned)proc_range((uint32_t)(uintptr_t)host_data, sizeof host_data, 1);
    report("proc_range_host_write", t, n);
    printf("sink=%llu unexpected_read_calls=%llu\n", (unsigned long long)sink,
           (unsigned long long)read_calls);
    return read_calls ? 4 : 0;
}
