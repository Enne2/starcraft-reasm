# Report — ottimizzazioni presenter `host_elf.c`

## Esito
Implementati in `runtime/shim-port/elf/host_elf.c` il rifiuto di renderer GL software nel percorso automatico, il dedup dei present ravvicinati, la configurazione opzionale `SC_FPS_CAP` e la conservazione del dirty flag della palette. `SC_PRESENT=gl` resta un override esplicito; `SC_PRESENT=auto` e variabile assente selezionano automaticamente GL quando il driver SDL non è `dummy`.

## Modifiche
- Il renderer GL viene letto con `SDL_GL_GetProcAddress("glGetString")`, insieme al vendor. In auto-select sono rifiutati `llvmpipe`, `softpipe`, `swrast`, `SwiftShader` e `Microsoft Basic Render Driver`; il normale fallback elimina contesto/finestra GL e prosegue con renderer SDL 2D o CPU.
- `present_gl` carica già la texture palette 256×1 solo con `g_pal_dirty`; questa logica è stata conservata. Una copia dell'ultimo canvas e palette consente di saltare present identici ravvicinati (<16 ms). `SC_DEDUP_PRESENT=0` disabilita il dedup; expose, resize, fullscreen, primo frame e refresh ≥250 ms forzano un nuovo present. `SC_GL_VERIFY` disabilita il dedup per non eludere il readback.
- `SC_FPS_CAP=<Hz>` limita i present effettivi senza `SDL_Delay`: pump/input e thread guest non vengono sospesi. Default assente/0: nessun cap.
- Il percorso `SC_CONTROL_FILE` incrementale e il guard di `mouse_poll_global()` non sono stati modificati. Il test `tests/x86_gl_palette_test/check.sh` non è stato modificato.

## Verifiche
- Compilazione C11 con Clang completata per i386 (`--target=i686-unknown-linux-gnu -m32`) e x86-64 nativo; oggetti sotto `build/qa/opt-host/` identificati rispettivamente ELF32 i386 ed ELF64 x86-64.
- Con i flag `-Wall -Wextra -Wno-unused-parameter`, i386 produce 6 warning e x86-64 1; confronto con `HEAD` mostra lo stesso numero e le stesse classi di warning preesistenti (dichiarazioni libc freestanding e `g_uitrace_present` inutilizzata), nessun warning nuovo. `git diff --check` e `bash -n tests/x86_gl_palette_test/check.sh` passano.
- Non eseguito `check.sh`: richiede `build/starcraft-x86`, che il task vieta di ricostruire/toccare; la verifica runtime `307200/307200`, Wayland hardware e Xvfb/llvmpipe resta pertanto non certificata da questa sessione. La verifica precedente in memoria riportava timeout del gate sul binario preesistente, non prova per questi sorgenti.

## Decisioni autonome
Dedup abilitato per default con finestra temporale stretta, forzando periodicamente il refresh e preservando sempre il pump. `SC_FPS_CAP` scarta present in eccesso invece di introdurre sleep o alterare il timing del gioco. Non ho modificato file di build né avviato sotto-agenti.
