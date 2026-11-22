# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` e `storm.orig.dll` (e `rg` mirato su `build/lift-full/*.llasm`) per verificare `EnumWindows`, `Save`/`Load` e registrazione SEH `fs:[0]`.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai simulare eccezioni gestite senza vero unwind; mantieni controlli rigorosi.
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `SC_BIN="$PWD/build/qa/gap-win32/starcraft-x86-win32" ./tools/run-isolated.sh` e chiudi sempre con `stop`.
5. **Divieti**: NON toccare file fuori proprietà (`host_elf.c`, `snp_elf.c`, `com_*.c`, `build/starcraft-x86`, `build/obj-x86/`). Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gap-win32`: `EnumWindows` callback i386, verifica Save/Load reale in Boot Camp & audit `fs:[0]` SEH

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/win32_shell_dlg.c`
- `runtime/shim-port/elf/win32_shell_dlg.h`
- `runtime/shim-port/elf/win32_shim.c`
- `tests/x86_saveload_test.c`
- `tests/x86_win32_shim_test.c`
- `build/qa/gap-win32/` (oggetti compilati, binario privato `starcraft-x86-win32`, test binaries)
- `reports/REPORT-gap-win32.md`
- `reports/STATE-gap-win32.md` (max 70 righe)

## Obiettivi
1. **`EnumWindows` su `__i386__` (`win32_shim.c`)**:
   - Nota che `arm_guest_call4` / `arm_guest_calln` in `runtime/common/guest_call_arm.c` sono già compilati e funzionanti anche su `__i386__` (come già usato in `win32_ui.c` e `win32_shim_timers_pump`)!
   - Verifica `runtime/common/guest_call_arm.c` (solo lettura!) per confermare la firma esatta di `arm_guest_calln` su `__i386__`.
   - Abilita `EnumWindows` in `win32_shim.c` ad invocare `WNDENUMPROC(hwnd, lparam)` tramite `arm_guest_calln` anche su `__i386__` (con controllo `proc` valido), e aggiungi un test dedicato in `tests/x86_win32_shim_test.c`!
2. **Verifica `Save`/`Load` reale in Boot Camp (`win32_shell_dlg.c` + prova isolata)**:
   - Compila i tuoi `.c` + linka un binario privato `build/qa/gap-win32/starcraft-x86-win32` (usando in sola lettura gli altri `.o` di `build/obj-x86/`).
   - Avvia `SC_BIN="$PWD/build/qa/gap-win32/starcraft-x86-win32" ./tools/run-isolated.sh start --id gap-win32 --skip-intro 1 --smack 1` ed entra in Boot Camp.
   - Apri il menu in-game (`MENU`), clicca **Save Mission** (e verifica **Load Mission**!), ispeziona gli screenshot con `read`: verifica se StarCraft usa il proprio dialogo interno Storm o `GetSaveFileNameA` (e verifica che il file `.sav` venga scritto nella cartella `Save/` della sandbox privata e ricaricato con successo!).
   - Se `GetSaveFileNameA`/`GetOpenFileNameA` viene invocato da un dialogo e `SC_SAVE_FILE`/`SC_LOAD_FILE` non è impostato, aggiungi anche supporto pulito a `SC_SAVE_AUTO=1` (es. `Save/autosave.sav` o `Maps/replays/auto.rep` non-bloccante) in `win32_shell_dlg.c`, oltre al test unitario `tests/x86_saveload_test.c`!
3. **Audit registrazione SEH `fs:[0]` (`RaiseException` / `RtlUnwind`)**:
   - Verifica in `StarCraft.orig.exe` / `storm.orig.dll` e `win32_shim.c` se `RaiseException` / `RtlUnwind` possono essere estese per ispezionare e loggare la catena `fs:[0]` del TEB emulato (`X86_ReadFsDword(0)`) prima di terminare fail-closed (`exit 70`), e aggiungi test su entrambi.
4. **Gate, report e `qmem_store`**:
   - Esegui `x86_win32_shim_test` e `x86_saveload_test` (entrambi **PASS**), chiudi `gap-win32` (`stop`), scrivi `reports/REPORT-gap-win32.md` + `reports/STATE-gap-win32.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
