#include <stdint.h>
#include <stdlib.h>

struct cache_entry { uint32_t key; void *val; };
struct cpu { uint32_t r[36]; struct cache_entry *cache; };
extern void reasm_indirect(void);
extern void reasm_extern(void);
extern void test_bridge(void);
extern void arm_com_thunk_7(void);
extern void x86_call_fast(void (*fn)(void), struct cpu *cpu);
extern void x86_call_extern(struct cpu *cpu, void (*fn)(struct cpu *));
extern void x86_call_bridge(void (*fn)(void), struct cpu *cpu);
extern void x86_call_com(void (*fn)(void), struct cpu *cpu);

static struct cache_entry cache[4096];
static struct cpu *expected;
static unsigned hits;
static void fast_target(void) { uint32_t v; __asm__ volatile("movl %%ecx,%0":"=r"(v)); if ((struct cpu *)(uintptr_t)v != expected) abort(); ++hits; }
static void extern_target(struct cpu *cpu) { if (cpu != expected) abort(); }
static void *resolve(uint32_t va) { return va == 0x1234u || va == 0x5678u ? fast_target : 0; }
void *reasm_indirect_resolve(uint32_t va) { return resolve(va); }
void reasm_indirect_unresolved(struct cpu *cpu, uint32_t va) { (void)cpu; (void)va; abort(); }
uint32_t cpu_llasm_pop32(struct cpu *cpu) { return cpu->r[4]; }
uint32_t arm_bridge_common_c(struct cpu *cpu, uint32_t index) { if (cpu != expected || index != 0) abort(); return 0x1234; }
uint32_t arm_com_bridge_common_c(struct cpu *cpu, unsigned index) { if (cpu != expected || index != 7) abort(); return 0x1234; }
extern void reasm_extern(void);
void storm_313_arm_body(struct cpu *cpu) { (void)cpu; }
void storm_334_arm_body(struct cpu *cpu) { (void)cpu; }
void storm_552_arm_body(struct cpu *cpu) { (void)cpu; }
void __sse2_available_init_body(struct cpu *cpu) { (void)cpu; }
void __sse2_available_init_storm_body(struct cpu *cpu) { (void)cpu; }

int main(void) {
    struct cpu cpu = {{0}}; expected = &cpu;
    cpu.r[4] = 0x1234; cpu.r[2] = 0x1234; cpu.r[34] = 0x5678;
    cpu.cache = cache;
    cache[(0x5678u >> 2) & 4095u].key = 0x5678u;
    cache[(0x5678u >> 2) & 4095u].val = fast_target;
    x86_call_fast(reasm_indirect, &cpu);
    cpu.r[4] = 0x1234;
    x86_call_extern(&cpu, extern_target);
    x86_call_bridge(test_bridge, &cpu);
    x86_call_com(arm_com_thunk_7, &cpu);
    if (hits != 4) return 1;
    return 0;
}
