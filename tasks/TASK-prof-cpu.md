# Preludio obbligatorio
- **SOLA ANALISI E PROFILING: NON modificare alcun file sorgente o script del progetto (`runtime/`, `tools/`, `vendor/`, `tests/`, `build/starcraft-x86`).**
- Lavora esclusivamente dentro `build/qa/prof-cpu/` e scrivi il report finale in `reports/PROF-CPU.md` (e `reports/STATE-prof-cpu.md`).
- Usa SOLO `./tools/run-isolated.sh` (display Xvfb privato, audio dummy, nessuna finestra su `:0`, nessun suono).
- **ATTENZIONE 64-bit vs 32-bit:** `Xvfb` è x86_64 mentre `starcraft-x86` è i386 (32-bit)! NON esportare `LD_PRELOAD` nell'ambiente di `tools/run-isolated.sh` (altrimenti `Xvfb` carica/fallisce il preload 32-bit). Invece crea un wrapper `build/qa/prof-cpu/starcraft-prof-cpu.sh` (`chmod +x`) che fa `exec env LD_PRELOAD=... /home/enne2/dev/starcraft-reasm-generic/build/starcraft-x86 "$@"` e passalo tramite `SC_BIN="$PWD/build/qa/prof-cpu/starcraft-prof-cpu.sh"`!
- NON lanciare altri sotto-agenti.

# Missione: Profiling approfondito CPU (`Boot`, `Menu`, `Gameplay`)

1. **Costruisci un profiler CPU a campionamento 32-bit (`build/qa/prof-cpu/prof_cpu.so`):**
   - Compila con `clang --target=i686-unknown-linux-gnu -m32 -shared -fPIC -O2 -pthread` (e `-ldl`).
   - Usa `timer_create(CLOCK_THREAD_CPUTIME_ID, ...)` con `SIGEV_THREAD_ID` (su ogni thread che parte intercettando `pthread_create` + sul thread principale in `__attribute__((constructor))`) OPPURE `ITIMER_PROF` (`1000 Hz`, handler `SA_SIGINFO` `SA_RESTART`) raccogliendo `(eip, esp, tid, phase)` in un ring/buffer lock-free o per-thread, scaricando i campioni su file su `atexit`/`SIGTERM`/periodicamente!
   - Nota: con `timer_create(CLOCK_THREAD_CPUTIME_ID)` + `SIGEV_THREAD_ID` (`syscall(SYS_gettid)`) misuri il **vero tempo CPU per thread** (main thread + worker Smacker + thread guest + SDL/Xvfb)! Se `SIGEV_THREAD_ID` ha restrizioni, `ITIMER_PROF` a 1000 Hz + campionamento `/proc/<pid>/task/*/stat` (`utime`/`stime` a 100 Hz) funziona sempre! Verifica che il profiler produca migliaia di campioni validi e non faccia crashare il gioco.
   - Simbolizza ogni `EIP` campionato usando:
     - `llvm-nm -nS build/starcraft-x86` (per tutte le funzioni host e `loc_<VA>` tradotte di `StarCraft` e `Storm`!)
     - `/proc/<pid>/maps` salvato dal processo per attribuire gli indirizzi nelle librerie dinamiche (`libsmacker-1.2.0.so`, `libSDL2`, `libGL`/`llvmpipe`/`libX11`, `libc`, `libpthread`).
2. **Esegui e profila le 3 fasi in isolamento (`./tools/run-isolated.sh`):**
   - Usa un segnale o file di marcatura fase (`SC_PROF_PHASE_FILE`) oppure 3 run/finestre distinte:
     - **Fase 1 — Boot** (`0s..2s` dall'avvio fino al Main Menu).
     - **Fase 2 — Menu animato** (`5s` sul Main Menu con `--skip-intro 1 --smack 1`, più un breve check di `hover` e `Campaign Select`).
     - **Fase 3 — Gameplay Boot Camp** (`10s` in partita Boot Camp con box-select + SCV in movimento/raccolta, usando le coordinate verificate: `220,120` → `260,226` → `100,109` → `515,393` → `320,250` → `520,400` → `538,390` → `443,263`).
   - Misura anche:
     - CPU% totale e **CPU% per thread** (`/proc/<pid>/task/*/stat` + `comm`),
     - FPS osservati (`SC_FPS_LOG=1`),
     - Confronto rapido opzionale `SC_PRESENT=cpu` vs `gpu` vs `gl` (in Xvfb) e `SC_SMACK_ASYNC=1` vs `0` se utile per capire il costo del presenter/driver rispetto al codice del gioco!
3. **Analisi & Report (`reports/PROF-CPU.md`, `reports/STATE-prof-cpu.md`):**
   - Tabella per ciascuna delle 3 fasi (**Boot**, **Menu**, **Gameplay**):
     - CPU% totale e breakdown per thread,
     - Top 25 funzioni / moduli per % CPU (separando: codice guest tradotto `loc_*` EXE vs Storm, runtime dispatch/memory/SCode/SComp, shim `win32`/`ddraw`/`smack`/`host_elf`, librerie esterne SDL2/GL/llvmpipe/X11/libsmacker/libc),
     - Eventuali funzioni guest tradotte calde (`loc_*`) ispezionate brevemente (che cosa fa quella funzione in `StarCraft.orig.exe` / `storm.orig.dll` — es. spin-loop, polling, blit, pathfinding, fog-of-war, memcpy, ecc.!),
     - Opportunità concrete di ottimizzazione ordinate per impatto atteso (con stima basata sui numeri misurati).
   - Chiudi tutte le istanze isolate (`./tools/run-isolated.sh stop`).
