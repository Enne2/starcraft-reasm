# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/storm.orig.dll` (`storm_357`, `storm_457` `0x1502F500`, `sub_1502EFA0`, `storm_334` `0x15030AB0`) e `StarCraft.orig.exe` se serve.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai disabilitare `proc_range` o alterare formato a tentativi; trova la vera causa nei buffer/palette/stride dei ritratti del briefing!
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `./tools/run-isolated.sh` con `SC_BIN="$PWD/build/qa/fix-portraits/starcraft-x86-port"` e chiudi sempre con `stop`.
5. **Divieti**: NON toccare `win32_shim.c`, `win32_shell_dlg.c`, `build/starcraft-x86`, `build/obj-x86/`. Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-portraits`: Diagnosi & Fix dei ritratti animati nel Briefing Terran

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/smack_elf.c`
- `runtime/common/storm_sdraw_arm.c`
- `build/qa/fix-portraits/`
- `reports/REPORT-fix-portraits.md`
- `reports/STATE-fix-portraits.md` (max 70 righe)

## Obiettivi
1. **Ispeziona i dati reali del Briefing (rimuovendo temporaneamente i cap di log su `smack_elf.c` nella tua build privata)**:
   - Leggi `build/qa/close-play/07-briefing-portraits.png` (se presente) o `build/qa/play-screenshots/play6-07-bootcamp.png` con `read` per vedere il difetto (2 riquadri neri, 1 con scanline).
   - Nota in `smack_elf.c` / `storm_sdraw_arm.c`: perché solo i primi `SmackToBuffer` vengono loggati? Perché molti slot ritratto 60×56 usano `SmackBufferToBufferTrans`, `SmackToBuffer` con `dest_pitch`, `SmackBufferCopyPalette`, oppure `storm_334` / `storm_357` (o `flags != 0` / interlacciato `height = rows / 2` / `dst_delta` doppio per scanline!)!
   - Controlla in `smack_elf.c`:
     - Come `SmackOpen` / `SmackToBuffer` / `SmackBufferOpen` / `SmackToBufferRect` gestiscono `flags`, `interlaced` (`smk_info_video` height o `flags` di Smacker!), `pitch`, `SmackBufferToBufferTrans` (`Trans` key color vs full copy!), `BufferNewPalette`!
     - Controlla in `storm.orig.dll` `0x1502E430..0x1502F800` (`storm_454`, `storm_457`, `sub_1502EFA0`, `storm_357`) come Storm passa `flags`, `pitch`, `y` e `SmackToBuffer` per i ritratti `60x56`! (Nel log precedente `storm_334` aveva `width=60, rows=56, dst_delta=120` = `2 * 60` per il 2x scaling — e nota che `ToBufferRect` o `SmackToBuffer` per i ritratti 60×56 potrebbe avere righe pari nere / scanlines!)
2. **Applica il fix dimostrato**:
   - Compila `smack_elf.o` (con `--redefine-syms`) e `storm_sdraw_arm.o` in `build/qa/fix-portraits/`, linka `build/qa/fix-portraits/starcraft-x86-port` seguendo `tools/link-x86.sh` (riusando gli altri `.o` di `build/obj-x86/` in sola lettura).
   - Avvia con `SC_BIN="$PWD/build/qa/fix-portraits/starcraft-x86-port" ./tools/run-isolated.sh start --id fix-port --skip-intro 1 --smack 1`, entra nel Briefing Terran e ispeziona i ritratti con `shot` + `read`!
3. **Chiusura, report e `qmem_store`**:
   - Rimuovi eventuali log di debug eccessivi lasciando solo log puliti, verifica la build privata sul Briefing con screenshot nitidi in `build/qa/fix-portraits/`, ferma `fix-port` (`stop`), scrivi `reports/REPORT-fix-portraits.md` + `reports/STATE-fix-portraits.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
