# Preludio obbligatorio
- Oracolo primario: codice esistente in `runtime/shim-port/elf/host_elf.c`, SDL2 API standard, `reports/PROF-CPU.md`, `reports/PROF-RAM.md`.
- **Proprietà esclusiva (NON toccare altri file di progetto):**
  - `runtime/shim-port/elf/host_elf.c`
  - `tests/x86_gl_palette_test/`
- **Vincoli fondamentali:**
  1. **Resa e comportamento identici al 100%:** stessa palette, stessi pixel esatti (`x86_gl_palette_test` `307200/307200` PASS), **preservando 100% `SC_CONTROL_FILE` e soppressione `mouse_poll_global()`**, oltre a tutti i `SC_PRESENT` esistenti (`gl`, `gpu`, `cpu`).
  2. **100% cross-platform, cross-OS, CPU-agnostic:** solo API C11 / SDL2 / OpenGL ES2 / Desktop GL standard (nessuna assunzione x86-only né Linux-only!).
  3. **NON eseguire `./tools/build.sh` globale** né modificare `build/starcraft-x86` (lavora e testa in `build/qa/opt-host/`).
  4. NON lanciare altri sotto-agenti.

# Missione: Ottimizzazioni presenter `host_elf.c` (Avoid software `llvmpipe` + Dirty-Frame Present Dedup + `SC_FPS_CAP`)

Leggi `runtime/shim-port/elf/host_elf.c`, `reports/PROF-CPU.md`, `reports/PROF-RAM.md`.

1. **Evitare il caricamento/uso di software GL (`llvmpipe`/`softpipe`/`swrast`) in auto-select (`−40–86 MiB` RSS, `−188%` CPU in software/Xvfb):**
   - Attualmente in `host_elf.c`, quando `SC_PRESENT` non è forzato, `present_gl` inizializza un contesto OpenGL anche se il driver sottostante è software (`llvmpipe`, `softpipe`, `swrast`, `SwiftShader`), caricando `libLLVM.so` (`~40–86 MiB` RSS) e lanciando 16 thread worker software (`188%` CPU)!
   - In `gl_present_init()` (solo quando `SC_PRESENT` NON è forzato esplicitamente a `"gl"`):
     - Leggi `glGetString(GL_RENDERER)` e `glGetString(GL_VENDOR)` (via `SDL_GL_GetProcAddress("glGetString")`).
     - Se il renderer è software (`"llvmpipe"`, `"softpipe"`, `"swrast"`, `"Microsoft Basic Render Driver"`), rifiuta `gl` e fai fallback pulito su `gpu` (`SDL_CreateRenderer` 2D, o `cpu`), liberando subito il contesto GL e la finestra GL così da creare la finestra SDL 2D pulita!
     - Se invece `SC_PRESENT=gl` è richiesto esplicitamente (o l'host ha GPU hardware vera come `AMD Radeon 780M` su Wayland), usa il presenter GLES2 palette shader!
2. **Present dedup / dirty-frame skip & `SC_FPS_CAP` opzionale (100% bit-identical):**
   - Quando StarCraft è in `Menu` o `Gameplay` senza frame-cap, chiama `present()`/`Unlock`/`Blt` fino a `380–610` volte al secondo anche su frame identici o a ritmo molto superiore al refresh.
   - Verifica `present_gl`, `present_gpu`, `present_scaled`:
     - Mantieni un contatore/dirty check pulito:
       - `host_elf_set_palette()` marca `g_pal_dirty = 1` (e puoi fare upload della texture palette `256×1` SOLO quando `g_pal_dirty != 0`, invece di ri-caricare la palette texture ad ogni singolo frame!).
       - Inoltre per il canvas `640×480` (se `SC_DEDUP_PRESENT` è attivo o di default in `present()` quando `g_pal_dirty == 0` e un hash veloce FNV-1a/memcmp del canvas coincide con l'ultimo presentato? Attenzione: verifica che `SDL_RenderPresent` / `SDL_GL_SwapWindow` dopo un expose/resize non venga saltato — es. ri-presenta sempre almeno su expose/resize/primo frame/dopo 250ms, oppure solo su `present` ravvicinati `< 16ms` se canvas+palette sono identici!).
     - Aggiungi anche `SC_FPS_CAP` (intero in Hz, es. `SC_FPS_CAP=60` -> throttle morbido con `SDL_GetTicks` / `SDL_Delay` tra un `Present` effettivo e il successivo, default `0` = nessuno sleep forzato che alteri il comportamento di timing del gioco se non richiesto, oppure verificare se `host_elf` può fare present pacing senza rallentare `input`/`pump`!). **Importante:** NON rallentare `host_elf_pump()` né il thread guest; il pacing al massimo limita i `Present` duplicati o applica `SC_FPS_CAP` configurabile!
3. **Test & Verifica:**
   - Compila `host_elf.c` in `build/qa/opt-host/` sia con `clang -m32` sia con `clang` 64-bit nativo (syntax/portability check con `SDL2`), verificando zero warning nuovi.
   - Verifica che `tests/x86_gl_palette_test/check.sh` funzioni concettualmente e che `SC_PRESENT=gl` (su hardware/test) vs `auto` su `llvmpipe` (Xvfb) scelga il percorso corretto senza regressioni su `SC_CONTROL_FILE`!
4. **Consegna:**
   - Scrivi `reports/REPORT-opt-host.md` e `reports/STATE-opt-host.md` (max 70 righe).
