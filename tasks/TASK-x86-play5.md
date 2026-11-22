# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA o crash guest (`0x0435A008`, `0x0040912C`, `0x0046A4D2`), usa `llvm-objdump -d` sui PE originali (`build/lift-full/StarCraft.orig.exe`, `storm.orig.dll`) e `rg` mirato su `build/lift-full/*.llasm` (o IDA se disponibile; controlla `ida_list` prima).
2. **Autonomia**: hai piena autorizzazione a modificare `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/`, `build/qa/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Regola di isolamento OBBLIGATORIA**:
   - **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` o `DISPLAY=:0` o con audio reale!**
   - Usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (display Xvfb privato, `SDL_AUDIODRIVER=dummy`, `SC_CONTROL_FILE`, zero finestre sullo schermo dell'utente, zero suono).
   - Ricordati sempre di fare `./tools/run-isolated.sh stop --id <id>` prima di ricostruire o alla fine!
5. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non leggere per intero `build/lift-full/*.ll`, non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-play5`: Risolvere STOP `0x0435A008` dopo Briefing Start → ingresso in partita (Tutorial / Prima Missione) + `qmem_store`

Leggi prima:
- `reports/STATE-x86-play4.md` e `reports/REPORT-x86-play4.md`

## Fatti già verificati (non ridimostrare)
- Sequenza isolata verificata (640×480, `--skip-intro 1 --smack 1`, pause ≥2s):
  1. Single Player: `'click 220 120'`
  2. StarCraft Original: `'click 260 226'`
  3. Registry profilo `enne2ù`: `'click 100 109'` poi `'click 517 391'`
  4. Campagna Terran Episode I: `'click 305 250'`
  5. Story text REBEL YELL: `'click 320 440'` (supera title card BOOT CAMP → Briefing Terran)
  6. Briefing Terran (`play4-17-after-scode-fix.png`): Start `'click 538 390'` (Skip Tutorial `'click 150 445'`)
- Dopo click Start `'click 538 390'`:
  `STOP: target=0x0435A008`, `eip chiamante=0`, `esp=0x021FF9E8`, `_eax=0x0066FFA0`, `_ecx=0xFFFFFFFA`, ring: `0x0046A4D2`, `0x0046A427`, `0x0046A45A`, `0x0040912C`.
- Nota importante: `0x0040912C` è uno dei 12 siti di tabella di salto toccati da `RE_TABLE_BASE` in `tools/dispatch-ir.py` (probabile `memcpy`/`memmove` o `memset`!), e `_ecx=0xFFFFFFFA` (`-6`) è un indice negativo!

## Obiettivi e ordine di esecuzione

1. **Diagnosi esatta di `0x0435A008` (`0x0040912C` / `0x0046A4D2`)**:
   - Ispeziona subito `llvm-objdump -d --start-address=0x00408FD0 --stop-address=0x00409200 build/lift-full/StarCraft.orig.exe` e `rg -n -C 8 'loc_40912C|40912C' build/lift-full/StarCraft.llasm` (e in `build/lift-full/entry-x86/StarCraft.entry.ll` con `rg -n -C 5 '40912C|loc_40912C|40902C'`), più `0x0046A400..0x0046A520` in `StarCraft.orig.exe`!
   - Verifica anche in GDB isolato (`play5` avviato con `tools/run-isolated.sh start --id play5 --skip-intro 1 --smack 1`, breakpoint condizionale su `reasm_indirect_unresolved` per `target==0x0435A008`):
     stampa `bt`, `*(_cpu*)g_cpu_for_signal`, `x/16wx cpu->_esp`, e il ring degli archi per confermare quale istruzione tradotta scrive `0x0435A008` in `_ind_target`!
   - Controlla se il bug è in `RE_TABLE_BASE` (`tools/dispatch-ir.py` — es. indice scalato o offset sbagliato a `0x0040912C` / `0x0040902C+4`, oppure `StarCraft.entry.ll` non rigenerato per tutti gli usi, oppure `0x0040912C` è `jmp *table[ecx*4]` vs qualcos'altro in `StarCraft.orig.exe`!), OPPURE un altro bug in `0x0046A4D2`!

2. **Applica il fix pulito (D19 intatto), ricostruisci e entra in partita**:
   - Ferma `play5`, applica il fix dimostrato (se cambi `tools/dispatch-ir.py`, riesegui `./tools/build.sh --arch x86` senza `--skip-entry`, altrimenti `./tools/build.sh --arch x86 --skip-entry`), verifica `./tests/x86_runtime_check/check.sh`.
   - Riprendi la navigazione isolata con `./tools/run-isolated.sh start --id play5 --skip-intro 1 --smack 1` attraverso Briefing → Start `'click 538 390'`!
   - Se compare un altro ostacolo dopo Start (es. SCode opcodes, `0x004D4DCA` race font MPQ, API mancante, ecc.), diagnosticalo e risolvilo pulitamente fino a **entrare nel Tutorial / Prima Missione**!
   - Verifica visivamente con `shot` + `read` la schermata **in-game** (mappa, Command Center, SCV, minerali, minimappa, HUD Terran), fai opzionalmente un piccolo click/selezione per osservare l'HUD/controlli reali, e salva gli screenshot chiave in `build/qa/play-screenshots/play5-*.png`!

3. **Memorizzazione in `qmem_store` + report**:
   - Salva in `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`):
     - Causa radice e fix di `0x0435A008` (e di eventuali altri bug risolti dopo Start).
     - Procedura completa verificata, coordinate di tutte le schermate (fino a Start e HUD di gioco), e modalità/controlli di gioco osservati!
   - Scrivi `reports/REPORT-x86-play5.md` e `reports/STATE-x86-play5.md` (max 70 righe).
   - **Chiudi sempre `play5` (`./tools/run-isolated.sh stop --id play5`)** alla fine!
   - Se raggiungi ~175k token, chiudi `play5`, salva qmem + report + STATE e fermati.

## Gate di accettazione (oggettivi)
1. Zero finestre visibili / zero audio reale (solo `tools/run-isolated.sh`).
2. Causa radice e fix di `0x0435A008` dimostrati e verificati con build + `./tests/x86_runtime_check/check.sh`.
3. Screenshot ispezionati con `read` in `build/qa/play-screenshots/play5-*.png` che mostrano il **Tutorial / Prima Missione avviata in-game** (oppure dimostrazione esatta dell'eventuale blocco successivo dopo aver superato `0x0435A008`).
4. `qmem_store` aggiornato + `reports/REPORT-x86-play5.md` + `reports/STATE-x86-play5.md` completi + nessuna istanza `run-isolated.sh` attiva.
