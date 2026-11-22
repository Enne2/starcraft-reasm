# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` e `rg` mirato su `build/lift-full/*.llasm` per verificare `0x00404F09` (`doexit`), `0x006DD684` (`_onexitbegin`/`_onexitend`), DirectSound exports e `host_elf`!
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai aggiustare `reasm_indirect_resolve` per accettare target invalidi; trova la vera causa radice!
4. **Regola di isolamento**: MAI finestre visibili sullo schermo dell'utente né audio reale per i test automatici! Usa `./tools/run-isolated.sh` con `SC_BIN` privato (es. `build/qa/dsound/starcraft-x86-dsound`) per provare il game exit o DirectSound! (Per verificare il solo bug `X11_ShowWindow`, nota che l'hang X11 avveniva su XWayland `:0` — NON lanciare finestre su `:0`; studia invece il codice `host_elf.c` e `libSDL2`/`libSDL3` via `nm -D` / `objdump` / `sdl2-compat` behavior!)
5. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (`win32_shim.c`, `snp_elf.c`, `storm_sdraw_arm.c`, `com_ddraw.c`, `build/starcraft-x86`, `build/obj-x86/`).
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-dsound`: DirectSound (`com_dsound.c`), Callback on-exit `0x1805F2D4` (`crt_init_arm.c`) & Analisi X11 `sdl2-compat` (`host_elf.c`)

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/com_dsound.c`
- `runtime/shim-port/elf/host_elf.c` (sei l'UNICO agente a modificare `host_elf.c`!)
- `runtime/common/crt_init_arm.c`
- `tests/x86_dsound_test.c` (nuovo)
- `build/qa/dsound/` (oggetti compilati + binario privato `starcraft-x86-dsound` per test isolati)
- `reports/REPORT-dsound.md`
- `reports/STATE-dsound.md` (max 70 righe)

## Obiettivi
1. **DirectSound completeness (`com_dsound.c`)**:
   - Ispeziona in `StarCraft.orig.exe` / `storm.orig.dll` eventuali chiamate a `dsound.dll` (`DirectSoundEnumerateA`, `DirectSoundCapture*`, metodi `IDirectSoundBuffer` come `SetVolume`, `SetPan`, `SetFrequency`, `GetVolume`, `GetPan`).
   - Migliora il mixer in `com_dsound.c`: espandi/alloca dinamicamente le voci del mixer (evitando `DSERR_ALLOCATED` precoce in battaglie affollate), implementa `DirectSoundEnumerateA` in `win32_dsound_get_proc` se usata/documentata, e verifica `SetVolume`/`SetPan`/`SetFrequency` reali sul mixer.
   - Scrivi ed esegui `tests/x86_dsound_test.c` (compilato `-m32`) per verificare creazione, duplicazione, voci multiple (>16) e controlli volume/pan/freq!
2. **Root-cause e fix del callback on-exit `0x1805F2D4` (`0x00404F09` → `0x006DD684`)**:
   - Ricorda dal report `REPORT-x86-final.md`: quando StarCraft esce (click su `Exit` nel menu principale `(220, 405)` circa, oppure exit path), `0x00404F09` drena la tabella `onexit` in `0x006DD684` (inizializzata a un buffer allocato `0x20180c4c`), legge `0x1805F2D4` e va in STOP fail-closed!
   - Usa un binario privato `build/qa/dsound/starcraft-x86-dsound` sotto GDB isolato con `watch` sul puntatore scritto dentro la tabella `onexit` (o breakpoint su `atexit` / `_onexit` / `0x00404F09` / `0x00404Exx` in `StarCraft.orig.exe`!) per scoprire **esattamente** chi scrive `0x1805F2D4` e perché è un indirizzo host (es. un thunk o un argomento CRT / `crt_init_arm.c` / `guest_pe_bytes` vs `StarCraft.entry.o`)!
   - Se la causa è in `crt_init_arm.c` (o in un gestore gestibile senza violare D19), fissala pulitamente e verifica che cliccando **Exit** nel menu principale (`--id dsound-exit` con `tools/run-isolated.sh`), StarCraft esca con `exit 0` pulito (zero `STOP 0x1805F2D4`)!
3. **Analisi e mitigazione `SDL_VIDEODRIVER=x11` su `sdl2-compat` (`host_elf.c`)**:
   - Senza aprire finestre sul desktop reale dell'utente, ispeziona `host_elf.c` e i simboli/comportamento di `libSDL2-2.0.so.0` (`sdl2-compat` i686 su Fedora 44): verifica perché `SDL_CreateWindow` su XWayland (`X11_ShowWindow` → `XIfEvent`) resta in attesa (es. `SDL_WINDOW_HIDDEN` + `SDL_ShowWindow` dopo `SDL_PumpEvents`, o hint `SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR`, o `SDL_SetHint` per `sdl2-compat` / `SDL_VIDEO_DRIVER`). Documenta in `REPORT-dsound.md` se esiste una sequenza sicura per `host_elf.c` o i limiti reali di `sdl2-compat`.
4. **Report, STATE e `qmem_store`**:
   - Salva risultati/fix in `qmem_store` (`project_id="starcraft-reasm-generic"`), chiudi qualsiasi istanza isolata (`stop`), e scrivi `reports/REPORT-dsound.md` e `reports/STATE-dsound.md`.
