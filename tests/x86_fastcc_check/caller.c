#include <stdint.h>
#include <stdio.h>

struct cpu { uint32_t words[36]; };
extern void fnA_cdecl(struct cpu *cpu);
extern void fastcc_destination(struct cpu *cpu);
static struct cpu *expected;

void *c_callback(struct cpu *cpu) {
    if (cpu != expected) return 0;
    return (void *)fastcc_destination;
}

int main(void) {
    struct cpu cpu = {{0}};
    expected = &cpu;
    fnA_cdecl(&cpu);
    if (cpu.words[0] != 0x12345678) {
        fprintf(stderr, "fastcc target not reached or bad cpu pointer: %08x\n", cpu.words[0]);
        return 1;
    }
    return 0;
}
