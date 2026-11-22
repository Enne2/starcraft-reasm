# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, dialoghi `SDlg`, vtable o crash guest (`0x90909090` a `0x004118E4`–`0x004118F1` / `0x0040F652`), usa `llvm-objdump -d` sui PE originali (`build/lift-full/StarCraft.orig.exe`, `storm.orig.dll`) e `rg` mirato su `build/lift-full/*.llasm` (o IDA se disponibile; controlla `ida_list` prima).
2. **Autonomia**: hai piena autorizzazione a modificare `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/`, `build/qa/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Regola di isolamento OBBLIGATORIA**:
   - **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` o `DISPLAY=:0` o con audio reale!**
   - Usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (display Xvfb privato, `SDL_AUDIODRIVER=dummy`, `SC_CONTROL_FILE`, zero finestre sullo schermo dell'utente, zero suono).
   - Ricordati di fare `./tools/run-isolated.sh stop --id <id>` prima di ricostruire o alla fine!
5. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non leggere per intero `build/lift-full/*.ll`, non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-play2`: Campagna Terran → Briefing → Tutorial / Prima Missione + Diagnostica `0x90909090` + `qmem_store`

Leggi prima:
- `reports/STATE-x86-play.md` e `reports/REPORT-x86-play.md`

Nota: `tools/run-isolated.sh` ora copia automaticamente i `.spc` da `build/asset-sandbox/characters/` (`enne2ù.spc`, 44 byte) nella cartella privata `build/isolated/<id>/sandbox/characters/`.

## Obiettivi e ordine di esecuzione

1. **Test con profilo esistente + navigazione interattiva (`--id play2`)**:
   - Avvia: `./tools/run-isolated.sh start --id play2 --skip-intro 1 --smack 1` (SENZA `--script campaign`, usa `cmd` + `shot` passo passo!).
   - Fai `shot` + `read` per ogni schermata e manda UN `cmd` alla volta:
     - `focus`, `click 220 120` (Single Player) -> `shot` -> `read`
     - `click 260 226` (StarCraft Original) -> `shot` -> `read`
     - Nella schermata Registry/Profilo: verifica se il profilo `enne2ù` è già listato; selezionalo e clicca `OK` (o se serve creane uno pulito), `shot` -> `read`.
     - Nella schermata **Campagna** (Umani/Terran, Protoss, Zerg): clicca la campagna **Terran (Umani)** (es. `313 256` o coordinate viste dallo screenshot!) -> `shot` -> `read`.
     - Nella schermata **Missioni / Tutorial** (se appare `Boot Camp` / `Tutorial` vs `Mission 1` o lista missioni): seleziona il **Tutorial / prima missione** -> `shot` -> `read`.
     - Nella schermata **Briefing Terran**: verifica visivamente obiettivi/briefing, clicca **Start** (`~520 400` o coordinate viste sullo screenshot!) -> `shot` -> `read`.
     - Verifica l'ingresso effettivo in partita (mappa di gioco / unità / HUD Terran / SCV / Command Center!) con `shot` + `read`!

2. **Diagnostica e fix di `0x90909090` e di qualsiasi blocco lungo il percorso**:
   - Se `build/starcraft-x86` si ferma ancora su `0x90909090` (o su qualsiasi altro STOP/SIGSEGV durante profilo, campagna, briefing o ingresso in partita come `0x004D4DCA` / font async race / SCode / API non implementata):
     - Diagnosticalo in modo isolato (usa GDB in `build/isolated/play2/sandbox` con `env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` o attach/xvfb privato, MAI visibile!).
     - Controlla `llvm-objdump -d --start-address=0x00411780 --stop-address=0x00411950 build/lift-full/StarCraft.orig.exe` e `0x0040F652` / `0x0050CF90` (`0x90909090` in `.text` padding o heap/stack/stub non inizializzato o bug di traduzione/CRT/stream/CreateFile/SCode!).
     - Applica il fix pulito (rispettando D19!), ricostruisci (`./tools/run-isolated.sh stop --id play2` poi `./tools/build.sh --arch x86 --skip-entry`), verifica `./tests/x86_runtime_check/check.sh` e ripeti il run isolato!

3. **Raccolta conoscenza e `qmem_store` (`project_id="starcraft-reasm-generic"`)**:
   - Registra in `qmem_store`:
     - Le **procedure esatte e i comandi** per avviare e pilotare istanze isolate parallele (`tools/run-isolated.sh` + `SC_CONTROL_FILE` + `Xvfb` + `SDL_AUDIODRIVER=dummy`).
     - Le **coordinate esatte 640x480 verificate visivamente** di ogni schermata raggiunta (Main Menu, Select Game Type, Registry/Profilo, Scelta Campagna Terran, Selezione Tutorial/Missione 1, Briefing Terran Start, HUD di gioco) + modalità di gioco osservate (controlli mouse/tastiera, comportamento `SC_SMACK`, `SC_SKIP_INTRO`).
     - Ogni bug diagnosticato e risolto (es. `0x90909090` e qualsiasi altro) con indirizzi e cause radice.
   - Salva gli screenshot chiave in `build/qa/play-screenshots/` (copiati prima di `stop`!) e scrivi `reports/REPORT-x86-play2.md` e `reports/STATE-x86-play2.md` (max 70 righe).
   - Se raggiungi ~175k token, ferma l'iterazione, chiudi `play2` (`stop`), salva report + STATE + qmem e chiudi il turno.

## Gate di accettazione (oggettivi)
1. Nessuna finestra visibile sullo schermo utente né audio emesso durante i test (solo `tools/run-isolated.sh`).
2. Risoluzione di `0x90909090` (e di eventuali blocchi successivi) con evidenza PE/GDB/build se incontrato.
3. Screenshot ispezionati con `read` in `build/qa/play-screenshots/` che mostrano: Single Player → Original → Campagna Terran → Briefing Terran → avvio Tutorial / Prima Missione (o l'ultimo ostacolo esattamente dimostrato se non superabile nella singola passata).
4. `qmem_store` aggiornato + `reports/REPORT-x86-play2.md` + `reports/STATE-x86-play2.md` completi + nessuna istanza `run-isolated.sh` lasciata accesa.
