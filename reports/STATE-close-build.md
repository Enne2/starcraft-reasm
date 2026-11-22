# STATE — scx86-close-build

- **Build**: `./tools/build.sh --arch x86` PASS, exit 0.
- **GFX**: `tests/x86_gfx_test.c` usa stdcall per COM e GDI; PASS per clipper, pixel Blt/BltFast e conversioni SetDIBits 8/24/32. Bug di clipping non rilevato; il test iniziale aveva dimensioni non impostate nel descrittore.
- **On-exit**: promosse con lifting reale in `code_entry_points.sci` `0x004FDE60`, `0x004FDDF0`, `0x004FDDE0`, `0x004FD770`, `0x004FD700`, `0x004FD6F0`, `0x004FD670`. D19/resolver invariati; nessun target fittizio.
- **Test PASS**: fastcc check; runtime check; win32 shim; saveload; SNP LAN; DirectSound; GFX.
- **Formato/map**: ELF32 / EXEC / Intel 80386; dispatch 102392 voci ordinate/univoche, zero collisioni.
- **Exit isolato**: menu raggiunto su Xvfb privato con audio dummy; click Exit `(510,328)`; nessun `[arm-dispatch] STOP`; processo non più vivo. Log finale: `GetModuleHandleA di un modulo non presente: mscoree.dll`.
- **Limite esito Exit**: `run-isolated.sh` non espone il wait status; `exit 0` non verificato. Non dichiarare chiusura pulita PASS senza catturare il codice.
- **Pulizia**: `./tools/run-isolated.sh stop --id close-exit`; game e Xvfb spenti.
- **File**: `tests/x86_gfx_test.c`, `build/lift-full/code_entry_points.sci`; report completo `reports/REPORT-close-build.md`.

## Decisioni autonome

Fail-closed conservato; nessuna scorciatoia di dispatch. L’esito di Exit è riportato con il limite misurato, senza inferire un codice 0.
