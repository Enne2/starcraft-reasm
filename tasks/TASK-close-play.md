# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` (e copy DB se serve; controlla `ida_list`).
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato). Hai piena autorizzazione a modificare `runtime/shim-port/elf/{com_ddraw.c,gdi_elf.c,smack_elf.c,smack_audio.c,win32_shell_dlg.c,win32_shim.c}`, `runtime/common/storm_sdraw_arm.c`, e ricostruire con `./tools/build.sh --arch x86 --skip-entry` se trovi un fix per i ritratti del briefing o per Save/Load.
3. **Fail-closed (D19)**: mai disabilitare `proc_range` o aggiungere stub silenziosi.
4. **Isolamento OBBLIGATORIO**: usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (`--id close-play`) — MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale, chiudi sempre con `stop`!
5. **Divieti**: niente sotto-agenti, niente `git`, niente input desktop, non toccare `vendor/`.

---

# Missione `scx86-close-play`: Ritratti Briefing, Save/Load reale in Boot Camp & Refinery su `build/starcraft-x86`

## Obiettivi e ordine di esecuzione

1. **Briefing Terran: diagnosi e fix dei ritratti (`play6-07-bootcamp.png` / `26-briefing.png`)**:
   - Avvia `./tools/run-isolated.sh start --id close-play --skip-intro 1 --smack 1` (usa direttamente `build/starcraft-x86` appena integrato!).
   - Raggiungi il **Briefing Terran Boot Camp** (`220 120` → `260 226` → `100 109` + `517 391` → `305 250` → `320 440` → `320 440`).
   - Cattura `shot` e controlla `build/isolated/close-play/game.log` intorno alle chiamate `SmackOpen` / `SmackBufferOpen` / `SmackToBuffer` / `SmackDoFrame` / `storm_357` / `storm_334` / `SetPalette` del briefing:
     - Scopri perché in `26-briefing.png` due slot ritratto risultano neri e uno ha scanline!
     - Controlla attentamente `smack_elf.c` (`SmackToBuffer`, `SmackToBufferRect`, `SmackBufferCopyPalette`, `SmackBufferToBufferTrans`, `SmackDoFrame`, `px0`/`dst0` vs palette!) e `storm_sdraw_arm.c` (`storm_357`, `storm_334`, `dst_delta`, `src_delta`, interlaced scanlines?) — nota: nel log precedente di `gfx-camp2` non comparivano tutte le chiamate? Verifica ora con `SC_SMACK_LOG` / log mirato su `smack_elf.c` e `storm_sdraw_arm.c`!
     - Se trovi la causa esatta (es. un'API Smacker mancante o `ToBuffer` stride/scanline/palette o `SDraw` LUT/trans), fissala pulitamente, ricostruisci con `./tools/build.sh --arch x86 --skip-entry` e verifica lo screenshot del briefing con `read`!

2. **Boot Camp: Save Mission / Load Mission reale**:
   - Entra in Boot Camp (`Start` `538 390` → Tips `OK` `197 262`), apri `MENU` (vedi coordinata nel HUD in-game, es. `F10` / bottone `MENU`), cattura screenshot e leggi con `read`!
   - Testa **Save Mission** (inserisci nome es. `testsave` e conferma OK — verifica se compare file `.snx`/`.sav` nella sandbox di `close-play`!) e poi **Load Mission** (verifica con screenshot + `read` che ricarichi la missione salvata!).

3. **Boot Camp: raccolta minerali + costruzione Refinery**:
   - Seleziona un SCV, mandalo sui minerali (click destro sul cristallo) o usa gli SCV esistenti per raccogliere >= 100 minerali, seleziona un SCV → Build Basic Structure → **Refinery** (sul geyser Vespene visibile nella mappa!) e cattura lo screenshot con `read` che mostra la Refinery in costruzione / costruita, verificando assenza di STOP/SCode!

4. **Chiusura, report e `qmem_store`**:
   - Chiudi sempre `./tools/run-isolated.sh stop --id close-play`.
   - Salva tutti gli screenshot chiave in `build/qa/close-play/`, aggiorna `qmem_store` (`project_id="starcraft-reasm-generic"`), e scrivi `reports/REPORT-close-play.md` + `reports/STATE-close-play.md` (max 70 righe).
   - Se tocchi ~175k token, chiudi `close-play`, salva report + STATE + qmem e fermati.
