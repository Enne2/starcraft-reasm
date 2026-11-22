# ARM guest-ABI: COM e Smack

## Stato iniziale

Autorizzazione esplicita ricevuta per modificare esclusivamente `com_ddraw.c`, `com_dsound.c`, `smack_elf.c` e, se necessario, aggiungere `runtime/arm/com_bridge_arm.{c,S}` e il supporto `--runtime-only` di `tools/build-entry-obj.sh`. Non saranno modificati `win32_shim.c`, `win32_ui.c`, `dlg_elf.c` né guest.

La prova precedente (Qmem, episodio del 20/11/2026) registra `GetProcAddress("DirectDrawCreate")` al thunk guest-ABI `0x189CCDF4`, chiamata `DirectDrawCreate` con `S_OK`, poi exit 3 per ritorno senza consumo del sentinella. Ispezione locale: `runtime/arm/bridge_arm.c:368-404` poppa il return guest, invoca la funzione C tramite caller per arità, poppa `pops` argomenti e restituisce il return guest al gate assembly; `runtime/arm/indirect_dispatch.S` conferma che la continuazione guest deve essere raggiunta con `bx`, non `bl`.

`com_ddraw.c` assegna direttamente funzioni C alle quattro vtable; `com_dsound.c` assegna direttamente funzioni C a `ds_vt`/`dsb_vt`; `smack_elf.c:570-578` restituisce direttamente `e->fn`. `win32_dsound_get_proc` restituisce inoltre direttamente `DirectSoundCreate` e `DirectSoundCreate8`. Sono puntatori AAPCS, non thunk per gli argomenti sullo stack guest.

## Piano in corso

Ponte ARM con registrazione di funzione C + `nargs` + `pops`, piccoli ingressi assembly contenuti nel `.text`, dispatch al caller per arità generato (`arm_shim_caller`) e prosecuzione guest via `reasm_indirect`. Vtable COM, API DSound e procedure Smack dovranno esporre esclusivamente tali thunk sul target ARM; sul target non ARM resta la semantica attuale. Verifica puntuale delle arità e delle convenzioni prima del test.

## Implementazione

Aggiunti `runtime/arm/com_bridge_arm.c` e `runtime/arm/com_bridge_arm.S`. Il C registra (deduplicando funzione/arità/convenzione) gli entrypoint nativi, usa i caller ARM per arità già generati in `shim_calls.c`, salva `_eax`, consuma l'indirizzo di ritorno guest e gli argomenti callee-pop. L'assembly espone 256 thunk minimi in `.text`, chiama il dispatcher e prosegue con `reasm_indirect_resolve`/`bx`; nessun ritorno AAPCS diretto al guest. Il gate accetta `nargs` fino a 14, caller supportati dal generatore incluso 1..12 e 14; le funzioni COM/Smack qui usate hanno al massimo 8.

`com_ddraw.c`: sostituiti sul solo ARM tutti gli slot assegnati delle quattro vtable (arità per slot, `this` incluso). `com_dsound.c`: adattati tutti gli slot IDirectSound/Buffer, inclusi gli slot unsupported che fermano il gioco, e `DirectSoundCreate`/`DirectSoundCreate8` restituiti da `win32_dsound_get_proc`. `smack_elf.c`: i 13 nomi decorati `_Name@N` sono stdcall; `N/4` argomenti e `pops=nargs`, confermati anche dalle firme C. `DirectDrawCreate`/`DirectDrawCreateClipper` sono già risolti dal thunk statico `arm_bridge_lookup_thunk` introdotto dal percorso 0003 (non è stato toccato `win32_shim.c` o `win32_ui.c`). `tools/build-entry-obj.sh` compila il nuovo C nella sezione runtime-only e l'assembly come oggetto runtime.

Arie COM verificate dalle firme implementate e assegnate: DirectDraw `{3,1,1,1,4,5,4,3,5,5,1,3,2,3,2,2,2,2,2,1,3,6,3}`; Surface (33 slot assegnati) `{3,1,1,2,2,6,4,6,3,3,4,3,3,2,2,2,3,2,2,3,2,2,2,3,1,5,2,1,2,3,3,2,2}`; Palette `{3,1,1,2,5,4,5}`; Clipper `{3,1,1,4,2,3,2,3,3}`. DirectSound `{3,1,1,4,2,3,3,1,2,2,2,2}`; SoundBuffer `{3,1,1,2,3,4,2,2,2,2,3,8,4,2,2,2,2,2,1,5,1,3,3,5}`. Tutti questi metodi sono stdcall; nessuna funzione cdecl esposta nelle tabelle verificate.

## Build e verifiche

