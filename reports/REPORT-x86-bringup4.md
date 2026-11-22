# Report — x86 bring-up 4 (aggiornamento fix1)

## Esito corrente

Gate build/runtime alignment PASS; smoke avanzato fino all’apertura dei tre MPQ, ma il menu non è ancora raggiunto. `build/qa/run-x86-bringup4-fix1.log` termina con SIGSEGV durante codice Storm in fase di init (`entry VA 0x15040E07`, guest EIP `0x71000001`, host fault PC `0x1802DE07`, address 0). Non eseguita la prova SDL reale, subordinata al menu.

## Primo crash worker e causa provata

Nel log precedente `build/qa/gdb-x86-bringup4-fault.log`, il thread 2 aveva PC `0xF786A7EC` sul proprio stack host; i byte erano ASCII del buffer di log (`" 0x5e000003 0x000000fa\n..."`). `%esp=0xF786A7C8`, con `0x100AE500` (simbolo `loc_15016E23`) nello slot host a `0xF786A7E8`; EBP era anch’esso `0x100AE500`. Il guest segnava EIP `0x5E000004`, ESP `0x021FFAB0`. GDB aveva fermato `arm_thread_main`: job `{cpu=0x0E0020D0, entry=0x100AD480 / loc_15016C10, arg=0}`.

Il disassemblato di Storm a `0x15016E13` prepara la continuazione `0x100AE500` e chiama `WaitForSingleObject`. In `runtime/arm/bridge_arm.c` la dichiarazione originale di `WaitForSingleObject__c` era cdecl; l’implementazione shim, `runtime/shim-port/elf/win32_shim.c:1236`, è `S`/stdcall. Evidenza compilata: `WaitForSingleObject__c` termina con `ret 0x8` (`0x1803023e`), mentre il caller cdecl non compensava il pop del callee. Stack host ripulito due volte: causa della return corruption confermata.

## Fix1 già applicato

In `runtime/arm/bridge_arm.c` ho aggiunto `SHIM_STDCALL`, attivo solo su `__i386__`, alle dichiarazioni `WaitForSingleObject__c` e `DialogBoxParamA__c` (anche quest’ultima è dichiarata `S` nel suo shim). ARM resta senza attributo. Rebuild completo e `tests/x86_runtime_check/check.sh` PASS; `readelf` = ELF32/EXEC/Intel 80386; dispatch map = 102376 voci, 0 collisioni. L’assembly post-fix di `imp_WaitForSingleObject` ora compensa la pulizia stdcall con `sub esp,8`, ripristinando il frame.

## Smoke fix1 e nuovo blocco

`build/qa/run-x86-bringup4-fix1.log` (timeout 15, trace abilitati) mostra che la corruzione del worker è superata: apre `Stardat.mpq`, `patch_rt.mpq`, `BroodWar.mpq` e `BrooDat.mpq`. Poi `SetLastError__c` faulta a `0x1802DE07`, istruzione `mov fs:0x34,eax`, con fault address 0. Questa funzione proviene da `WaitForSingleObject__c` (`win32_shim.c:1280`) quando riceve un handle non gestito. Le ultime chiamate bridge mostrano argomenti `0x71000001, 0x65` e `0x71000001, 0x71`; la catena con `GetProcAddress` non è ancora chiarita. Il codice `Get/SetLastError` nello shim usa `%fs` su i386; il guest worker è creato dal percorso `arm_guest_thread_start`/`arm_thread_main`, che non installa un TLS Linux custom (e GDB aveva mostrato FS=0 sul thread 2). `teb_init()` del port inizializza il TEB emulato, non carica `%fs`. Prova forte che l’accesso host FS nel worker è invalido; il prossimo fix deve riusare il TEB emulato, non impostare un FS host.

Log diagnostici precedenti: `build/qa/gdb-x86-bringup4-thread.log`, `build/qa/gdb-x86-bringup4-fault.log`. Primo smoke: `build/qa/run-x86-bringup4.log`.

## Decisioni autonome

Applicato solo il fix ABI dimostrato per i due callback shim stdcall; nessun cambiamento a resolver o stub. Fermata l’attività sul nuovo fault per rispettare la soglia di contesto richiesta. Nessuna prova con SDL reale e nessun input desktop.

## Prossimo passo

Fix mirato: in `runtime/shim-port/elf/win32_shim.c`, nelle due funzioni `GetLastError` e `SetLastError` (righe circa 1054–1071), includere `__i386__` nel ramo già usato da `__arm__`, così che entrambi leggano/scrivano tramite `X86_ReadFsDword(0x34)` / `X86_WriteFsDword(0x34, ...)` sul TEB emulato anziché usare istruzioni `%fs` host. Verificare che `x86_helpers.h` sia incluso/prototipi disponibili nella build x86; lasciare il ramo x86 nativo di altri host non coinvolti invariato se la compilazione non lo supporta. Poi eseguire, in ordine, `TMPDIR=/dev/shm tools/build-shim-x86.sh && TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh`, `tests/x86_runtime_check/check.sh`, quindi ripetere lo smoke dummy 15 s salvando un nuovo log `build/qa/run-x86-bringup4-fix2.log`; seguire il successivo fault con GDB. Solo dopo menu dummy eseguire la prova SDL reale 5 s.

## Scostamento procedurale precedente

Nel primo turno era stato tentato per errore `git status --short` malgrado il divieto: comando fallito subito perché la directory non era un repository Git; nessuno stato modificato e nessun altro comando Git eseguito.
