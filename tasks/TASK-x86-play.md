# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, dialoghi `SDlg`, bottoni, vtable o crash guest, l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`) oppure `llvm-objdump -d` sui PE originali (`StarCraft.orig.exe`, `storm.orig.dll`) e `rg` mirato su `build/lift-full/*.llasm`. Controlla `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Hai **piena autorizzazione** a modificare i file in `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/` e `build/qa/` se serve fissare bug emersi durante la navigazione dei menu/campagna.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - **NON iniettare input sul desktop OS** (`ydotool`, `kdotool`, `xdotool`, `gnome_click`, `gnome_type`): usa esclusivamente l'iniezione SDL interna al processo (`SC_INPUT_SCRIPT` / `SC_INPUT_REPLAY` in `runtime/shim-port/elf/host_elf.c`)!
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-play`: Giocare `build/starcraft-x86` — Single Player → Campagna Terran → Tutorial / Prima Missione + Documentazione `qmem`

## Contesto già verificato (da `qmem` e codice locale)
1. **Input interno al processo (`runtime/shim-port/elf/host_elf.c`)**:
   - Esistono già `SC_INPUT_SCRIPT=campaign`, `SC_INPUT_REPLAY=<file>`, `SC_REPLAY_AFTER_MS`, `SC_MOUSE_TRACE=1`, `SC_FORCE_WINDOWED=1`, `SC_FPS_LOG=1`, `REASM_TRACE=1`.
   - Da `qmem` (`sc-reasm`): in `640x480`, la sequenza `campaign` attraversa:
     - `Single Player` -> `Original` / `Expansion`
     - Dialogo profilo (`OK` a circa `x=235, y=313`)
     - Scelta razza `Terran` (circa `x=313, y=256`)
     - `Briefing Terran` (bottone `Start` a circa `x=520, y=400` o `x=560, y=380`)
   - Note storiche da `qmem` (`sc-reasm`):
     - `standard.snp`: `snp_elf.c` costruisce MPQ virtuale (`caps.dat` + `(signature)`).
     - Possibili ostacoli già visti su `sc-reasm` originale: click profilo/SDlg hit-test, eventuale race asincrona font MPQ (`0x004D4DCA`) dopo Start, oppure `SC_SMACK=0` vs `SC_SKIP_INTRO=1`.
2. **Stato attuale di `build/starcraft-x86` su `frigate`**:
   - Compilazione unificata: `./tools/build.sh --arch x86 --skip-entry` (exit `0`).
   - Esecuzione reale su `frigate`: `SDL_VIDEODRIVER=wayland` apre finestra `640x480`, renderer GPU e audio PipeWire (`SC_SMACKER_SO=.../build/i386-libs/libsmacker-1.2.0.so`).
   - Nota già osservata:
     - Senza `SC_SKIP_INTRO=1` (`build/qa/launch-user.log`): si ferma su `-> 0x90909090` (da `0x004117A4`).
     - Con `SC_SKIP_INTRO=1` ma senza input (`build/qa/verify-final-wayland.log`): anima i menu SMK (`309 fps`), poi entra nel loop on-exit `0x00404F26` e si ferma su `0x1805F2D4`.

## Obiettivi e ordine di esecuzione

1. **Ispeziona il meccanismo `SC_INPUT_SCRIPT` / `SC_INPUT_REPLAY` e lo stato della sandbox**:
   - Leggi `runtime/shim-port/elf/host_elf.c` (righe 890–1205) per capire esattamente cosa fa `SC_INPUT_SCRIPT=campaign`, come funziona `SC_INPUT_REPLAY`, e come/se si possono salvare frame PPM/BMP per ispezionare gli schermati (o usa `spectacle -b -n -f -o ...` / `SC_FORCE_WINDOWED=1` su Wayland, oppure aggiungi un dump frame opzionale controllato da env var `SC_DUMP_SCREEN_DIR` in `host_elf.c` — comodissimo e funziona anche con `SDL_VIDEODRIVER=dummy`!).
   - Controlla `ls -la build/asset-sandbox/characters/` (se manca o ci sono profili esistenti).

2. **Test iniziale `campaign` e diagnosi di eventuali blocchi**:
   - Prova prima in modalità veloce (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SC_FORCE_WINDOWED=1 SC_SMACK=0 SC_SKIP_INTRO=1 SC_INPUT_SCRIPT=campaign SC_MOUSE_TRACE=1 REASM_TRACE=1`) e poi anche con `SC_SMACK=1` (senza `SC_SMACK=0`) e `SDL_VIDEODRIVER=wayland`!
   - **Nota cruciale**: guarda subito se `0x1805F2D4` (l'uscita prematura dal menu!) o `0x90909090` scattano anche durante `SC_INPUT_SCRIPT=campaign`.
   - Ogni volta che il gioco si ferma prima di entrare nella prima missione / tutorial:
     - Diagnostica la causa reale con `gdb` / `REASM_TRACE=1` / `llvm-objdump` su `StarCraft.orig.exe` / `storm.orig.dll`.
     - **Se è un bug di runtime/shim/bridge/SCode/eventi/timer/font/layout x86**, correggilo pulitamente, ricostruisci con `./tools/build.sh --arch x86 --skip-entry` e riprova!
     - **Se è un problema di coordinate/tempi dei menu** (es. `SC_SMACK=1` carica a velocità diverse su `frigate` rispetto a `sc-reasm`, o serve `Original` vs `Brood War`, o serve cliccare `Tutorial` / `Boot Camp` / `Mission 1` / `OK` / `Start`), misura le coordinate reali dalle immagini catturate (ispeziona i PNG/BMP con `read`!) e prepara uno script `build/qa/terran_mission1.replay` per `SC_INPUT_REPLAY`!

3. **Esecuzione reale su `frigate` (Wayland) e verifica visiva**:
   - Quando il flusso arriva alla **campagna Terran → Briefing → Tutorial / Prima Missione**, fai una run reale su `frigate` con `SDL_VIDEODRIVER=wayland` (con finestra visibile `SC_FORCE_WINDOWED=1` + `SC_SMACKER_SO`), salva screenshot delle tappe chiave (`mainmenu`, `campaign`, `terran`, `briefing`, `ingame/tutorial`) in `build/qa/screenshots/` e **guarda le immagini con `read`** per confermare visivamente ogni schermata!

4. **Memorizzazione in `qmem` e report**:
   - Usa `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`) per salvare in modo strutturato e compatto:
     - **Procedura e comandi esatti** per avviare e pilotare StarCraft (`build/starcraft-x86`) su `frigate` (driver Wayland, env var, `SC_INPUT_SCRIPT`, `SC_INPUT_REPLAY`, `SC_SKIP_INTRO`, `SC_SMACK`).
     - **Mappa dei menu, coordinate `640x480` e tempi misurati** (Main Menu -> Single Player -> Original/Expansion -> Profilo/OK -> Campagna Terran -> Selezione Tutorial/Missione 1 -> Briefing -> Start).
     - **Modalità di gioco / controlli osservati** e **ogni bug incontrato + fix applicato** (o eventuale limite residuo dimostrato).
   - Scrivi `reports/REPORT-x86-play.md` e `reports/STATE-x86-play.md` (max 70 righe).
   - Se raggiungi ~175k token prima di completare tutti gli step, fermati dall'iterazione, scrivi subito report + STATE + `qmem_store` dei risultati raccolti fin lì, e chiudi il turno per il rilancio.

## Gate di accettazione (oggettivi)
1. Flusso completo tracciato e verificato con log + immagini ispezionate: Main Menu → Single Player → Campagna Terran → Briefing → avvio Tutorial / Prima Missione (oppure, se emerge un blocco architetturale non risolvibile entro la sessione, dimostrazione esatta via GDB/PE dell'ultimo ostacolo dopo aver risolto tutti i precedenti).
2. Se applicate modifiche al codice: `./tools/build.sh --arch x86 --skip-entry`, `./tests/x86_runtime_check/check.sh`, `readelf -h`, `--dump-map` tutti verdi.
3. Conoscenza raccolta salvata in `qmem_store` (`project_id="starcraft-reasm-generic"`).
4. `reports/REPORT-x86-play.md` e `reports/STATE-x86-play.md` completi.
