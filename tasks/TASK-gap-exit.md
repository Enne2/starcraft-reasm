# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` (`0x00404F09` `doexit`, `0x00406041` `__onexit_lk`) e una copia privata dell'i64 se usi IDA; controlla `ida_list` prima e non toccare sessioni altrui.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai accettare target invalidi in `reasm_indirect_resolve`! Trova e correggi il vero writer di `0x1805F2D4`.
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `SC_BIN="$PWD/build/qa/gap-exit/starcraft-x86-exit" ./tools/run-isolated.sh` e chiudi sempre con `stop`.
5. **Divieti**: NON toccare file fuori proprietà (`win32_shim.c`, `snp_elf.c`, `com_ddraw.c`, `build/starcraft-x86`, `build/obj-x86/`). Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gap-exit`: Fix callback on-exit `0x1805F2D4`, unit test DirectSound & diagnosi `X11_ShowWindow`

## File di proprietà (esclusivi)
- `runtime/common/crt_init_arm.c`
- `runtime/shim-port/elf/com_dsound.c`
- `runtime/shim-port/elf/host_elf.c`
- `tests/x86_dsound_test.c` (nuovo)
- `build/qa/gap-exit/` (oggetti compilati, binario privato `starcraft-x86-exit`, test binary, log GDB)
- `reports/REPORT-gap-exit.md`
- `reports/STATE-gap-exit.md` (max 70 righe)

## Obiettivi
1. **Trova e risolvi il callback on-exit `0x1805F2D4`**:
   - Copia `build/starcraft-x86` (o ricompila i tuoi `.c` e linka) in `build/qa/gap-exit/starcraft-x86-exit`.
   - Avvia con `SC_BIN="$PWD/build/qa/gap-exit/starcraft-x86-exit" ./tools/run-isolated.sh start --id gap-exit --skip-intro 1 --smack 1` e attacca GDB con `watch *(unsigned int*)0x006DD684` (e breakpoint su `0x0802B...` / `__onexit_lk` `0x00406041` / `loc_406041` + il buffer puntato da `0x006DD684`, per capire ESATTAMENTE chi scrive `0x1805F2D4` nella tabella onexit!).
   - Clicca `Exit` nel menu principale (cattura prima `shot` per la coordinata esatta di `Exit`, circa `(220, 405)`), cattura il writer di `0x1805F2D4`, correggi la causa radice (in `crt_init_arm.c` o nel writer dimostrato, oppure documenta se risiede in `dispatch-ir.py` / `StarCraft.entry.ll` fornendo la patch esatta per il merge!), e verifica che cliccando `Exit` il gioco esca pulitamente con `exit 0` (zero `STOP 0x1805F2D4`)!
2. **Unit test completo DirectSound (`tests/x86_dsound_test.c`)**:
   - Compila `com_dsound.c` + `tests/x86_dsound_test.c` (con stub minimali `host_elf_audio_*` per testare in isolamento) in `build/qa/gap-exit/x86_dsound_test` con `clang -m32`.
   - Testa: `DirectSoundCreate`, `SetCooperativeLevel`, `CreateSoundBuffer` (primaria + 32+ secondarie per esercitare LRU stealing senza crash), `DuplicateSoundBuffer`, `Lock`/`Unlock`, `Play`/`Stop`, `SetVolume`/`GetVolume`, `SetPan`/`GetPan`, `SetFrequency`/`GetFrequency`, `Release`.
3. **Analisi binaria `X11_ShowWindow` / `sdl2-compat` (`host_elf.c`)**:
   - Ispeziona con `nm -D` / `readelf -d` `/lib/libSDL2-2.0.so.0` e `/lib/libSDL3.so.0` (`sdl2-compat` i686 su `frigate`) e verifica in `Xvfb` isolato se in `host_elf.c` una sequenza `SDL_WINDOW_HIDDEN` → `SDL_PumpEvents()` → `SDL_ShowWindow()` o un hint SDL3 cambia il percorso `X11_ShowWindow` (senza aprire finestre su `:0`!), e documenta esattamente i fatti misurati.
4. **Report, STATE e `qmem_store`**:
   - Scrivi `reports/REPORT-gap-exit.md`, `reports/STATE-gap-exit.md`, chiudi `gap-exit` (`stop`), e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
