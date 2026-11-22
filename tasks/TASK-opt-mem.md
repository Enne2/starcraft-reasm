# Preludio obbligatorio
- **Proprietà esclusiva (NON toccare altri file di progetto):**
  - `runtime/shim-port/elf/snp_elf.c`
  - `runtime/shim-port/elf/win32_ui.c`
  - `runtime/common/game_memory_arm.c`
  - `runtime/common/indirect_dispatch.c`
  - `tools/link-x86.sh`, `tools/link-arm.sh`
- **Vincoli fondamentali:**
  1. **Comportamento e capacità identici al 100%:** nessuna regressione su limiti, slot o gestione errori.
  2. **100% C11 portabile, cross-OS, CPU-agnostic:** niente estensioni x86-only o Linux-only non protette da guardie standard; tutti i file devono compilare pulitamente per i686, ARMv6/v7, AArch64 e x86_64!
  3. **NON eseguire `./tools/build.sh` globale** né modificare `build/starcraft-x86` (usa test unitari in `build/qa/opt-mem/`).
  4. NON lanciare altri sotto-agenti.

# Missione: Riduzione Churn `snp_elf`, Ottimizzazione `.bss` statica & Separate Debug Info

Leggi `reports/PROF-RAM.md` e ispeziona i 6 file di tua proprietà.

1. **`runtime/shim-port/elf/snp_elf.c` (`−63,1 MiB` churn malloc/free in gameplay):**
   - In `receive_packet` (o ovunque si faccia `malloc` prima di `recvfrom`): usa un buffer sullo stack (es. `2048` byte, o verifica `MSG_PEEK` / dimensione max UDP StarCraft) prima di chiamare `malloc`, allocando sullo heap (`malloc`) **SOLO** quando `recvfrom` ha effettivamente ricevuto un pacchetto (`n > 0`)!
   - Verifica che `./build/qa/net/x86_snp_lan_test` (ricompilando `snp_elf.c` in `build/qa/opt-mem/`) continui a passare 100% e misuri `0` allocazioni a vuoto!
2. **Riduzione `.bss` statica senza cambiare capacità (`game_memory_arm.c`, `win32_ui.c`, `indirect_dispatch.c`):**
   - `runtime/common/indirect_dispatch.c`: `g_sc_prof` (`80 KiB` `.bss`, `81.920 B`) -> allocala **lazily** (`calloc`) solo quando il profiling SCode/dispatch è effettivamente abilitato (`REASM_PROF` / `SC_PROF`), `0 B` `.bss` altrimenti!
   - `runtime/common/game_memory_arm.c`: `g_vm_regions` (`448 KiB` `.bss`, `16384` slot) -> verifica quanti slot vengono effettivamente usati (tipicamente decine/centinaia) e sostituisci con allocazione dinamica crescente (`realloc` da capacità iniziale `128` fino al cap esistente o illimitato con crescita `2x`) protetta dallo stesso mutex!
   - `runtime/shim-port/elf/win32_ui.c`: `g_win` (`449 KiB` `.bss`) -> sostituisci l'array statico enorme con allocazione dinamica / crescita lazy (es. `64` iniziali -> `2x`, preservando gli indici/HWND esatti — attenzione: verifica se `HWND` è un indice o un puntatore a `g_win[i]`! Se è un puntatore a slot `g_win[i]`, **non fare `realloc` che invalida i puntatori**, ma alloca a chunk fissi da `64` slot ciascuno così gli indirizzi dei nodi restano stabili al 100%!).
3. **Separate debug info opzionale in `tools/link-x86.sh` e `tools/link-arm.sh` (`−3,47 MiB` su disco: `.symtab` + `.strtab` + `.debug_*`):**
   - Controlla `tools/link-x86.sh` e `tools/link-arm.sh`:
     - **ATTENZIONE:** `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` usa `llvm-nm` o ELF `.symtab`? **VERIFICA PRIMA `tools/dispatch-ir.py`!!** Se `--dump-map` legge `.symtab` (`reasm_va_map_n`, `reasm_va_va`, `reasm_va_fn`), NON strippare `.symtab` dal binario principale se rompe `--dump-map`! Invece:
       - Verifica cosa legge `dispatch-ir.py --dump-map` (`readelf` / `llvm-nm` / `.symtab`)!
       - Se `SC_SPLIT_DEBUG=1` (o di default se `--dump-map` funziona ancora oppure salvando `starcraft-x86.sym` / `starcraft-x86.debug` con `llvm-objcopy --only-keep-debug` + `--strip-debug` mantenendo `--add-gnu-debuglink`), verifica esplicitamente su una copia di `build/starcraft-x86` che `--strip-debug` rimuova `.debug_*` (`~0.7 MiB`) mantenendo `dispatch-ir.py --dump-map` 100% funzionante (e documenta lo split completo `--strip-all` come opzionale `SC_STRIP_ALL=1`)!
4. **Verifica compilazione portabile:**
   - Compila ogni file `.c` modificato in `build/qa/opt-mem/` sia con `clang --target=i686-unknown-linux-gnu -m32` sia con `clang` 64-bit nativo (`-Wall -Wextra`) + verifica `tests/x86_shadow_mem_test` / `snp_elf` UDP test in `build/qa/opt-mem/`!
   - Verifica `bash -n tools/link-x86.sh tools/link-arm.sh`.
5. **Consegna:**
   - Scrivi `reports/REPORT-opt-mem.md` e `reports/STATE-opt-mem.md` (max 70 righe).