Ricompilati soltanto `build/runtime-arm/shim-port/obj/com_ddraw.o`, `com_dsound.o`, `smack_elf.o` con clang target `armv6-unknown-linux-gnueabihf`, `-march=armv6zk -mfpu=vfp -mfloat-abi=hard`, sysroot ARM, SDL2 overlay, include shim/runtime ELF e `-DSC_ELF_REAL_GDI=1` come richiesto (il define non cambia questi tre translation unit). Oggetti nuovi ARM aggiunti in `build/lift-full/entry/runtime/` dal build script.

Comando link richiesto: `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-com` — PASS, binario `build/starcraft-armv6-com`, SHA-256 `4b5cc9f9ec3690ef8947d1bb8af16abdb7be8b66b3c4ae62a8b93decfa0ab722`. `llvm-nm`/`llvm-readelf` confermano `arm_com_bridge_common` a `0x189ccfe0`, thunk 0 a `0x189cd01c`, thunk 255 a `0x189cd814`, tutti simboli di funzione in `.text`; il dispatcher e la tabella sono linkati.

`make check`: PASS (inclusi ARMv6 sotto QEMU, PE ordinal gate/Wine x86, e test ARM1176 CPU/guest-stack bridge).

Smoke richiesto, da `build/asset-sandbox`, `ulimit -c 0`, `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 20 qemu-arm-static -cpu arm1176 -L ../../build/sysroot ../../build/starcraft-armv6-com`: **exit 0**. Log integrale: `build/qa/run-com.log`. Estratto verbatim:

```text
[com] SetDisplayMode 0x028001E00x00000008
[host] init: video driver = dummy
[host] finestra creata
[com] CreateSurface caps=0x00000200 size=0x028001E0 PRIMARY
[dsound] LoadLibraryA("dsound.dll") -> modulo nativo (handle 0x7000DD01)
[dsound] *** PRIMA CHIAMATA REALE a DirectSoundCreate (chiamante 0x189CC16C) ***
[dsound] DirectSoundCreate -> IDirectSound (mixer host)
[dsound] SetCooperativeLevel flags=0x00000002
[host] audio aperto: driver = dummy, 0x00005622 Hz, canali 0x00000002
[dsound] CreateSoundBuffer #0x00000001 PRIMARIA, byte 0x00000000, formato 0x56220210
[smack] dlopen fallito: /home/enne2/dev/third_party/libsmacker/build-i386/libsmacker-1.2.0.so
[smack] motivo: /home/enne2/dev/third_party/libsmacker/build-i386/libsmacker-1.2.0.so: cannot open shared object file: No such file or directory
[dsound] CreateSoundBuffer #0x00000002 secondaria, byte 0x00008380, formato 0x56220110
[smoke] exit=0
```

Conclusione: il precedente errore di pila/sentinella è superato. Le chiamate vtable DirectDraw arrivano a `SetDisplayMode` e `CreateSurface`; DirectSound risolve e completa `SetCooperativeLevel` e crea i buffer. L'implementazione non emette una riga per DirectDraw `SetCooperativeLevel`, quindi non lo si dichiara osservato in log. Lo smoke termina con exit 0 ma non stampa una conferma del menu principale. Il prossimo limite osservato è il `.so` libsmacker non presente al percorso configurato; non è un errore del thunk guest-ABI.

## File modificati

- `build/runtime-arm/shim-port/tools/elf/com_ddraw.c`
- `build/runtime-arm/shim-port/tools/elf/com_dsound.c`
- `build/runtime-arm/shim-port/tools/elf/smack_elf.c`
- `runtime/arm/com_bridge_arm.c` (nuovo)
- `runtime/arm/com_bridge_arm.S` (nuovo)
- `tools/build-entry-obj.sh`
- `work/link/REPORT-arm-com.md`

Hash oggetti: `com_ddraw.o` `5aaa146edcdc418eb521d8969d9f43fbdd7cf0bb5e7d96021588434afb55bf4a`; `com_dsound.o` `e1f6786b124fc7a7c9f52f9a52326c860df432e83cfedb101e2788b157d13243`; `smack_elf.o` `bc9b2c846bfbf1c2377f663e1544fe14022b83109ba76a9f3fcb0973faa3cad5`.

## Diagnostica dell'uscita 0 (20/11/2026)

Comando richiesto, dalla sandbox, con `SC_BRIDGE_TRACE=1 REASM_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SC_SMACK=0 timeout 20`: `build/qa/run-com-trace.log`, exit 0. Il trace ordinario limita i nomi import alle prime 4000 chiamate. Per identificare l'ultimo import ho temporaneamente alzato **solo** quel limite in `runtime/arm/bridge_arm.c` (4000→100000), rilinkato un binario diagnostico separato `build/starcraft-armv6-com-trace` e ripetuto lo stesso comando: log integrale `build/qa/run-com-trace-uncapped.log`, exit 0. Il limite è stato ripristinato a 4000, poi `build/starcraft-armv6-com` è stato rilinkato; il suo SHA è tornato identico (`4b5cc9f9…`). Nessuna modifica permanente a `runtime/arm/bridge_arm.c`.

