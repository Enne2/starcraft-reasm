# Report — scx86-close-build

## Esito

- Build completo `./tools/build.sh --arch x86`: **PASS** (exit 0), con lifting reale delle entry on-exit; D19 invariato.
- `tests/x86_gfx_test.c`: COM vtable cast e dichiarazioni GDI aggiornate a `stdcall`; descrittore DDSURFACEDESC corretto per WIDTH/HEIGHT/PIXELFORMAT. `x86_gfx_test`: **PASS** su clipper Get/Set/change, SetClipper/GetClipper, pixel Blt/BltFast clippati e SetDIBits 8/24/32.
- Il test ha mostrato che il clipping di `com_ddraw.c` era corretto; il primo pixel-test falliva perché il test creava superfici 640×480 invece che 4×4. `gdi_elf.c` non richiedeva fix; le aspettative palette sono state riallineate alla quantizzazione RGB→palette implementata.

## On-exit / lifting

- Oracolo PE `build/lift-full/StarCraft.orig.exe` via `llvm-objdump`: `0x004FDE60` contiene `push 0x006D62B8; call [0x004FE204]; ret` (0x4FDE60–0x4FDE6B). IDA sulla copia in `/tmp` ha confermato il `call DeleteCriticalSection`; xref al callback da `0x004FD3AB`.
- La entry mancava da `code_entry_points.sci`: aggiunta e realmente riliftata con `lift-full.sh srw exe` + `ir exe`. Durante il drain sono emerse ulteriori callback non presenti nella dispatch; promosse allo stesso modo le entry effettivamente osservate: `0x004FDDF0`, `0x004FDDE0`, `0x004FD770`, `0x004FD700`, `0x004FD6F0`, `0x004FD670`.
- La build finale riporta 102392 entry dispatch, ordinate/univoche, zero collisioni. Nessun target fittizio, modifica del resolver o allentamento D19.

## Verifiche finali

- **PASS** `tests/x86_fastcc_check/check`
- **PASS** `tests/x86_runtime_check/check.sh`
- **PASS** `x86_win32_shim_test`, `x86_saveload_test`, `x86_snp_lan_test`, `x86_dsound_test`, `x86_gfx_test`
- **PASS** `readelf`: ELF32 / EXEC / Intel 80386
- **PASS** `dispatch-ir.py --dump-map`: 102392 voci; zero collisioni

## Menu → Exit isolato

Avvio via `./tools/run-isolated.sh start --id close-exit --skip-intro 1 --smack 1`; screenshot 640×480 verificato, click sul pulsante Exit a `(510,328)` tramite il control file del runner (Xvfb privato, audio dummy). Nel log finale **non** compare `[arm-dispatch] STOP`; il processo non era più vivo al controllo e `stop --id close-exit` ha chiuso anche Xvfb. Il log termina con `[reasm-arm] GetModuleHandleA di un modulo non presente: mscoree.dll`. Il runner non conserva il codice d’uscita e non è stato acquisito in altro modo: **exit 0 non verificato** (non dichiarato PASS).

## File e decisioni autonome

Modificati: `tests/x86_gfx_test.c`, `build/lift-full/code_entry_points.sci`; rigenerati `build/lift-full/StarCraft.ll`, `entry-x86` e `build/starcraft-x86`. Nessuna modifica a `vendor/`; nessun comando Git; nessun audio reale o display `:0`.

## Decisioni autonome

Ho mantenuto il fail-closed e promosso solo gli indirizzi di codice realmente osservati nel drain. Per l’esito Exit riporto precisamente l’evidenza disponibile: assenza di STOP e processo terminato, ma codice di uscita non catturato; quindi non attribuisco `exit 0`.
