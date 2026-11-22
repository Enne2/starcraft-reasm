# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` (e IDA su copia privata se utile; controlla `ida_list` prima).
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai disabilitare `proc_range` o eseguire SCode non verificato.
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `SC_BIN="$PWD/build/qa/gap-gfx/starcraft-x86-gfx" ./tools/run-isolated.sh` e chiudi sempre con `stop`.
5. **Divieti**: NON toccare file fuori proprietà (`win32_shim.c`, `host_elf.c`, `snp_elf.c`, `com_dsound.c`, `build/starcraft-x86`, `build/obj-x86/`). Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-g-gfx2`: Completamento grafica — test clipper + `SetDIBits`, ritratti Briefing, Refinery Boot Camp

## Contesto di compilazione (esatto, da `tools/build-shim-x86.sh`!)
- Gli header `reasm_elf.h` si trovano in **`${TOOLKIT:-$HOME/dev/reasm-toolkit}/runtime/elf`**!
- Comando esatto per compilare i tuoi shim in `build/qa/gap-gfx/`:
  ```sh
  TOOLKIT="${TOOLKIT:-$HOME/dev/reasm-toolkit}"
  SDL_CFLAGS="$(pkg-config --cflags sdl2)"
  clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g \
    -Wno-ignored-attributes -Wno-incompatible-library-redeclaration -Wno-string-compare -Wno-self-assign -Wno-builtin-requires-header \
    -DSC_ELF_REAL_GDI=1 -DSC_PORT_EMULATED_TEB=1 -include stdint.h \
    -I runtime/shim-port/elf -I runtime/common -I "$TOOLKIT/runtime/elf" -I vendor/sr/SR/llasm-support $SDL_CFLAGS \
    -c <file.c> -o build/qa/gap-gfx/<file.o>
  ```
  Poi applica `llvm-objcopy --redefine-syms=runtime/shim-port/shim_rename.txt` su `<file.o>` prima di linkare `build/qa/gap-gfx/starcraft-x86-gfx` (vedi `tools/link-x86.sh` usando gli altri `.o` da `build/obj-x86/{runtime,shim-redef}/` in sola lettura + `build/lift-full/entry-x86/*.entry.o`!).
- Leggi `reports/STATE-gap-gfx.md` e `reports/REPORT-gap-gfx.md` (già applicata prima bozza `blit_clipped` in `com_ddraw.c` da verificare/completare!).

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/com_ddraw.c`
- `runtime/shim-port/elf/gdi_elf.c`
- `runtime/shim-port/elf/smack_elf.c`
- `runtime/shim-port/elf/smack_audio.c`
- `runtime/common/storm_sdraw_arm.c`
- `tests/x86_gfx_test.c` (nuovo)
- `build/qa/gap-gfx/`
- `reports/REPORT-gap-gfx.md`
- `reports/STATE-gap-gfx.md` (max 70 righe)

## Obiettivi
1. **Compila/verifica `blit_clipped` in `com_ddraw.c` e scrivi/esegui `tests/x86_gfx_test.c`**:
   - Verifica la correttezza di `blit_clipped` (`s_Blt` e `s_BltFast` quando c'è un clipper con `RGNDATA`, clamp dei rettangoli, sorgente/destinazione parzialmente fuori clip).
   - Scrivi `tests/x86_gfx_test.c` (`build/qa/gap-gfx/x86_gfx_test`, `-m32`): testa `IDirectDrawClipper` (`SetClipList`, `GetClipList`, `IsClipListChanged`, `SetClipper`/`GetClipper`, `s_Blt` clippato con verifica pixel-per-pixel dentro/fuori clip) E `SetDIBits` (`8↔24/32 bpp` con `RGBQUAD`, palette vuota fallback, stride 4-byte)!
2. **Linka `build/qa/gap-gfx/starcraft-x86-gfx` e diagnostica i ritratti del Briefing (`26-briefing.png`)**:
   - Avvia con `SC_BIN="$PWD/build/qa/gap-gfx/starcraft-x86-gfx" ./tools/run-isolated.sh start --id gap-gfx --skip-intro 1 --smack 1`.
   - Raggiungi il Briefing Terran (`26-briefing`), ispeziona lo screenshot con `read`, e con log mirato su `SmackOpen` / `SmackToBuffer` / `SmackToBufferRect` / `storm_357` / `storm_334` / `SetPalette` / `Blt` trova perché 2 riquadri erano neri e 1 aveva scanline (es. `dst_delta` / `rows` interlace o color-key o ordine surface/palette!), fissa il bug dimostrato se è in `smack_elf.c`/`com_ddraw.c`/`storm_sdraw_arm.c`/`gdi_elf.c` e verifica con un nuovo screenshot!
3. **Avanza Boot Camp fino a costruire una Refinery**:
   - Nella stessa istanza `gap-gfx`, avvia la partita, manda gli SCV ai minerali, raccogli 100 minerali, seleziona un SCV e costruisci una **Refinery** sul geyser Vespene (verifica screenshot con `read` e assenza di nuovi STOP/SCode!).
4. **Chiusura, report e `qmem_store`**:
   - Ferma `gap-gfx` (`stop`), scrivi `reports/REPORT-gap-gfx.md` + `reports/STATE-gap-gfx.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
