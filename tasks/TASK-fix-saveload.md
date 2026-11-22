# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` / `storm.orig.dll` se serve capire il flusso di Save Mission.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai simulare un salvataggio finto; trova perché la UI `Save Mission` non scrive/chiude e risolvi la vera causa (es. `Save/` directory, `CreateDirectoryA`, `CreateFileA`, `RegQueryValueExA`, `FindFirstFileA`, coordinate pulsante OK!).
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `./tools/run-isolated.sh` con `SC_BIN` privato (es. `build/qa/fix-saveload/starcraft-x86-save`) se modifichi file `.c`, e chiudi sempre con `stop`.
5. **Divieti**: NON toccare `smack_elf.c`, `storm_sdraw_arm.c`, `build/starcraft-x86`, `build/obj-x86/`. Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-saveload-live`: Diagnosi & Fix Save/Load Mission reale in Boot Camp

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/win32_shim.c`
- `runtime/shim-port/elf/win32_shell_dlg.c`
- `runtime/shim-port/elf/win32_shell_dlg.h`
- `build/qa/fix-saveload/`
- `reports/REPORT-fix-saveload.md`
- `reports/STATE-fix-saveload.md` (max 70 righe)

## Obiettivi
1. **Ispeziona gli screenshot esistenti e avvia traccia `REASM_TRACE=1` sul dialogo Save**:
   - Leggi `build/qa/close-play/12-save-dialog.png`, `13-save-name.png`, `14-after-save.png`, `17-load-dialog.png` con `read` per verificare le coordinate esatte della casella di testo, del bottone `OK` / `Save`, e se manca la directory `Save/` (nota: `build/asset-sandbox/Save` NON esisteva!).
   - Avvia un'istanza isolata con `--trace 1` (`./tools/run-isolated.sh start --id fix-save --skip-intro 1 --smack 1`), entra in Boot Camp (`538 390` → `197 262`), apri `MENU` → `Save Mission`, digita `testsave`, clicca il vero bottone `OK`/`Save` dello screenshot (controlla nello screenshot `13-save-name.png` dov'è!) e guarda `build/isolated/fix-save/game.log` per vedere ESATTAMENTE quali chiamate Win32 (`CreateFileA`, `GetFileAttributesA`, `CreateDirectoryA`, `FindFirstFileA`, `RegQueryValueExA`, `SetEndOfFile`, ecc.) avvengono o falliscono!
2. **Applica il fix dimostrato**:
   - Se manca qualcosa in `win32_shim.c` / `win32_shell_dlg.c` (es. path mapping `Save\\...`, `CreateDirectoryA`, `GetFileAttributesA`, `GetDiskFreeSpaceA`, ecc.) o `tools/run-isolated.sh` non crea `Save/`, sistemalo pulitamente!
   - Compila `win32_shim.o` + `win32_shell_dlg.o` con `--redefine-syms` in `build/qa/fix-saveload/`, linka `build/qa/fix-saveload/starcraft-x86-save` seguendo esattamente `tools/link-x86.sh` (usando gli altri `.o` di `build/obj-x86/` in sola lettura!).
3. **Verifica Save + Load end-to-end**:
   - Con `SC_BIN="$PWD/build/qa/fix-saveload/starcraft-x86-save" ./tools/run-isolated.sh start --id fix-save --skip-intro 1 --smack 1`:
     - Salva la partita `testsave` in Boot Camp -> verifica che esista il file `.snx`/`.sav` nella sandbox (`build/isolated/fix-save/sandbox/Save/` o `characters/`!) e che il dialogo si chiuda.
     - Riapri `MENU` → `Load Mission` -> verifica con `shot` + `read` che `testsave` appaia in lista -> selezionalo e caricalo -> verifica che la partita riprenda regolarmente!
4. **Chiusura, report e `qmem_store`**:
   - Ferma `fix-save` (`stop`), scrivi `reports/REPORT-fix-saveload.md` + `reports/STATE-fix-saveload.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
