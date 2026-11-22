# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA o crash guest, usa `llvm-objdump -d` sui PE originali (`build/lift-full/StarCraft.orig.exe`, `storm.orig.dll`) e `rg` mirato su `build/lift-full/*.llasm` (o IDA se disponibile; controlla `ida_list` prima).
2. **Autonomia**: hai piena autorizzazione a modificare `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/`, `build/qa/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Regola di isolamento OBBLIGATORIA**:
   - **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` o `DISPLAY=:0` o con audio reale!**
   - Usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (display Xvfb privato, `SDL_AUDIODRIVER=dummy`, `SC_CONTROL_FILE`, zero finestre sullo schermo dell'utente, zero suono).
   - Ricordati sempre di fare `./tools/run-isolated.sh stop --id play4` prima di ricostruire o alla fine!
5. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non leggere per intero `build/lift-full/*.ll`, non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-play4`: Fix `win32_shim_timers_pump` + avvio Tutorial / Prima Missione Terran + `qmem_store`

Leggi prima:
- `reports/STATE-x86-play3.md` e `reports/REPORT-x86-play3.md`
- `runtime/shim-port/elf/win32_shim.c` (intorno alle righe 430-485) e `runtime/shim-port/elf/win32_ui.c` (intorno alle righe 940-985 per il pattern `arm_guest_call4`)

## Fatti già verificati (coordinate 640×480 con `tools/run-isolated.sh`)
- `cmd --id play4 '<comando intero>'` va **quotato** come singolo argomento.
- Sequenza già verificata:
  1. Single Player: `'click 220 120'` (attesa ≥2s)
  2. StarCraft Original: `'click 260 226'` (attesa ≥2s)
  3. Registry: profilo `enne2ù` già copiato in `sandbox/characters/`: `'click 100 109'` poi `'click 517 391'` (attesa ≥2.5s) — oppure se lista vuota: `'click 122 344'`, `'text enne2'`, `'key 13'`.
  4. Campagna: **Terran Episode I `'click 305 250'`** (attesa ≥3s).

## Obiettivi e ordine di esecuzione

1. **Fix immediato di `win32_shim_timers_pump` (`runtime/shim-port/elf/win32_shim.c`)**:
   - Ispeziona `win32_shim.c:430-485` e `win32_ui.c:940-985`.
   - Sotto `#if defined(__arm__) || defined(__i386__)` (e verificando eventuale puntatore host vs guest VA se qualche timer interno usasse funzioni C — controlla `arm_va_is_mapped` o come `win32_ui.c` distingue host vs guest, oppure `SetTimer`!), instrada il `TIMERPROC` tradotto tramite `arm_guest_call4(proc, hwnd, 0x113 /* WM_TIMER */, id, GetTickCount())` (nota: controlla `extern unsigned int arm_guest_call4(...)` weak come in `win32_ui.c`!).
   - Ricostruisci con `./tools/build.sh --arch x86 --skip-entry` e verifica `./tests/x86_runtime_check/check.sh`.

2. **Avanzamento interattivo fino a Tutorial / Prima Missione (`--id play4`)**:
   - Avvia `./tools/run-isolated.sh start --id play4 --skip-intro 1 --smack 1`.
   - Naviga passo-passo con pause ≥2s, `shot` + `read` a ogni schermata chiave, salvando gli screenshot in `build/qa/play-screenshots/play4-*.png`:
     - Menu → Single Player → Original → Registry (`enne2ù`) → Campagna (`Terran Episode I` `'click 305 250'`)
     - Schermata selezione missione / **Boot Camp (Tutorial)** o **Wasteland (Missione 1)**: leggi lo screenshot per individuare le coordinate esatte del Tutorial / Missione 1 e di OK/Next, poi clicca!
     - **Briefing Terran**: leggi lo screenshot, annota testo/obiettivi visibili e coordinate di `Start` (~`520,400`), poi clicca `Start`!
     - **Campo di battaglia / Partita**: verifica con `shot` + `read` la schermata di gioco (mappa, unità Terran / SCV / Command Center, minimappa, HUD)! Prova opzionalmente un semplice comando di gioco (es. selezione / movimento) per documentare i controlli osservati.
   - Se incontri un nuovo STOP o SIGSEGV prima/dopo il briefing (es. callback `SNet`/`SDlg`, SCode, `0x004D4DCA` race font MPQ, API non implementata), diagnosticalo con GDB isolato (`handle SIGSEGV nostop pass` + `break cpu_signal`), applicalo se chiaro e sicuro (D19 intatto!), ricostruisci e riprendi!

3. **Memorizzazione completa in `qmem_store` + report**:
   - Registra in `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`):
     - Fix di `win32_shim_timers_pump` (e di eventuali altri bug risolti in `play4`).
     - Mappa completa delle schermate e coordinate `640x480` (dalla Campagna Terran → Selezione Tutorial/Missione 1 → Briefing → Start → Partita in-game) e modalità/controlli di gioco osservati.
   - Scrivi `reports/REPORT-x86-play4.md` e `reports/STATE-x86-play4.md` (max 70 righe).
   - **Chiudi sempre `play4` (`./tools/run-isolated.sh stop --id play4`)** prima di finire!
   - Se tocchi ~175k token, chiudi `play4`, salva qmem + report + STATE e chiudi il turno.

## Gate di accettazione (oggettivi)
1. Zero finestre visibili / zero audio reale (solo `tools/run-isolated.sh`).
2. Fix `win32_shim_timers_pump` applicato e verificato con `./tools/build.sh --arch x86 --skip-entry` + `./tests/x86_runtime_check/check.sh`.
3. Screenshot ispezionati con `read` in `build/qa/play-screenshots/play4-*.png` dalla Campagna Terran fino al Briefing e all'avvio del Tutorial / Prima Missione (o dimostrazione esatta dell'ultimo ostacolo se si ferma più avanti).
4. `qmem_store` aggiornato + `reports/REPORT-x86-play4.md` + `reports/STATE-x86-play4.md` completi + nessuna istanza `run-isolated.sh` attiva.
