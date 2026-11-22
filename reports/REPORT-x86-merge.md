# Report — scx86-merge

## Esito

Integrazione SHIM completata e build i686 riuscita. Il binario `build/starcraft-x86` è ELF32/EXEC Intel 80386; dispatch map: 102376 voci ordinate, zero collisioni. Il bridge annuncia 357 import, 20 UNK. I 20 `crt_unlifted_1504xxxx` restano dichiarazioni UNK: nessuno stub autonomo inventato. D19 non è stato indebolito.

## Modifiche

- `tools/build-shim-x86.sh`: aggiunto `win32_shell_dlg` (13 translation unit i686 totali).
- `runtime/shim-port/elf/win32_shim.c`: include e tabella di lookup delle sei API shell/dialog.
- `runtime/shim-port/elf/win32_shell_dlg.c`: controlli snprintf ora rifiutano anche troncamenti.
- `runtime/common/bridge_unk.def`: rimosse le API con implementazione concreta dal fallback UNK.
- `runtime/shim-port/bridge_shims.def`, `shim_rename.txt`: rigenerati; 333 SHIM / 20 UNK, collisioni globali zero.
- `tools/link-x86.sh`: rimozione dell'OUT prima del link, per evitare ETXTBSY sul vecchio inode.

Il mapping CSIDL resta esplicito e limitato ai valori supportati dalla funzione (`PERSONAL→Save`, `APPDATA→AppData`, `PROGRAMS→Programs`, `DESKTOP→.`); valori ignoti falliscono. `ShellExecuteA` resta bloccata e `FindExecutableA` fallisce in modo esplicito. EnumWindows su i386 conserva il comportamento prudenziale (FALSE senza guest callback trampoline); SEH termina fail-closed con exit 70. Nessun mapping CRT inventato.

## Verifiche

- `./tools/build-shim-x86.sh`: PASS, 13 shim TU, zero collisioni globali; diff generati puliti.
- `./tools/build.sh --arch x86 --skip-entry`: PASS.
- `./tests/x86_fastcc_check/check`: PASS.
- `./tests/x86_runtime_check/check.sh`: PASS.
- `readelf -h build/starcraft-x86`: ELF32 / EXEC / Intel 80386.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102376 voci, zero collisioni.
- `x86_win32_shim_test`: PASS (ricompilato con `--gc-sections`).
- `x86_snp_lan_test`: PASS, scambio UDP loopback reale.
- `x86_saveload_test`: linkato ed eseguito, PASS.
- `x86_crt_unlifted_test.c` e `storm_crt_unlifted.c`: compilazione i686 PASS; il test è guardia di compilazione senza corpi CRT.
- `gen_bridge_stubs.py --arch arm` generato in `/tmp/scx86-merge-arm`; sorgenti `runtime/arch/arm/*.S` compilati uno per uno con target armv6 in `/tmp`.

## Smoke isolato

Avviato esclusivamente con `./tools/run-isolated.sh start --id merge-verify --skip-intro 1 --smack 1`; log conferma dummy audio, display virtuale `:130` e bridge `357 import, 20 UNK dichiarati`. Screenshot verificati: menu principale e dialogo Select Game Type. Dopo la selezione di StarCraft Original lo screenshot successivo era nero; non è stato raggiunto il profilo, Terran Episode I, Briefing/Start, tips o partita. Nessun messaggio `STOP`/`ERROR` trovato nel log acquisito, ma lo smoke end-to-end richiesto **non è PASS**. Runner sempre arrestato con `stop --id merge-verify`; `game_alive=0`.

## Decisioni autonome

- Mantenuto il mapping CSIDL ristretto agli ID esplicitamente riconosciuti; ogni valore non supportato fallisce.
- Mantenuti callback EnumWindows i386 non eseguibili e SEH fail-closed, senza simulare successo o unwind.
- Smoke interrotto al nero dopo Original: nessuna ulteriore assunzione o modifica al runtime per aggirare il blocco.

## Limiti

Non verificati dialoghi UI reali (selezione file via variabili d'ambiente), tutti i path CSIDL nel gioco né callback EnumWindows su i386. Il test di partita non ha raggiunto Boot Camp. Nessun input desktop, audio reale, git o sessione concorrente è stato usato.
