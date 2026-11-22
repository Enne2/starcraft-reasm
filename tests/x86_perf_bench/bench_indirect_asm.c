#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NENTRIES 4096u
struct cache_entry { uint32_t key; void *val; };
static struct cache_entry cache[NENTRIES];
static unsigned char cpu_state[160] __attribute__((aligned(16)));
extern void reasm_indirect(void);

__attribute__((noinline)) static void bench_target(void) { __asm__ volatile("" ::: "memory"); }
void *reasm_indirect_resolve(uint32_t target) { return (void *)(uintptr_t)target; }
void reasm_indirect_unresolved(void *cpu, uint32_t target) { (void)cpu; (void)target; abort(); }
uint32_t cpu_llasm_pop32(void *cpu) { (void)cpu; return 0; }
void storm_313_arm_body(void *cpu) { (void)cpu; }
void storm_334_arm_body(void *cpu) { (void)cpu; }
void storm_552_arm_body(void *cpu) { (void)cpu; }
void __sse2_available_init_body(void *cpu) { (void)cpu; }
void __sse2_available_init_storm_body(void *cpu) { (void)cpu; }

static double now_sec(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    uint64_t n = argc > 1 ? strtoull(argv[1], 0, 0) : 50000000ull, i;
    uint32_t target = (uint32_t)(uintptr_t)bench_target;
    unsigned index = (target >> 2) & 4095u;
    double t;
    *(uint32_t *)(cpu_state + 16) = 0x12345678u;
    *(uint32_t *)(cpu_state + 136) = target;
    *(void **)(cpu_state + 144) = cache;
    cache[index].key = target;
    cache[index].val = (void *)(uintptr_t)target;
    t = now_sec();
    for (i = 0; i < n; ++i) {
        __asm__ volatile("movl %0, %%ecx\n\tcall reasm_indirect"
                         : : "r"(cpu_state) : "eax", "ecx", "edx", "memory", "cc");
    }
    t = now_sec() - t;
    if (*(uint32_t *)(cpu_state + 140) != 0x12345678u) return 2;
    printf("calls=%llu seconds=%.9f Mops=%.3f ns/call=%.3f\n",
           (unsigned long long)n, t, (double)n/t/1e6, t*1e9/(double)n);
    return 0;
}
