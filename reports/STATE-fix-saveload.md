# STATE — scx86-saveload-live

- Esito: PASS per singolo-player Boot Camp `.sng`; build privata `build/qa/fix-saveload/starcraft-x86-save` (ELF32/i386).
- Causa: shim mancava `CREATE_NEW` (`O_CREAT|O_EXCL`) e `reasm_open_ci` rifiutava la foglia nuova durante la risoluzione case-insensitive. Trace pre-fix: `Save/qasave/testsave.sng`, `ENOENT`, poi SIGSEGV in `fwrite` guest `0x0041182A`.
- Fix: `runtime/shim-port/elf/win32_shim.c` gestisce `CREATE_NEW` e la foglia finale mancante; `tools/run-isolated.sh` crea `Save/` e una cartella per ogni profilo `.spc`.
- Verifica reale: `Save/qasave/testsave.sng` scritto dal gioco, 104598 byte; Save dialog chiuso; Load list mostra `testsave`; file riaperto dal gioco e gameplay visibile (`56-load-dialog.png`, `57-after-load.png`).
- Unit test esistente `build/qa/saveload/x86_saveload_test`: PASS.
- Isolamento fermato e verificato: `fix-save` game/Xvfb non vivi.
- Limiti: una sessione Boot Camp; multiplayer `.snx`, replay, altri profili e overwrite non verificati. `win32_shell_dlg.c/.h` non modificati.
- Evidenze: `build/isolated/fix-save/game.log`, screenshot `41`, `43`, `55`–`57`; report `reports/REPORT-fix-saveload.md`.
- Decisioni autonome: fix circoscritto ai path e alla creazione reale; nessun save finto, nessun audio/display desktop.
