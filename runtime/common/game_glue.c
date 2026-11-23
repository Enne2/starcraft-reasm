/* runtime/arm/game_glue.c — link-time compatibility for the legacy ELF shim objects.
 *
 * The active ARM game path uses the static LLASM bridge table and cpu_llasm_run_raw. Some
 * already-built shim objects still contain references from the older native-ELF loader; keep
 * those symbols linkable without pretending that its dynamic IAT or WinMain trap is active.
 */
#include "cpu_llasm.h"
#include "bridge.h"
#include "elf_support.h"

#include <stdint.h>

/* main_elf.c's import-record ABI. The ARM runtime resolves imports statically through g_bridge,
 * so this compatibility table has zero logical entries (one physical sentinel for C arrays). */
typedef struct {
    const char *dll;
    const char *name;
    unsigned int slot;
} ReasmImport;

const ReasmImport g_imports[1] = { { 0, 0, 0 } };
const int g_imports_n = 0;

/* main_elf.c calls this before doing anything else. Build identity checking is not wired into
 * this ARM link yet; the explicitly conservative compatibility behavior is success. */
int build_guard_check(int check_only) {
    (void) check_only;
    reasm_elf_log("[arm-game-glue] build_guard_check: controllo non configurato, accettato\n");
    return 0;
}

/* Legacy i386 loader helper. The ARM guest stack lives at cpu->_esp, not on the host stack
 * (elf_support.h); consequently stack_top is intentionally ignored. This signature receives
 * a host C callback, unlike guest callbacks, which use arm_guest_call4/arm_guest_calln in
 * guest_call.c. */
void reasm_call_on_stack(void *stack_top, void (*fn)(void)) {
    (void) stack_top;
    if (!fn) {
        reasm_elf_log("[arm-game-glue] reasm_call_on_stack: callback NULL\n");
        return;
    }
    fn();
}

/* Legacy dynamic-IAT hook. With g_imports_n == 0, the static bridge table already supplies all
 * guest import entry points; do not inspect or write any emulated address. The legacy caller
 * declares this as void and ignores the result; int preserves the requested 0-success contract. */
int reasm_iat_install(void) {
    if (g_imports_n == 0) return 0;
    reasm_elf_log("[arm-game-glue] reasm_iat_install: tabella non vuota inattesa\n");
    return 0;
}

/* Adapter from the old ELF loader name to the generated ARM ordinal map. Unknown ordinals are
 * reported once, then remain unresolved (zero); no VA is invented. */
unsigned int reasm_storm_ordinal_va(int ord) {
    static int warned_missing;
    uint32_t va = ord > 0 ? arm_bridge_ordinal_va((unsigned) ord) : 0;
    if (!va && !warned_missing) {
        warned_missing = 1;
        reasm_elf_log("[arm-game-glue] ordinal storm non risolto (ulteriori avvisi soppressi)\n");
    }
    return (unsigned int) va;
}

/* The old x86 loader patches native WinMain and transfers control here. The ARM runtime instead
 * enters the translated CRT directly from game_main.c; do not guess a second entry VA here. */
void winmain_trap(void) {
    reasm_elf_log("[arm-game-glue] winmain_trap legacy invocato; nessun entry ARM selezionato\n");
}
