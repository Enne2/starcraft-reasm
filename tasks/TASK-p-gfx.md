# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` (e `rg` mirato su `build/lift-full/*.llasm`) per verificare ogni opcode SCode, flag Smacker/DirectDraw o formato pixel!
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai stub silenziosi o esecuzione "a fiducia" di SCode non validati; mantieni `proc_range` e controllo esplicito degli opcode/ROP.
4. **Regola di isolamento**: MAI finestre visibili né audio reale! Per testare nel gioco usa `./tools/run-isolated.sh` con `SC_BIN=<tuo_binario_privato>` (es. `build/qa/gfx/starcraft-x86-gfx`), MAI sovrascrivere `build/starcraft-x86` né `build/obj-x86/`!
5. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (`win32_shim.c`, `host_elf.c`, `snp_elf.c`, `com_dsound.c`, `build/starcraft-x86`, `build/obj-x86/`).
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gfx`: Ritratti Briefing, SCode in-game (Boot Camp completo), `DirectDrawClipper`, `GDI` bpp conversion & `Smack` flags/volume

## File di proprietà (esclusivi)
- `runtime/common/storm_sdraw_arm.c`
- `runtime/shim-port/elf/com_ddraw.c`
- `runtime/shim-port/elf/gdi_elf.c`
- `runtime/shim-port/elf/smack_elf.c`
- `runtime/shim-port/elf/smack_audio.c`
- `build/qa/gfx/` (oggetti compilati + binario privato `starcraft-x86-gfx` linkato da te se ti serve provare in-game con `SC_BIN=build/qa/gfx/starcraft-x86-gfx ./tools/run-isolated.sh ...`!)
- `reports/REPORT-gfx.md`
- `reports/STATE-gfx.md` (max 70 righe)

## Obiettivi
1. **Ritratti animati nel Briefing + `Smack` flags/volume**:
   - Indaga perché i ritratti nel briefing (`play6-07-bootcamp.png`) apparivano neri: controlla `smack_elf.c` (`SmackToBuffer` flags != 0, `SmackBufferNewPalette`, `SmackDoFrame`) e `storm_sdraw_arm.c` / `com_ddraw.c` durante il briefing!
   - Implementa in `smack_elf.c` / `smack_audio.c` eventuali `ToBuffer` flags reali mancanti e il controllo reale volume/pan (`SmackSoundUseDirectSound` / `SmackVolumePan` su `smack_audio.c`)!
2. **DirectDrawClipper (`com_ddraw.c`) & GDI bpp (`gdi_elf.c`)**:
   - Implementa in `com_ddraw.c` un clipper reale (`s_SetClipper`, `s_GetClipper`, `c_SetHWnd`, `c_SetClipList`, `c_GetClipList`, `c_IsClipListChanged`) invece di tornare sempre `DDERR_NOCLIPLIST`.
   - Implementa in `gdi_elf.c` la conversione di profondità colore mancante alla riga 956 (`bpp != b->bpp`, es. 8bpp→24/32bpp e 24/32bpp→8bpp con palette).
3. **Esercizio in-game Boot Camp & nuovi kernel SCode (`storm_sdraw_arm.c`)**:
   - Compila i tuoi 5 `.c` modificati in `build/qa/gfx/` (riuscendo gli altri `.o` già in `build/obj-x86/` in sola lettura per linkare `build/qa/gfx/starcraft-x86-gfx`!).
   - Con `SC_BIN=$PWD/build/qa/gfx/starcraft-x86-gfx ./tools/run-isolated.sh start --id gfx-camp --skip-intro 1 --smack 1`:
     - Entra in Boot Camp (usa le coordinate verificate in `reports/STATE-x86-play6.md`!), ispeziona lo screenshot del briefing (verifica se i ritratti/colori migliorano!) e prosegui in partita: **ordina a un SCV di costruire un Supply Depot / Refinery**, apri il menu in-game (`MENU` button), prova **Zerg / Protoss** campaign screen!
     - Se emergono nuovi opcode/ROP SCode o colori errati in `storm_sdraw_arm.c`, implementali fedelmente (con `proc_range`, D19 intatto!).
   - **Chiudi sempre `gfx-camp` (`stop`)** alla fine!
4. **Report, STATE e `qmem_store`**:
   - Documenta tutti i fix verificati, gli screenshot ispezionati con `read`, e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
