# Dialogo fatale ARM — LPARAM, autoclose diagnostico e punto di arresto successivo

## Esito

Aggiornato `dlg_elf.c` con una modalità opzionale di autoclose, disattivata per default, e con il target del caricamento esplicitato nel log. Il parametro del template 0x6A è ora riportato così:

```text
[dlg] template=0x6A text="Stardat.mpq\nunknown error 0x00000002" target="Stardat.mpq"
```

Testo verbatim guest:

```text
Stardat.mpq
unknown error 0x00000002
```

Con `SC_DLG_AUTOCLOSE=1`, dopo aver registrato il testo e consegnato `WM_INITDIALOG`, il port chiama direttamente `EndDialog(hwnd,2)`. Non simula click, tastiera o altri input. La modalità è disattivata per default; quando attiva chiude intenzionalmente il dialogo e quindi cambia il comportamento osservabile. È diagnostica, non una correzione del fallimento di caricamento.

## Dove prosegue il processo

Comando: da `build/asset-sandbox`, `ulimit -c 0; SC_DLG_AUTOCLOSE=1 timeout 20 qemu-arm-static -cpu arm1176 -L ../sysroot ../starcraft-armv6-dlg3`.

Dopo `DialogBoxParamA returns 2` il guest prosegue, ma non arriva a winmain/menu: si arresta con exit 71 sullo stop nominato del dispatch:

```text
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x00427714
  eip chiamante = 0x004207F1
  esp       = 0x021FF928
```

Il chiamante è nel runner dei distruttori `sub_4207B0`; l’identificazione del target e il contesto sono documentati in `REPORT-flash-heap.md` §11.1. Quindi sappiamo che il codice oltre il dialogo viene eseguito, ma il percorso si ferma prima del menu. Questo risultato non dichiara il gioco funzionante.

Log completo: `build/qa/run-dlg3-autoclose.log`. Non si osservano SIGBUS/SIGSEGV; la chiusura è via API dialogo, non input simulato.

## Diagnosi LPARAM e callback

`sub_4212C0` (VA `0x004212C0`) compone il buffer locale con `"%s\n%s"` e lo passa come quinto argomento a `DialogBoxParamA`. `imp_DialogBoxParamA` estrae l’argomento con `cpu_llasm_arg32(cpu,4)` e il port lo consegna come LPARAM di `WM_INITDIALOG`. Non era un puntatore guest malformato: il precedente `process_vm_readv()` restituisce `ENOSYS` (errno 38) in qemu-user ARM anche per un buffer locale. È sostituito dalla lettura limitata a regioni `r` verificate in `/proc/self/maps`; gli indirizzi illeggibili non sono dereferenziati. Il target è ricavato dalla prima riga del testo già passato dal chiamante.

Il SIGBUS storico del DialogProc era dovuto alla chiamata C diretta di una funzione guest (`fastcc void(%_cpu*)`). Il trampolino `dlg_thunk_*` di `bridge_arm.c` chiama `arm_guest_call4`, rispettando l’ABI guest. Nel run corrente `WM_INITDIALOG`, i controlli, `EndDialog` e il ritorno dal dialogo avvengono senza SIGBUS.

## Gate e artefatti

- `make check`: **PASS**, exit 0; log `build/qa/make-check-dlg3.log`.
- Smoke asset reali, QEMU ARM1176, `ulimit -c 0`, `SC_DLG_AUTOCLOSE=1`: prosegue oltre il dialogo, poi stop dichiarato a `0x00427714`; exit 71.
- Candidato nuovo, senza sovrascrivere i precedenti: `build/starcraft-armv6-dlg3`.
- SHA-256: `4d6d9a89a1213504e2c30b08834edf35fa5e50ed590dd29d57180c2c281252d0`.
- File sorgente di proprietà modificato: `build/runtime-arm/shim-port/tools/elf/dlg_elf.c`.

## Decisioni autonome

