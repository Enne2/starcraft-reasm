# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, `SnpQuery`/`SnpBind`/`SNet` o crash guest, usa `llvm-objdump -d` sui PE originali (`build/lift-full/StarCraft.orig.exe`, `storm.orig.dll`), `rg` mirato su `build/lift-full/*.llasm` (o IDA se disponibile; controlla `ida_list` prima).
2. **Autonomia**: hai piena autorizzazione a modificare `runtime/common/`, `runtime/arch/x86/`, `runtime/shim-port/elf/`, `tools/`, `build/qa/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Regola di isolamento OBBLIGATORIA**:
   - **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` o `DISPLAY=:0` o con audio reale!**
   - Usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (display Xvfb privato, `SDL_AUDIODRIVER=dummy`, `SC_CONTROL_FILE`, zero finestre sullo schermo dell'utente, zero suono).
   - Ricordati sempre di fare `./tools/run-isolated.sh stop --id <id>` prima di ricostruire o alla fine!
5. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non leggere per intero `build/lift-full/*.ll`, non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-play3`: Risolvere il crash dopo Terran/`SnpQuery` e avviare Tutorial / Prima Missione + `qmem_store`

Leggi prima:
- `reports/STATE-x86-play2.md` e `reports/REPORT-x86-play2.md`

Fatti già verificati (non ridimostrare):
- `cmd --id <id> '<comando intero>'` va **quotato** come singolo argomento (`'click 220 120'`).
- Sequenza già verificata fino alla Campagna (640×480):
  1. Single Player: `'click 220 120'`
  2. StarCraft Original: `'click 260 226'`
  3. Registry profilo `enne2ù`: `'click 100 109'` poi `'click 517 391'`
  4. Campagna `Terran Episode I`: `'click 320 250'`
- Dopo `'click 320 250'` (`Terran Episode I`): `[risorse] battle.snp`, `caps.dat`, `standard.snp`, `SnpQuery index>=2`, poi SIGSEGV host `pc=0x1`, `guest esp=0x08582010` (host dentro `.guest_exe_code`!), `[esp]=0x0530A156`, `eax=0x18029160`.

## Obiettivi e ordine di esecuzione

1. **Diagnosi immediata con GDB isolato del crash dopo `SnpQuery`**:
   - Controlla prima `llvm-nm -n build/starcraft-x86` attorno a `0x18029160` e `0x08582010`!
   - Controlla anche `runtime/shim-port/elf/snp_elf.c`, `win32_shim.c` (hook `snp` / `GetProcAddress` / `SnpQuery` / `SnpBind` / `arm_com_thunk` / `spiInitialize`!), e `runtime/common/` (`guest_call_arm.c` / `bridge_arm.c`).
   - Avvia un'istanza isolata `./tools/run-isolated.sh start --id play3 --skip-intro 1 --smack 1`, attacca GDB sul `game_pid` con:
     `handle SIGSEGV nostop pass` + `break cpu_signal` + `break SnpQuery` / `break SnpBind` (se presenti!), poi invia la sequenza fino a `'click 320 250'` (Terran).
   - Stampa `bt`, `*(_cpu*)g_cpu_for_signal` (o il simbolo corrente di `_cpu`), la pila emulata, e il ring degli archi per capire **esattamente chi ha scritto `0x08582010` in `cpu->_esp` e perché PC è saltato a `0x1`**!

2. **Fix, rebuild e avanzamento completo fino a Tutorial / Prima Missione**:
   - Ferma `play3`, applica il fix pulito (D19 intatto!), esegui `./tools/build.sh --arch x86 --skip-entry` e `./tests/x86_runtime_check/check.sh`.
   - Riprendi la navigazione interattiva con `./tools/run-isolated.sh` (`shot` + `read` + `cmd` quotato):
     - **Terran Episode I** (`'click 320 250'`) → schermata selezione missione / **Boot Camp (Tutorial)** o **Mission 1 (Wasteland)** → **Briefing Terran** → **Start** → **ingresso in partita / campo di battaglia**!
   - Diagnostica e risolvi eventuali ulteriori blocchi lungo il percorso (es. callback `SNet`/`spi*`, `SetTimer`/`KillTimer`, SCode, o eventuale race font `0x004D4DCA` se si presenta!).
   - Salva gli screenshot ispezionati delle nuove schermate in `build/qa/play-screenshots/play3-*.png` (sempre copiandoli prima di `stop`, e verificando con `read`!).

3. **Memorizzazione in `qmem_store` e report**:
   - Salva in `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`):
     - Causa radice e fix del crash `SnpQuery` / Terran (`0x08582010`/`0x1`) e di eventuali altri blocchi risolti.
     - Coordinate verificate `640x480` per tutte le nuove schermate (Campagna Terran, Selezione Tutorial/Missione, Briefing Start, HUD in-game) e modalità/controlli di gioco osservati.
   - Scrivi `reports/REPORT-x86-play3.md` e `reports/STATE-x86-play3.md` (max 70 righe).
   - Chiudi sempre l'istanza con `./tools/run-isolated.sh stop --id play3`!
   - Se raggiungi ~175k token, ferma l'iterazione, chiudi `play3`, salva qmem + report + STATE e chiudi il turno.

## Gate di accettazione (oggettivi)
1. Zero finestre visibili / zero audio reale (solo `tools/run-isolated.sh`).
2. Causa radice e fix del crash post-`SnpQuery` dimostrati e verificati con `./tools/build.sh --arch x86 --skip-entry` + `./tests/x86_runtime_check/check.sh`.
3. Screenshot verificati con `read` in `build/qa/play-screenshots/play3-*.png` fino a Tutorial / Prima Missione avviata (o dimostrazione esatta dell'eventuale ultimo blocco dopo aver superato `SnpQuery` e Terran).
4. `qmem_store` aggiornato + `reports/REPORT-x86-play3.md` + `reports/STATE-x86-play3.md` completi + nessuna istanza `run-isolated.sh` attiva.
