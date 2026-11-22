# Preludio obbligatorio
- **Proprietà esclusiva:** `runtime/shim-port/elf/win32_shim.c` (SOLO `g_vm_regions`!), `runtime/common/game_main_arm.c` (SOLO `g_sc_prof`!).
- **Vincoli:** 100% C11 portabile, cross-OS, CPU-agnostic, nessuna modifica a logica funzionale né perdita dei fix in `win32_shim.c`.
- NON eseguire `./tools/build.sh` globale; testa solo con unit test in `build/qa/opt-mem2/`. NON lanciare altri sottoagenti.

# Missione: Riduzione `.bss` residua (`g_vm_regions` 448 KiB + `g_sc_prof` 80 KiB)

Leggi `reports/REPORT-opt-mem.md`.
1. In `runtime/shim-port/elf/win32_shim.c`, ispeziona `g_vm_regions` (`458.752 B` in `.bss`):
   - Sostituiscilo con allocazione dinamica / chunk lazy (mantenendo la stessa capacità massima `VM_MAX_REGIONS` e gli stessi invarianti di lock/ricerca, o `realloc`/`calloc` a chunk fissi se i puntatori vengono conservati!).
2. In `runtime/common/game_main_arm.c`, ispeziona `g_sc_prof` (`81.920 B` in `.bss`):
   - Rendilo allocato lazily (`calloc`) solo quando il profiling (`SC_PROF` / env var corrispondente) è abilitato, `0 B` in `.bss` altrimenti!
3. Verifica compilazione C11 (`-Wall -Wextra`) sia `--target=i686-unknown-linux-gnu -m32` sia host 64-bit, e riesegui `x86_vm_pool_test`, `x86_io_readahead_test`, `x86_win32_shim_test` in `build/qa/opt-mem2/`.
4. Scrivi `reports/REPORT-opt-mem2.md` e `reports/STATE-opt-mem2.md`.
