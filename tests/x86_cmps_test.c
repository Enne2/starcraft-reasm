#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "llasm_cpu.h"

extern uint32_t x86_repe_cmpsd(_cpu *cpu);
extern uint32_t x86_repne_cmpsd(_cpu *cpu);
extern uint32_t x86_repe_cmpsw(_cpu *cpu);
extern uint32_t x86_repne_cmpsw(_cpu *cpu);

int main(void) {
    _cpu cpu;
    uint32_t a[4] = {0, 1, 2, 3};
    uint32_t b[4] = {0, 99, 2, 3};
    uint32_t c[4] = {0, 1, 2, 3};
    uint16_t w1[4] = {0, 10, 20, 30};
    uint16_t w2[4] = {0, 99, 20, 30};

    /* REPE CMPSD with equal element 0 and mismatch at element 1: must NOT stop at 0! */
    memset(&cpu, 0, sizeof(cpu));
    cpu._ecx = 4;
    cpu._esi = (uint32_t)(uintptr_t)a;
    cpu._edi = (uint32_t)(uintptr_t)b;
    x86_repe_cmpsd(&cpu);
    assert((cpu._eflags & ZF) == 0);
    assert(cpu._ecx == 2); /* stopped after comparing index 1 */

    /* REPE CMPSD with all equal: must finish with ZF=1, ECX=0 */
    memset(&cpu, 0, sizeof(cpu));
    cpu._ecx = 4;
    cpu._esi = (uint32_t)(uintptr_t)a;
    cpu._edi = (uint32_t)(uintptr_t)c;
    x86_repe_cmpsd(&cpu);
    assert((cpu._eflags & ZF) != 0);
    assert(cpu._ecx == 0);

    /* REPNE CMPSD: repeats while !=, stops at index 2 (2 == 2) */
    a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40;
    b[0] = 11; b[1] = 21; b[2] = 30; b[3] = 41;
    memset(&cpu, 0, sizeof(cpu));
    cpu._ecx = 4;
    cpu._esi = (uint32_t)(uintptr_t)a;
    cpu._edi = (uint32_t)(uintptr_t)b;
    x86_repne_cmpsd(&cpu);
    assert((cpu._eflags & ZF) != 0);
    assert(cpu._ecx == 1);

    /* REPE CMPSW with equal word 0 and mismatch at word 1 */
    memset(&cpu, 0, sizeof(cpu));
    cpu._ecx = 4;
    cpu._esi = (uint32_t)(uintptr_t)w1;
    cpu._edi = (uint32_t)(uintptr_t)w2;
    x86_repe_cmpsw(&cpu);
    assert((cpu._eflags & ZF) == 0);
    assert(cpu._ecx == 2);

    /* REPNE CMPSW: stops at word 0 (0 == 0) */
    memset(&cpu, 0, sizeof(cpu));
    cpu._ecx = 4;
    cpu._esi = (uint32_t)(uintptr_t)w1;
    cpu._edi = (uint32_t)(uintptr_t)w2;
    x86_repne_cmpsw(&cpu);
    assert((cpu._eflags & ZF) != 0);
    assert(cpu._ecx == 3);

    puts("x86_cmps_test: PASS");
    return 0;
}