- `SC_DLG_AUTOCLOSE` richiede il valore esatto `1`; default invariato e chiusura tramite `EndDialog(hwnd,2)` dopo la registrazione diagnostica.
- L’autoclose non tenta di emulare l’intero callback del pulsante: non inietta input e non afferma equivalenza funzionale con una sessione utente.
- Il candidato usa il suffisso nuovo `dlg3`; i candidati precedenti sono rimasti intatti.

## Follow-up ExitThread: terminazione del solo worker

In `runtime/arm/bridge_arm.c`, `imp_CreateThread` delega ad `arm_guest_thread_start`; in `guest_call_arm.c` questo crea un `pthread` (`pthread_create(..., arm_thread_main, ...)`) con CPU e stack guest distinti. `main` non è creato da `CreateThread`: `game_main_arm.c:129-137` chiama `win32_shim_init()` nel thread principale host, poi avvia direttamente `cpu_llasm_run_raw`.

Per gli handle creati dal bridge, `imp_WaitForSingleObject` riconosce il range `0x72000000` e chiama `arm_guest_thread_join`, che fa `pthread_join` e restituisce `WAIT_OBJECT_0`. Quindi `ExitThread` sul worker può usare `pthread_exit((void *)(ulong)code)`: termina il solo pthread e rende completato il join. Il codice di ritorno del pthread non è il valore Win32 di WaitForSingleObject (che resta `WAIT_OBJECT_0`). Anche il percorso `th_tramp` legacy di `win32_shim.c` è un pthread e la sua `WaitForSingleObject` fa `pthread_join`. Sul thread host principale, l’implementazione mantiene `_exit(code)` per non lasciare il processo senza il suo thread d’ingresso.

Modifiche a `win32_shim.c`: dichiarati `pthread_self`/`pthread_exit`, `win32_shim_init()` salva l’identità del thread principale, ed `ExitThread` usa `pthread_exit` se il chiamante è un worker; solo il thread principale conserva `_exit`. Ricompilato soltanto `build/runtime-arm/shim-port/obj/win32_shim.o` con gli stessi flag ARMv6 hard-float/include del runtime; `llvm-nm -u` mostra `U pthread_exit` e `U pthread_self`.

Build: `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-thread` (658 ms, link 348 ms). Verifica finale `make check`: **PASS**, exit 0 (`build/qa/make-check-thread.log`).

Smoke eseguito da `build/asset-sandbox` con `ulimit -c 0`, `SC_BRIDGE_TRACE=1`, `SDL_VIDEODRIVER=dummy`, `timeout -k 2s 12s qemu-arm-static -cpu arm1176 -L ../sysroot ../starcraft-armv6-thread`: exit **71** prima del timeout. Il trace mostra un `CreateThread` e ripetuti `WaitForSingleObject`; il processo host non viene terminato da `_exit(0)`. Il run arriva al dialogo fatale per `smk\\blizzard.smk` / `unknown error 0x00000002`, poi si ferma sul prossimo target guest non tradotto `0x004D1DBC` (caller `0x00000000`), non su `IDirectDraw::*` e non per SIGSEGV. Evidenza completa: `build/qa/run-thread.log`.

```text
[dlg] control=0x3E8 text="smk\\blizzard.smk\nunknown error 0x00000002"
[dlg] WM_INITDIALOG delivered; entering modal loop
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x004D1DBC
  eip chiamante = 0x00000000
[smoke] exit=71
```

Candidato: `build/starcraft-armv6-thread`; SHA-256 `640500aa2f6b188071a164591a04514a45d07bd2012c8eaf2b0db368f067caf5`.
File sorgente modificato in questo follow-up: `build/runtime-arm/shim-port/tools/elf/win32_shim.c`; oggetto rigenerato: `build/runtime-arm/shim-port/obj/win32_shim.o`. `bridge_arm.c` e `guest_call_arm.c` sono stati solo letti.

### Decisioni autonome (ExitThread)

