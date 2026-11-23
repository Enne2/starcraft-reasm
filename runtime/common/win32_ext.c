/* runtime/arm/win32_ext.c — the few Win32 APIs the game imports that the sc-reasm shims do
 * not implement, and that can be implemented *faithfully* on the ARM runtime.
 *
 * Only two groups qualify; everything else stays a declared UNK trampoline (see
 * runtime/arm/bridge_unk.def) so that it stops with a name instead of returning a plausible lie.
 *
 * 1) `AdjustWindowRectEx` — pure geometry. The port's windows are created by host_elf.c as a
 *    client-sized SDL window: no caption, no border, no system menu is drawn or managed by us, so
 *    the documented adjustment for this window model is the identity. StarCraft uses it to go
 *    from a client size to an outer size; with no decorations the two coincide. Returning the
 *    rect unchanged is therefore the faithful answer, not a stub that skips work.
 *
 * 2) The IMM32 (input method) entry points — the game probes for an IME context. On Windows
 *    without an active IME these return exactly the values below (documented as "no context"):
 *    ImmGetContext -> NULL, ImmAssociateContext -> NULL, ImmReleaseContext -> TRUE,
 *    ImmGetCompositionStringA -> 0, ImmSetCompositionStringA -> FALSE. There is no IME in this
 *    host, so this is the real behaviour of the API here, and each of them is logged once so the
 *    choice is visible in the trace instead of being silent.
 */
#include "guest_cpu_helpers.h"
#include "game_memory.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void note_once(const char *name) {
    static char seen[16][24];
    static int n;
    int i;
    for (i = 0; i < n; ++i) if (strcmp(seen[i], name) == 0) return;
    if (n < 16) {
        int len = (int) strlen(name);
        if (len > 23) len = 23;
        memcpy(seen[n], name, (size_t) len);
        seen[n][len] = 0;
        n++;
    }
    (void) write(2, "[arm-win32] prima chiamata a ", 28);
    (void) write(2, name, strlen(name));
    (void) write(2, " -> valore documentato \"nessun contesto\"\n", 40);
}

/* BOOL AdjustWindowRectEx(RECT *rc, DWORD style, BOOL menu, DWORD exstyle) */
int AdjustWindowRectEx(int *rc, uint32_t style, int menu, uint32_t exstyle) {
    (void) rc; (void) style; (void) menu; (void) exstyle;
    note_once("AdjustWindowRectEx");
    return 1;             /* TRUE; the rect is already the outer size (no decorations) */
}

/* ---- IMM32: no input method on this host ---- */
void *ImmGetContext(void *hwnd) { (void) hwnd; note_once("ImmGetContext"); return 0; }
void *ImmAssociateContext(void *hwnd, void *himc) {
    (void) hwnd; (void) himc; note_once("ImmAssociateContext"); return 0;
}
int ImmReleaseContext(void *hwnd, void *himc) {
    (void) hwnd; (void) himc; note_once("ImmReleaseContext"); return 1;
}
int ImmGetCompositionStringA(void *himc, uint32_t index, void *buf, uint32_t len) {
    (void) himc; (void) index; (void) buf; (void) len;
    note_once("ImmGetCompositionStringA");
    return 0;
}
int ImmSetCompositionStringA(void *himc, uint32_t index, const void *comp, uint32_t comp_len,
                             const void *read, uint32_t read_len) {
    (void) himc; (void) index; (void) comp; (void) comp_len; (void) read; (void) read_len;
    note_once("ImmSetCompositionStringA");
    return 0;             /* FALSE: nothing to set without an IME */
}
