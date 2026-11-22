/* ARM-only guest ABI adapters for native COM / dynamically resolved exports. */
#include "cpu_llasm.h"
#include "indirect_dispatch.h"
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

#define COM_THUNK_CAP 256u
typedef struct { void *fn; unsigned char nargs, pops; } ComThunk;
static ComThunk g_com[COM_THUNK_CAP];
static unsigned g_com_n;
extern void *arm_com_thunk_table[COM_THUNK_CAP];
extern unsigned (*arm_shim_caller(unsigned))(void *, _cpu *);

void *arm_com_thunk(void *fn, unsigned nargs, unsigned pops) {
    unsigned i;
    if (!fn || nargs > 14 || pops > nargs) return 0;
    for (i = 0; i < g_com_n; ++i)
        if (g_com[i].fn == fn && g_com[i].nargs == nargs && g_com[i].pops == pops)
            return arm_com_thunk_table[i];
    if (g_com_n == COM_THUNK_CAP) { (void)write(2, "[com-bridge] thunk table full\n", 30); _exit(3); }
    i = g_com_n++;
    g_com[i].fn = fn; g_com[i].nargs = (unsigned char)nargs; g_com[i].pops = (unsigned char)pops;
    return arm_com_thunk_table[i];
}

void arm_com_wrap_vtable(void **vt, const unsigned char *nargs, unsigned count) {
    unsigned i;
    for (i = 0; i < count; ++i)
        if (vt[i]) vt[i] = arm_com_thunk(vt[i], nargs[i], nargs[i]);
}

uint32_t arm_com_bridge_common_c(_cpu *cpu, unsigned index) {
    uint32_t ret;
    unsigned i;
    unsigned (*call)(void *, _cpu *);
    if (index >= g_com_n) { (void)write(2, "[com-bridge] invalid thunk index\n", 33); _exit(3); }
    ret = cpu_llasm_pop32(cpu); /* guest return address */
    call = arm_shim_caller(g_com[index].nargs);
    if (!call) { (void)write(2, "[com-bridge] unsupported arity\n", 31); _exit(3); }
    cpu->_eax = call(g_com[index].fn, cpu);
    for (i = 0; i < g_com[index].pops; ++i) (void)cpu_llasm_pop32(cpu);
    return ret;
}
