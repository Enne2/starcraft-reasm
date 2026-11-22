# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` (e IDA su copia privata se utile; controlla `ida_list` prima).
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai disabilitare controlli `proc_range` o eseguire bytecode SCode non verificato.
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `SC_BIN="$PWD/build/qa/gap-gfx/starcraft-x86-gfx" ./tools/run-isolated.sh` e chiudi sempre con `stop`.
5. **Divieti**: NON toccare file fuori proprietà (`win32_shim.c`, `host_elf.c`, `snp_elf.c`, `com_dsound.c`, `build/starcraft-x86`, `build/obj-x86/`). Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gap-gfx`: Ritratti Briefing, Clipping reale `Blt` in `com_ddraw.c`, Refinery Boot Camp & unit test `x86_gfx_test`

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/com_ddraw.c`
- `runtime/shim-port/elf/gdi_elf.c`
- `runtime/shim-port/elf/smack_elf.c`
- `runtime/shim-port/elf/smack_audio.c`
- `runtime/common/storm_sdraw_arm.c`
- `tests/x86_gfx_test.c` (nuovo)
- `build/qa/gap-gfx/` (oggetti compilati, binario privato `starcraft-x86-gfx`, screenshot, test binary)
- `reports/REPORT-gap-gfx.md`
- `reports/STATE-gap-gfx.md` (max 70 righe)

## Obiettivi
1. **Diagnostica e fix dei ritratti del Briefing (`26-briefing.png`)**:
   - In `REPORT-gfx.md` era emerso che i pixel decodificati da `SmackDoFrame` hanno hash valido e palette presente, ma al briefing solo un riquadro era visibile con scanline e gli altri due neri.
   - Indaga con il tuo binario privato `build/qa/gap-gfx/starcraft-x86-gfx` + `tools/run-isolated.sh` / GDB cosa succede durante il briefing:
     - guarda i `DoFrame`/`ToBuffer`/`ToBufferRect` chiamati per i 3 slot ritratto,
     - verifica se `SmackBufferToBufferTrans` / `SmackBufferCopyPalette` / `BufferNewPalette` / `SDraw` `storm_334` / `storm_357` / `ToBufferRect` (es. scanline `rows` o `dst_delta` o trasparenza/color-key o `CreateSurface` / `Blt` / `Unlock`) è responsabile delle scanline e dei riquadri neri!
     - Implementa il fix dimostrato e verifica visivamente con `shot` + `read` che i ritratti nel briefing siano nitidi e visibili!
2. **Clipping reale in `com_ddraw.c` (`s_Blt` / `s_BltFast`)**:
   - Quando la superficie di destinazione ha un `IDirectDrawClipper` con clip list (`RGNDATA` non vuota), interseca il rettangolo di destinazione (e corrispondente porzione `src`) con i rettangoli della clip list in `s_Blt` / `s_BltFast` (senza impattare le superfici senza clipper).
3. **Unit test completo `tests/x86_gfx_test.c`**:
   - Compila ed esegui in `build/qa/gap-gfx/x86_gfx_test` (`-m32`):
     - `IDirectDrawClipper`: `SetClipList`, `GetClipList`, `IsClipListChanged`, `SetClipper`/`GetClipper`, `Blt` effettivamente clippato contro rettangolo (verifica pixel fuori clip intatti e pixel dentro clip aggiornati!).
     - `gdi_elf.c`: `SetDIBits` `8↔24/32 bpp` con `RGBQUAD` e fallback palette vuota, stride allineato a 4 byte.
4. **Completamento Boot Camp fino a Refinery**:
   - Nella tua istanza isolata `gap-gfx`, dopo aver costruito il Supply Depot (o accumulando minerali con gli SCV sui minerali), costruisci anche una **Refinery** sul geyser Vespene e verifica che non emergano nuovi opcode SCode/ROP mancanti (se emergono, implementali fedelmente in `storm_sdraw_arm.c`!).
5. **Report, STATE e `qmem_store`**:
   - Chiudi `gap-gfx` (`stop`), scrivi `reports/REPORT-gap-gfx.md` + `reports/STATE-gap-gfx.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
