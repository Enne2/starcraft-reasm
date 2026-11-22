# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA o crash guest, usa `llvm-objdump -d` sui PE originali (`build/lift-full/StarCraft.orig.exe`, `storm.orig.dll`) e `rg` mirato su `build/lift-full/*.llasm` (o IDA se disponibile; controlla `ida_list` prima).
2. **Autonomia**: hai piena autorizzazione a modificare `tools/dispatch-ir.py`, `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/`, `build/qa/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Regola di isolamento OBBLIGATORIA**:
   - **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` o `DISPLAY=:0` o con audio reale!**
   - Usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (display Xvfb privato, `SDL_AUDIODRIVER=dummy`, `SC_CONTROL_FILE`, zero finestre sullo schermo dell'utente, zero suono).
   - Ricordati sempre di fare `./tools/run-isolated.sh stop --id play6` prima di ricostruire o alla fine!
5. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non leggere per intero `build/lift-full/*.ll`, non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-play6`: Fix `RE_TABLE_BASE` su alias `@_text` in `tools/dispatch-ir.py`, build e avvio Tutorial / Prima Missione + `qmem_store`

Leggi prima:
- `reports/STATE-x86-play5.md` e `reports/REPORT-x86-play5.md`
- `tools/dispatch-ir.py` (sezione `RE_TABLE_BASE` / riscritture `ptrtoint`)

## Obiettivi e ordine di esecuzione

1. **Estendi `tools/dispatch-ir.py` per gli alias `@loc_<VA>` verso `@_text`**:
   - Controlla con `rg -n '= private alias i8,.*@_text' build/lift-full/StarCraft.ll build/lift-full/storm.ll` e `rg -n 'ptrtoint \(i8\* @loc_' build/lift-full/StarCraft.ll build/lift-full/storm.ll` tutti i siti coinvolti (in exe e in storm).
   - Verifica `tools/dispatch-ir.py`: estendi la riscrittura in modo che **ogni `ptrtoint (i8* @loc_<VA> to i32)` il cui simbolo `@loc_<VA>` è definito come alias dentro `@_text`** (e quindi appartiene al `.text` PE mappato da `guest_pe_bytes` ai VA originali `0x00401000..0x004FE000` / `0x15001000..0x15045000`) venga sostituito con la costante intera `va` (es. `0x00409268` in decimale), esattamente come già fatto per `void(%_cpu*)* @loc_<VA>`!
   - Controlla inoltre che la sostituzione non rompa ARM (`build-entry-obj.sh`) e mantenga `reasm_indirect_resolve` 100% fail-closed (D19).

2. **Rebuild completo e verifica gate**:
   - Esegui `./tools/build.sh --arch x86` (rebuild completo: `entry-x86`, `shim-x86`, `runtime-x86`, `link-x86`) e verifica che stampi il conteggio dei siti riscritti in `dispatch-ir.py`.
   - Esegui `./tests/x86_runtime_check/check.sh`, `LC_ALL=C readelf -h build/starcraft-x86` (`ELF32 / EXEC / Intel 80386`) e `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` (`0` collisioni).

3. **Avvio isolato fino a Tutorial / Prima Missione (`--id play6`)**:
   - Esegui `./tools/run-isolated.sh start --id play6 --skip-intro 1 --smack 1`.
   - Naviga con pause ≥2s e `cmd` quotato:
     1. `'click 220 120'` (Single Player)
     2. `'click 260 226'` (StarCraft Original)
     3. `'click 100 109'` poi `'click 517 391'` (Profilo `enne2ù` → OK)
     4. `'click 305 250'` (Terran Episode I)
     5. `'click 320 440'` (Story → Boot Camp → Briefing)
     6. `shot` -> `read` su Briefing, poi `'click 538 390'` (**Start**!)
   - Verifica con `shot` + `read` cosa succede dopo Start!
     - Se entra nel **Tutorial / Prima Missione in-game**: cattura gli screenshot del campo di battaglia (mappa, unità Terran / SCV / Command Center, minimappa, HUD), prova opzionalmente un click di selezione su un SCV o sul terreno (annotando coordinate e reazione), e verifica che il gioco resti attivo e reattivo!
     - Se incontra un nuovo STOP/SIGSEGV post-Start (es. un altro opcode SCode o race font `0x004D4DCA` o API non implementata), diagnosticalo subito con GDB isolato/objdump e se è un fix pulito e circoscritto (es. opcode SCode) applicalo e riprova!

4. **Salvataggio in `qmem_store` e report**:
   - Registra in `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`):
     - Fix di `RE_TABLE_BASE` in `tools/dispatch-ir.py` su alias `@_text`.
     - Mappa completa e verificata della procedura di gioco (`Menu -> Single Player -> Original -> Profilo -> Campagna Terran -> Briefing -> Start -> Partita/HUD`), coordinate `640x480`, controlli osservati ed esito della missione.
   - Salva tutti gli screenshot chiave ispezionati in `build/qa/play-screenshots/play6-*.png`.
   - Scrivi `reports/REPORT-x86-play6.md` e `reports/STATE-x86-play6.md` (max 70 righe).
   - **Chiudi sempre `play6` (`./tools/run-isolated.sh stop --id play6`)** alla fine!

## Gate di accettazione (oggettivi)
1. Zero finestre visibili / zero audio reale (solo `tools/run-isolated.sh`).
2. `./tools/build.sh --arch x86`, `./tests/x86_runtime_check/check.sh`, `readelf -h`, `--dump-map` tutti verdi.
3. Screenshot ispezionati con `read` in `build/qa/play-screenshots/play6-*.png` dopo **Briefing → Start**, con ingresso verificato nella partita / Tutorial (o esatta diagnosi dell'ultimo ostacolo se non superabile in questa passata).
4. `qmem_store` aggiornato + `reports/REPORT-x86-play6.md` + `reports/STATE-x86-play6.md` completi + nessuna istanza `run-isolated.sh` attiva.