L'ultimo percorso registrato non è `ExitProcess`, `TerminateProcess`, `GetMessageA`, `PostQuitMessage` né il ritorno dal runner: è una normale terminazione del thread Storm:

```text
ShowWindow @ 0x180fca10
 0x00010001 0x00000006
 = 0x00000001
SetEvent @ 0x185ad784
 0x5e000045
 = 0x00000001
...
ExitThread @ 0x1807cb98
 0x00000000
[smoke] exit=0
```

Oracolo IDA (sessioni `StarCraft.orig.exe` e `storm.orig.dll`, senza annotazioni/modifiche): l'indirizzo di ritorno host `0x1807CB98` è `loc_40CCA6`, immediatamente dopo `call ExitThread` a `0x0040CCA0` in `__endthreadex` (`0x0040CC76`). Il wrapper CRT `__threadstartex@4` (`0x0040CCA7`) chiama la routine thread guest, poi passa il suo return code a `_endthreadex`; qui l'argomento osservato è 0. Il target è il worker Storm `StartAddress` (`storm.dll:0x15016C10`), che IDA mostra come loop bloccante su `WaitForSingleObject`; quando `dword_1505E5CC` diventa nonzero esce con return 0 (`0x15016E32–0x15016E3E`). `storm_262` (`0x15015D40`) imposta quel flag, segnala l'evento e attende il worker; è chiamata dal cleanup Storm `storm_301` (`0x1500F090`).

**Bug immediato, fuori dal perimetro dei file assegnati:** in `build/runtime-arm/shim-port/tools/elf/win32_shim.c`, `ExitThread` è implementata come `S void ExitThread(u32 code) { _exit((int) code); }` (riga 1835 nel sorgente locale). L'uscita legittima del solo worker con codice 0 termina quindi l'intero processo host con exit 0. La semantica corretta è terminare il thread corrente, non chiamare `_exit`; il fix va fatto in `win32_shim.c`/gestione thread, non nei file COM/Smack autorizzati. Non ho modificato quel file.

C'è anche evidenza che il programma principale sta già eseguendo cleanup: `ShowWindow(0x10001, 6)` ha return address `0x0041D927`, dentro `sub_41D8B0` (`0x0041D8B0`), che rilascia i quattro oggetti audio e minimizza la finestra; la funzione è chiamata dal callback atexit `sub_4DA310`, registrato da `sub_4DB060` (`0x004DB060`, chiamata da `_WinMain@16`/`0x004E0820`). In IDA `sub_4E0820` è una state machine non ritornante; il ramo `default` (`0x004E0A9C`) chiama `exit(0)` a `0x004E0AAD`. La sequenza di cleanup è coerente con quel ramo di uscita CRT, ma il trace non ha registrato il valore di `word_596904`: **non posso provare quale stato invalido abbia scelto il default né attribuirlo al driver dummy**. Nessuna `GetMessageA`/`PostQuitMessage` compare nel trace uncapped. `GetSystemMetrics` col dummy ha restituito 480 per `SM_CYSCREEN` e 640 per `SM_CXSCREEN`, e la finestra 640×480 è stata creata; non è evidenza di dimensioni zero/non supportate. Quindi non è un ritorno dimostrato da `WinMain`: la funzione principale è `__noreturn`, e termina tramite il ramo `exit(0)` della state machine; l'ExitThread errata del worker è ciò che fa materialmente terminare il processo durante il cleanup.

### libsmacker / `SC_SMACK=0`

Il sorgente esterno esiste (`~/dev/third_party/libsmacker/smacker.c`, `smacker.h`, `driver.c`). Il file configurato esiste ma `file`/`readelf` lo identificano come **ELF 32-bit Intel 80386**, incompatibile con il processo ARM: non è la libreria da caricare nel guest ARM. La directory esterna risultava `?? build-i386/` nello stato Git; non ho modificato né buildato il repository esterno. Con `SC_SMACK=0`, `active()` in `smack_elf.c` restituisce false prima di `lib_load()` (e prima di risolvere le procedure); nel log richiesto non compare alcuna riga `[smack]`, quindi il salto filmati è pulito.

**Decisione autonoma:** nessuna modifica fuori perimetro; segnalo il fix necessario su `ExitThread` e lascio al supervisore la scelta di autorizzare il cambiamento. Il binario canonico è stato ripristinato, mentre il binario e il log di trace uncapped restano come artefatti diagnostici.
