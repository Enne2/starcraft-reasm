# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` / `llvm-readobj` su `build/lift-full/StarCraft.orig.exe` e `build/lift-full/storm.orig.dll` (e `rg` mirato su `build/lift-full/*.llasm`) per verificare call-site, struct e arità stdcall di ogni API!
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai stub silenziosi che mentono sul risultato; comportamento fedele a Win32/StarCraft o errore dichiarato.
4. **Regola di isolamento**: MAI finestre visibili né audio reale; usa solo test unitari i686 o `./tools/run-isolated.sh` con `SC_BIN` privato.
5. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (In particolare NON modificare `win32_shim.c`, `bridge_shims.def`, `shim_rename.txt`, `bridge_unk.def`, `build/starcraft-x86` o `build/obj-x86/` — li unirà l'agente di integrazione finale.)
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-saveload`: API File Dialog, Shell e Percorsi (`GetSaveFileNameA`, `ShellExecuteA`, `FindExecutableA`, `SHGet*`)

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/win32_shell_dlg.c` (nuovo)
- `runtime/shim-port/elf/win32_shell_dlg.h` (nuovo)
- `tests/x86_saveload_test.c` (nuovo)
- `build/qa/saveload/` (output compilazione/test privati)
- `reports/REPORT-saveload.md`
- `reports/STATE-saveload.md` (max 70 righe)

## Obiettivi
1. **Ispeziona i call-site originali** in `StarCraft.orig.exe` (`rg` su `StarCraft.llasm`, `llvm-objdump -d`, `externals-StarCraft.txt`) per tutte le 5 API oggi in `bridge_unk.def`:
   - `GetSaveFileNameA` (OPENFILENAMEA 32-bit layout, flags, `lpstrFile`, `nMaxFile`, `lpstrInitialDir`, `lpstrDefExt`)
   - `ShellExecuteA`
   - `FindExecutableA`
   - `SHGetPathFromIDListA`
   - `SHGetSpecialFolderLocation` (CSIDL constants: `CSIDL_PERSONAL`, `CSIDL_APPDATA`, `CSIDL_PROGRAMS`, ecc.)
   - Verifica anche se StarCraft usa `GetOpenFileNameA` o altre API di salvataggio/caricamento o percorsi `Save/` / `Maps/replays/`.
2. **Implementa in `win32_shell_dlg.c` + `win32_shell_dlg.h`** (stile freestanding compatibile con `tools/build-shim-x86.sh`, `REASM_STDCALL` / `S` macro come `win32_shim.c`):
   - `SHGetSpecialFolderLocation` + `SHGetPathFromIDListA`: restituiscono PIDL sintetici validi mappati su path Windows reali dentro la sandbox (`C:\StarCraft\Save`, `C:\StarCraft\Maps`, ecc. corrispondenti alle directory locali reali, creando le directory se mancanti).
   - `GetSaveFileNameA` / `GetOpenFileNameA`: implementazione deterministica e sicura:
     - supporta `SC_SAVE_FILE` / `SC_LOAD_FILE` per test automatici, oppure fallback non bloccante / selezione path coerente con `lpstrInitialDir` / `lpstrFile` / `lpstrDefExt` (oppure verifica se StarCraft chiama `GetSaveFileNameA` solo per *Save Replay* / *Campaign Editor* e usa il proprio dialogo interno per *Save Game*!).
   - `ShellExecuteA` / `FindExecutableA`: gestione fail-safe (log esplicito + no-op controllato o `SE_ERR_FNF` / `>32` documentato, mai exec incontrollato di binari Windows).
   - Esporta le definizioni compatibili con `gen_shims_bridge.py` (`SHIM(...)` exact signature).
3. **Compila e testa (`clang --target=i686-unknown-linux-gnu -m32 -fno-pie`)**:
   - Scrivi `tests/x86_saveload_test.c` che verifica `SHGetSpecialFolderLocation` -> `SHGetPathFromIDListA`, `GetSaveFileNameA` (`OPENFILENAMEA` 32-bit offsets), `ShellExecuteA`, `FindExecutableA`, e l'arità `stdcall` (`ret N` verificato con `llvm-objdump -d`).
4. **Report, STATE e `qmem_store`**:
   - Documenta esattamente in `reports/REPORT-saveload.md` quali call-site di `StarCraft.orig.exe` usano queste 5 API, l'elenco esatto di righe `SHIM(...)` / `shim_rename.txt` da aggiungere nell'integrazione finale, e salva un record sintetico in `qmem_store` (`project_id="starcraft-reasm-generic"`).
