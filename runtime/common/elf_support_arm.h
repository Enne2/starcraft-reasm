/* runtime/arm/elf_support_arm.h — ARM replacements for the x86 loader/TEB surface used by the
 * sc-reasm ELF shims.
 *
 * What the shims expect from the toolkit (see ~/dev/reasm-toolkit/runtime/elf/{reasm_elf.h,teb.h}):
 *
 *   reasm_elf_log / log_hex / log_iat / unimplemented     -> elf_support_arm.c (portable C)
 *   reasm_elf_GetModuleHandleA/LoadLibraryA/GetProcAddress -> elf_support_arm.c (bridge aware)
 *   loader_init / teb_init / teb_set_stack / teb_envA     -> elf_support_arm.c
 *
 * The i386 originals reached the TEB through `set_thread_area` + `%fs` and the images through a
 * linker script placing them at their PE VAs. On ARM:
 *   - the TEB is the emulated page from x86_helpers.c (no segment register exists), and
 *   - the images are mapped with MAP_FIXED at their original VAs by game_memory_arm.c.
 * `reasm_call_on_stack` is *not* needed: in the LLASM model the guest stack is `cpu->esp`
 * (a VA inside the low arena), not the host stack, so a translated call never touches it.
 */
#ifndef ARM_ELF_SUPPORT_ARM_H
#define ARM_ELF_SUPPORT_ARM_H

#include <stdint.h>

/* log/stop, same contract as the toolkit (stderr, no printf). */
void reasm_elf_log(const char *s);
void reasm_elf_log_hex(unsigned int v);
void reasm_elf_log_iat(int done, int missing, int total);
void reasm_elf_unimplemented(const char *name, unsigned int caller);   /* logs, _exit(3) */

/* Module/proc by name. There is exactly one image pair (exe at 0x00400000, storm at
 * 0x15000000); GetProcAddress resolves Win32 imports through the bridge stubs and storm
 * ordinals ("#N" or "ordinal_<hexdll>_<n>") through the generated ordinal table. */
void *reasm_elf_GetModuleHandleA(const char *n);
void *reasm_elf_LoadLibraryA(const char *n);
void *reasm_elf_GetProcAddress(void *m, const char *n);

/* Toolkit teb.h API, re-implemented on the emulated TEB. */
void loader_init(void);
void teb_init(void);
void teb_set_stack(unsigned int stack_base, unsigned int stack_limit);
char *teb_envA(void);

/* Address of the guest-ABI thunk for an import name (0 if none). GetProcAddress must hand out
 * *this*, never a C shim symbol: the guest calls with its own stack-based convention. */
void *arm_bridge_lookup_thunk(const char *n);

/* Name -> stub address table, generated from bridge_table.def (bridge_symbols.c). */
typedef struct { const char *name; void *fn; } ArmBridgeSymbol;
extern const ArmBridgeSymbol g_bridge_symbols[];
extern const uint32_t g_bridge_symbols_n;

#endif /* ARM_ELF_SUPPORT_ARM_H */