- Ho identificato il thread principale per `pthread_self()` in `win32_shim_init()`; `ExitThread` dal main resta `_exit`, mentre i pthread worker usano `pthread_exit`.
- Il valore di `pthread_exit` è un valore di join; il risultato Win32 dell’attesa rimane `WAIT_OBJECT_0`, come previsto dal bridge `imp_WaitForSingleObject`.

## Follow-up critico: callback WndProc ARM (`win32_ui.c`)

Applicata a `build/runtime-arm/shim-port/tools/elf/win32_ui.c` la sezione pertinente di `runtime/arm/patches/0002-guest-callbacks.patch` (la patch completa contiene anche hunk `dlg_elf.c`, non applicabile qui e non necessario: `DialogBoxParamA` passa già dal trampolino C in `bridge_arm.c`). La WndProc in `SendMessageA` ora passa su ARM da `arm_guest_call4(proc, hwnd, msg, wp, lp)`; `CallWindowProcA` ha lo stesso dispatch. Le call-site `CreateWindowExA` (WM_NCCREATE), `DestroyWindow` (WM_DESTROY/WM_NCDESTROY) e `DispatchMessageA` transitano in `SendMessageA`, quindi sono coperte dal percorso centrale. L’unico call diretto rimasto è il fallback per non-ARM o quando il simbolo weak `arm_guest_call4` non è disponibile.

Ricompilato **solo** `build/runtime-arm/shim-port/obj/win32_ui.o`, con flag ARMv6 hard-float del runtime, include SDL/sysroot del progetto e `-DSC_ELF_REAL_GDI=1` (come stage 8.4 di `tools/build-runtime-arm.sh`). Verifica simbolica: `llvm-nm .../win32_ui.o` mostra `w arm_guest_call4`. Oggetto nuovo: 148088 byte, timestamp 2026-11-20 18:08:13 +0100.

Eseguito `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-win`: link 415 ms, totale 737 ms.

Gate successivi:

- `make check`: **PASS**, exit 0; log `build/qa/make-check-win.log`.
- Smoke da `build/asset-sandbox`, `ulimit -c 0`, `SDL_VIDEODRIVER=dummy REASM_TRACE=1`, QEMU ARM1176: exit 9, log `build/qa/run-win.log`.
- La finestra `SWarClass` viene registrata e creata (`hwnd=0x00010001`, 640×480); il run supera la chiamata WndProc di creazione e l’attivazione (`ShowWindow: WM_ACTIVATEAPP(TRUE)`). Dopo il caricamento dello shim `ddraw.dll`, arriva a un nuovo SIGSEGV, non più al fault della WndProc:

```text
[arm-cpu] SIGSEGV
[arm-cpu] fault pc 0x189D5F8C  lr 0x189D5F58
[arm-cpu] procedura tradotta: entry VA 0x15040E07  offset nel proc 0x0000A8C0
[arm-cpu] fault address 0x000007CA
[arm-cpu] guest eip 0x180FCF08  esp 0x021FF9C4  eax 0x189D5F28  edx 0x1505CA00
```

Dunque raggiunge creazione/attivazione finestra e caricamento DirectDraw, ma non menu, rendering o SMACK; il prossimo stop è SIGSEGV nella procedura storm `0x15040E07` al fault sopra. Nessun SIGBUS.

- Candidato nuovo: `build/starcraft-armv6-win`.
- SHA-256: `308519cb61dc8f1b7f8d91ab1fa50a936819152a7e48546c6880956cb72c3dc0`.
- Sorgente modificato in questo follow-up: `build/runtime-arm/shim-port/tools/elf/win32_ui.c`; oggetto rigenerato: `build/runtime-arm/shim-port/obj/win32_ui.o`. `dlg_elf.c` non è stato modificato in questo follow-up.

### Decisioni autonome (follow-up WndProc)

