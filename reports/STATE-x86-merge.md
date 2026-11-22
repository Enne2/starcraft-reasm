# STATE — scx86-merge

- Integrazione shell/dialog + winproc nei manifest e lookup completata; `win32_shell_dlg` incluso nel build shim.
- `bridge_shims.def`/`shim_rename.txt` rigenerati; zero collisioni. Bridge: 357 import, 20 UNK; i 20 `crt_unlifted_1504xxxx` invariati.
- Build x86, fastcc/runtime check, dispatch map e ELF32/EXEC/i386: PASS; 102376 dispatch entry, zero collisioni.
- Test win32 shim, UDP SNP e saveload: PASS; test CRT compilato (non semantico).
- Generator ARM e tutti gli assembly ARM compilati in `/tmp`: PASS.
- Smoke `merge-verify`: menu e Select Game Type verificati; dopo Original schermata nera, nessun Boot Camp/partita. Runner fermato, `game_alive=0`; smoke end-to-end non PASS.
- File dettagliato: `reports/REPORT-x86-merge.md`.
- D19 invariato; nessuno stub CRT autonomo aggiunto; EnumWindows i386 e SEH restano fail-closed.
