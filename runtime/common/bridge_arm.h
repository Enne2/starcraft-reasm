/* runtime/arm/bridge_arm.h — import bridge: from SRW/LLASM import symbols to ARM code.
 *
 * Naming follows the emitter exactly (`SRW/SR_output.c:SR_write_output_import`): for LLASM
 * output every used import becomes `proc <ProcName> external`, where <ProcName> is
 *   - the export name for a named import (`GetTickCount`, `CreateWindowExA`, ...), or
 *   - `ordinal_<hex of the lower-case DLL name>_<ordinal>` for an ordinal import
 *     (`MakeOrdinalSymbol`, SRW/SRW_loader.c:382). Example: storm.dll #101 ->
 *     `ordinal_73746f726d2e646c6c_101`.
 * In the emitted IR that is `declare hidden fastcc void @<ProcName>(%_cpu*)`, and the *address*
 * of that symbol is what the image's IAT-style data slot holds (verified: StarCraft.o has an
 * `R_ARM_ABS32` relocation to every import). So each import must be a real function with the
 * guest calling convention: on entry r0 = %_cpu, and it must not return on the host stack — it
 * pops the guest return EIP and its stdcall arguments and *branches* to the continuation.
 *
 * Three kinds of entry live in one table (`gen_bridge_stubs.py` generates both the stubs and the
 * table from bridge_table.def + the generated bridge_shims.def):
 *   HAND  hand-written C implementation with the guest ABI (cpu_llasm_arg32 + cpu->_eax).
 *   SHIM  a C function from sc-reasm/tools/elf, reached through a per-arity caller because the
 *         C prototype (AAPCS) and the guest stack (stdcall) are unrelated calling conventions.
 *   UNK   no implementation: a declared trampoline that logs and stops (rule D19).
 *
 * storm.dll is *not* bridged: it is translated in-process, so its ordinals are guest VAs
 * (`arm_bridge_ordinal_va`).
 */
#ifndef ARM_BRIDGE_ARM_H
#define ARM_BRIDGE_ARM_H

#include <stdint.h>
#include "cpu_llasm.h"

enum { ARM_BRIDGE_HAND = 0, ARM_BRIDGE_SHIM = 1, ARM_BRIDGE_UNK = 2 };

typedef struct {
    const char *name;
    uint8_t kind;
    uint8_t nargs;
    uint8_t pops;                                  /* stdcall words the callee removes */
    uint8_t var;                                   /* SHIM: variadic (needs the window caller) */
    void *fn;                                      /* HAND: guest-ABI impl; SHIM: C function */
    unsigned (*call)(void *fn, _cpu *cpu);         /* SHIM: cached per-arity marshaller, 0 = resolve */
} ArmBridgeEntry;

extern const ArmBridgeEntry g_bridge[];
extern const unsigned g_bridge_count;

/* Called by arm_bridge_common (bridge_stubs.S) with the index baked into the stub.
 * Pops the guest return EIP, runs the entry, pops the stdcall arguments, and returns the guest
 * VA to branch to (the stub does the branch, so the host stack returns to the entry level). */
uint32_t arm_bridge_common_c(_cpu *cpu, uint32_t index);

/* Name -> entry index, -1 when absent. Used by GetProcAddress and by the diagnostics. */
int arm_bridge_find(const char *name);
unsigned arm_bridge_nargs(unsigned index);
const char *arm_bridge_name(unsigned index);
unsigned arm_bridge_kind(unsigned index);

/* storm.dll ordinal -> guest VA, from the generated table (0 when unknown). The table is
 * produced from sc-reasm's analysis/storm_ordinals.json; an absent table resolves nothing and
 * never invents an address. */
uint32_t arm_bridge_ordinal_va(unsigned ordinal);

/* Diagnostics: dump the table (name, kind, nargs) on stderr. */
void arm_bridge_dump(void);

/* Number of UNK (declared but unimplemented) entries: reported at startup, because each of them
 * is a place where the game will stop with a name instead of misbehaving silently. */
unsigned arm_bridge_unk_count(void);

#endif /* ARM_BRIDGE_ARM_H */
