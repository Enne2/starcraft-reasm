# Preludio obbligatorio
- **SOLA ANALISI E PROFILING: NON modificare alcun file sorgente o script del progetto (`runtime/`, `tools/`, `vendor/`, `tests/`, `build/starcraft-x86`).**
- Lavora esclusivamente dentro `build/qa/prof-mem/` e scrivi il report finale in `reports/PROF-RAM.md` (e `reports/STATE-prof-mem.md`).
- Usa SOLO `./tools/run-isolated.sh` (display Xvfb privato, audio dummy, nessuna finestra su `:0`, nessun suono).
- **ATTENZIONE 64-bit vs 32-bit:** `Xvfb` è x86_64 mentre `starcraft-x86` è i386 (32-bit)! NON esportare `LD_PRELOAD` nell'ambiente di `tools/run-isolated.sh`; usa un wrapper `build/qa/prof-mem/starcraft-prof-mem.sh` passato tramite `SC_BIN`!
- NON lanciare altri sotto-agenti.

# Missione: Profiling approfondito Memoria RAM (`Statico ELF`, `Boot`, `Menu`, `Gameplay`)

1. **Analisi statica completa di `build/starcraft-x86` (`readelf -SW`, `readelf -lW`, `llvm-nm -S --size-sort`):**
   - Classifica i `17 MiB` su disco e in memoria virtuale/residente statica:
     - `.text` (`8.69 MiB`: quanto è codice tradotto EXE vs codice tradotto Storm vs runtime/shim/llasm, e quanto pesa la tabella `reasm_va_va` + `reasm_va_fn`!),
     - `.rodata` (`0.84 MiB`), `.bss` (`2.92 MiB`: elenca i top 20 simboli `.bss` e `.data` con `llvm-nm -S --size-sort`!),
     - segmenti guest (`guest_pe_bytes` / `.guest_*`), `.symtab`/`.strtab`/`.debug_*`.
2. **Profiler runtime `LD_PRELOAD` (`build/qa/prof-mem/prof_mem.so`) + `smaps` / `pmap`:**
   - Compila `prof_mem.so` (`clang --target=i686-unknown-linux-gnu -m32 -shared -fPIC -O2 -ldl -pthread`) che intercetta `malloc`, `calloc`, `realloc`, `free`, `mmap`, `mmap64`, `munmap` (con guardia ricorsiva `__thread` e buffer statico per bootstrap `dlsym`!) registrando:
     - totale allocazioni, picco live bytes, live bytes correnti per call-site (`__builtin_return_address(0)` + 2–3 frame, risolti con `llvm-nm` + `/proc/self/maps`!).
     - NOTA: `VirtualAlloc` in `win32_shim.c` chiama `mmap` o usa l'arena pre-riservata (`0x20000000`?): misura ANCHE l'uso dell'arena guest `0x0E000000` / `0x02000000` / `0x20000000` leggendo `/proc/<pid>/smaps` e `/proc/<pid>/smaps_rollup`!
   - Avvia in `./tools/run-isolated.sh --id prof-mem` e cattura snapshot completi (`smaps_rollup`, `smaps` raggruppato per mapping/categoria, `pmap -x`, e dump `prof_mem.so`) in tutte e 3 le fasi:
     - **Boot** (subito dopo init/MPQ),
     - **Menu** (Main Menu animato + Campaign Select),
     - **Gameplay** (Boot Camp in-game con unità selezionate/attive).
3. **Analisi & Report (`reports/PROF-RAM.md`, `reports/STATE-prof-mem.md`):**
   - Breakdown preciso di **VmSize**, **RSS**, **PSS**, **Private_Dirty**, **Private_Clean**, **Shared_Clean**, **Anonymous vs File-backed** per ciascuna fase (Boot, Menu, Gameplay).
   - Elenco dettagliato di:
     - Tutte le regioni grandi (`> 256 KiB`) in `smaps` (es. `heap`, `mmap` SDL/Mesa/llvmpipe/DRI/Wayland/X11, `guest arena`, `shadow table 1 MiB`, `VirtualAlloc pool`, `smacker buffers`, `.bss` statica, `reasm_va_*` mappe, `starcraft-x86` file-backed code/data).
     - Top call-site di `malloc` e `mmap` (live bytes e churn alloc/free!).
     - Quantificazione esatta di quanta RAM può essere risparmiata con eventuali ottimizzazioni mirate (es. strip/split debug, layout arena lazy, riduzione `.bss` di `indirect_dispatch`/`win32_shim`/`com_dsound`/`smack`, `VirtualAlloc` pool cap, ecc.).
   - Chiudi `./tools/run-isolated.sh stop --id prof-mem`.