- Ho applicato solo gli hunk della patch 0002 per `win32_ui.c`: il suo hunk dialogo non è necessario perché il dialogo già usa il trampolino del bridge e chiamarlo di nuovo via guest-call cambierebbe l’ABI del thunk.
- Ho rispettato il vincolo di ricompilare solo `win32_ui.o`; la rigenerazione runtime-only ha creato un candidato con suffisso nuovo.

## Follow-up GetProcAddress / DirectDraw

Applicato `runtime/arm/patches/0003-getprocaddress-bridge.patch` al solo `build/runtime-arm/shim-port/tools/elf/win32_shim.c`. Il dry-run è riuscito con offset di 88 righe: l’hunk si inserisce all’inizio di `GetProcAddress`, prima dei resolver `win32_smack_get_proc`, `win32_dsound_get_proc` e `win32_ui_get_proc`. Le modifiche path/eventi preesistenti di `evt` sono rimaste intatte. Su ARM il bridge ora consulta prima `arm_bridge_lookup_thunk` e restituisce il thunk guest quando disponibile.

Ricompilato **solo** `build/runtime-arm/shim-port/obj/win32_shim.o` con gli stessi flag di target e include dello stage 8.4 (`-march=armv6zk -mfpu=vfp -mfloat-abi=hard`, sysroot e include SDL/toolkit/runtime). `llvm-nm -u` conferma `w arm_bridge_lookup_thunk`; oggetto 175088 byte.

Eseguito `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-ddraw` (totale 626 ms, link 342 ms). Gate:

- `make check`: **PASS**, exit 0; log `build/qa/make-check-ddraw.log`.
- Smoke richiesto con `SDL_VIDEODRIVER=dummy REASM_TRACE=1`, `ulimit -c 0`, QEMU ARM1176: log `build/qa/run-ddraw.log`, exit **3**.
- Crea e attiva `SWarClass`, carica lo shim `ddraw.dll`. Il trace mostra la risoluzione dinamica a `0x189CCDF4`, che nel binario è il thunk guest `DirectDrawCreate`; la chiamata con argomenti `0, 0x006D5E08, 0` restituisce `0` (successo/HRESULT S_OK). La build quindi supera il precedente crash dentro `DirectDrawCreate__c` / fault `0x7CA`.
- Non arriva però a una chiamata `IDirectDraw::*`: termina subito dopo con `[arm-game] l'entry e' tornata senza consumare il sentinella (pila sbilanciata)` (exit 3). Non c’è un SIGSEGV né uno stop nominato su vtable COM in questo run. È il prossimo ostacolo esatto da investigare; non affermo che il percorso COM sia stato raggiunto.

```text
[win32] CreateWindowExA cls=SWarClass title=Brood War hwnd=0x00010001 rect=0,0 640x480 style=0x90080000 parent=0x0
[win32] ShowWindow: WM_ACTIVATEAPP(TRUE) a 0x00010001
[win32] modulo emulato dagli shim: ddraw.dll
  0x7000dd00 0x00505f08
 = 0x189ccdf4
  0x00000000 0x006d5e08 0x00000000
 = 0x00000000
[arm-game] l'entry e' tornata senza consumare il sentinella (pila sbilanciata)
[smoke] exit=3
```

Candidato: `build/starcraft-armv6-ddraw`; SHA-256 `386acd7873f71414f61ec451b47f6d935e43ed023847c0676e9f7681379bbb86`.
File sorgente modificato in questo follow-up: `build/runtime-arm/shim-port/tools/elf/win32_shim.c`; oggetto ricompilato: `build/runtime-arm/shim-port/obj/win32_shim.o`. Nessuna modifica a `com_ddraw.c`, `com_dsound.c` o `smack_elf.c`.

### Decisioni autonome (GetProcAddress)

- Ho applicato il solo hunk di `0003` in testa a `GetProcAddress`, preservando il codice concorrente path/eventi.
- Il trace termina con exit 3 senza chiamata COM osservata; non ho alterato l’ordine di ritorno né inventato una chiamata `IDirectDraw::*` per forzare il gate atteso.
