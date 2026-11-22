# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Autonomia**: hai piena autorizzazione a modificare `runtime/`, `tools/`, `tests/`, `build/`. Non usare `ask_user`, non chiudere turni chiedendo conferme.
2. **Fail-closed (D19)**: mai indebolire `reasm_indirect_resolve` o inventare stub per i 20 `crt_unlifted_1504xxxx` (non sono funzioni autonome).
3. **Regola di isolamento OBBLIGATORIA**:
   - **NON toccare/uccidere l'eventuale processo utente vivo su Wayland (`PID 2386043`)** e **NON lanciare MAI `build/starcraft-x86` su `WAYLAND_DISPLAY` / `DISPLAY=:0` / audio reale**!
   - Prima di scrivere `build/starcraft-x86` al link, verifica che `tools/link-x86.sh` faccia `rm -f "$OUT"` (o link atomico via tmp + `mv -f`) così da non incorrere in `ETXTBSY` se l'utente ha un'istanza aperta sul vecchio inode!
   - Per i test di gioco usa ESCLUSIVAMENTE `./tools/run-isolated.sh` (e fai sempre `stop` alla fine!).
4. **Divieti**: niente sotto-agenti annidati, niente `git`, niente input desktop (`ydotool`/`kdotool`), non toccare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arch/arm/`.

---

# Missione `scx86-merge`: Integrazione finale dei 6 rami paralleli + Suite Test + Smoke Boot Camp su `build/starcraft-x86`

Leggi prima i 6 `STATE-*.md`:
- `reports/STATE-saveload.md` (e `reports/REPORT-saveload.md` sezione "Integrazione finale richiesta" e caveat!)
- `reports/STATE-winproc.md` (e `reports/REPORT-winproc.md`)
- `reports/STATE-net.md`
- `reports/STATE-gfx.md`
- `reports/STATE-dsound.md`
- `reports/STATE-crt-unlifted.md`

## Obiettivi e ordine di esecuzione

1. **Integra `win32_shell_dlg.c` e i nuovi simboli nel build shim (`tools/build-shim-x86.sh`, `win32_shim.c`, manifest)**:
   - Controlla i caveat in `reports/REPORT-saveload.md` (`snprintf` truncation check, `CSIDL_*` safe mapping).
   - Aggiungi `win32_shell_dlg` alla lista `shim_files` in `tools/build-shim-x86.sh` (e l'include/lookup in `win32_shim.c` per `GetSaveFileNameA`, `GetOpenFileNameA`, `SHGetSpecialFolderLocation`, `SHGetPathFromIDListA`, `ShellExecuteA`, `FindExecutableA` se non già esposti — verifica come `gen_shims_bridge.py` + `win32_shim_lookup` risolvono i nuovi SHIM!).
   - Verifica `runtime/common/bridge_unk.def`, `runtime/shim-port/bridge_shims.def`, `runtime/shim-port/shim_rename.txt`:
     - Rimuovi da `bridge_unk.def` le API che ora hanno implementazione reale (`GetSaveFileNameA`, `ShellExecuteA`, `FindExecutableA`, `SHGetPathFromIDListA`, `SHGetSpecialFolderLocation`, `EnumWindows`, `FindWindowA`, `FindWindowExA`, `GetLastActivePopup`, `WaitForInputIdle`, `RaiseException`, `RtlUnwind`) — lascia intatti in `bridge_unk.def` i 20 `crt_unlifted_1504xxxx` (come provato da `REPORT-crt-unlifted.md`!).
     - Aggiorna/verifica `bridge_shims.def` e `shim_rename.txt` tramite `tools/build-shim-x86.sh` (mantenendo `diff` pulito e zero collisioni globali).
   - Verifica `EnumWindows` su `__i386__` in `win32_shim.c` se `arm_guest_calln` (in `runtime/common/guest_call_arm.c`) è già disponibile su `__i386__`, oppure mantieni la scelta conservativa documentata in `REPORT-winproc.md`.
   - Assicurati che `tools/link-x86.sh` rimuova/rimpiazzi atomicamente `build/starcraft-x86` (`rm -f "$OUT"` prima di `ld.lld` o link su file temporaneo + `mv -f`) per evitare `ETXTBSY`.

2. **Compilazione completa e verifica di tutti i test unitari**:
   - Esegui `./tools/build.sh --arch x86 --skip-entry` (exit `0`).
   - Esegui:
     - `./tests/x86_fastcc_check/check`
     - `./tests/x86_runtime_check/check.sh`
     - `LC_ALL=C readelf -h build/starcraft-x86` (`ELF32 / EXEC / Intel 80386`)
     - `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` (`0` collisioni)
   - Ricompila/esegui tutti i 4 test unitari in `build/qa/`:
     - `tests/x86_win32_shim_test.c`
     - `tests/x86_snp_lan_test.c`
     - `tests/x86_saveload_test.c` (linka ed esegui davvero `build/qa/saveload/x86_saveload_test`!)
     - `tests/x86_crt_unlifted_test.c` (compila & verifica in `build/qa/crt/`)
   - Verifica ARM check: `python3 runtime/common/gen_bridge_stubs.py --arch arm ...` e `clang --target=armv6-unknown-linux-gnueabihf -c runtime/arch/arm/*.S` in `/tmp`!

3. **Smoke end-to-end isolato sul binario integrato `build/starcraft-x86` (`--id merge-verify`)**:
   - Esegui `./tools/run-isolated.sh start --id merge-verify --skip-intro 1 --smack 1`.
   - Attraversa Menu → Single Player → Original → Profilo `enne2ù` → Terran Episode I → Story → Boot Camp Briefing → **Start** → Tips OK → **In-game** (SCV select + Supply Depot / move) e verifica con `shot` + `read`!
   - Verifica che nel log di gioco compaiano ZERO errori/STOP e il nuovo numero aggiornato di stub bridge!
   - Chiudi sempre con `./tools/run-isolated.sh stop --id merge-verify`!

4. **Report, STATE e `qmem_store`**:
   - Salva il riepilogo finale integrato in `qmem_store` (`project_id="starcraft-reasm-generic"`, `scope="project"`).
   - Scrivi `reports/REPORT-x86-merge.md` e `reports/STATE-x86-merge.md` (max 70 righe).
