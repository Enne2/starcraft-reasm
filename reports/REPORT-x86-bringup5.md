# Report — x86 bring-up 5 (frigate)

## Esito

Build, runtime check e dispatch-map passano. Lo smoke dummy ora supera `SetDisplayMode`, crea la superficie primaria, raggiunge DirectSound e Smacker, ed emette ripetuti `[fps]` (circa 27–56 FPS). Non è provato che sia arrivato al menu principale; nessuna prova SDL2 reale è stata eseguita. L’ultimo smoke (15 s, `SC_BRIDGE_TRACE=1`) ha exit 124; il log registra poi un SIGSEGV guest con PC/fault address `0x00056F24`, EIP riportato `0x0000003F`. Il guasto finale resta da diagnosticare.

## Fix e verifiche

- `runtime/shim-port/elf/win32_shim.c`: `GetLastError`/`SetLastError` usano `X86_ReadFsDword`/`X86_WriteFsDword` nel runtime x86, tramite `SC_PORT_EMULATED_TEB`; niente accesso al `%fs` Linux del worker. Stessa macro abilita l’include `x86_helpers.h`.
- `tools/build-shim-x86.sh`: passa `-DSC_PORT_EMULATED_TEB=1`; il controllo delle undefined continua a rifiutare riferimenti ARM non previsti e permette solo i tre helper runtime `arm_bridge_lookup_thunk`, `arm_com_wrap_vtable`, `arm_com_thunk` richiesti dal bridge i386.
- `runtime/shim-port/elf/win32_shim.c`: `GetProcAddress` consulta `arm_bridge_lookup_thunk` anche nel runtime tradotto i386, così le API dinamiche passano dal bridge guest ABI invece che restituire una funzione C diretta.
- `runtime/shim-port/elf/win32_ui.c`: `SendMessageA` e `CallWindowProcA` usano `arm_guest_call4` anche su i386 per richiamare i callback LLASM; il primo crash `loc_4D1D70` mostrava che la chiamata nativa diretta violava l’ABI guest.
- `runtime/shim-port/elf/com_ddraw.c`, `com_dsound.c`, `smack_elf.c`: abilitate su i386 le stesse thunk COM/export guest ABI già usate su ARM. Evidenza: senza wrapper dinamico `DirectDrawCreate` riceveva `_cpu` come `guid`; con wrapper e COM thunk lo smoke arriva a SetDisplayMode, DirectSound, Smack e FPS.
- Audit `runtime/arm/bridge_arm.c`: le sole dichiarazioni `extern ...__c` sono `WaitForSingleObject__c` e `DialogBoxParamA__c`; entrambe hanno `SHIM_STDCALL`, coerente con le implementazioni `S` negli shim. Nessuna modifica a `bridge_arm.c` in questa sessione.
- `tests/x86_runtime_check/check.sh`: PASS. ELF: ELF32 / EXEC / Intel 80386. `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102376 voci ordinate/univoche, 0 collisioni.

## Evidenze e log

- `build/qa/run-x86-bringup5.log`: fix TEB applicato, smoke supera il fault `%fs`, poi crash nel WndProc LLASM invocato con ABI nativa.
- `build/qa/gdb-x86-bringup5-fault.log`: SIGSEGV in `loc_4D1D70+13`, da `SendMessageA` durante `CreateWindowExA`.
- `build/qa/run-x86-bringup5-fix1.log`: callback guest invocato, ma `DirectDrawCreate` dinamica riceve argomenti ABI errati; `build/qa/gdb-x86-bringup5-ddraw.log` conferma `guid=0x0E000000`, `out=arm_guest_sentinel`.
- `build/qa/run-x86-bringup5-fix2.log`: dopo la thunk dinamica, fault a `0x48`; il successivo wrapper COM ha consentito di proseguire.
- `build/qa/run-x86-bringup5-fix3.log`: `SetDisplayMode`, `CreateSurface PRIMARY`, `DirectSoundCreate`, `SmackOpen`/`SmackBufferOpen`, numerosi `[fps]`; SIGSEGV finale a `0x56F24`. Il log con tracing è molto grande (oltre 6,2 milioni di righe).
- `build/qa/gdb-x86-bringup5-fault2.log`, `gdb-x86-bringup5-fault3.log`, `gdb-x86-bringup5-dynamic-ddraw.log`, `gdb-x86-bringup5-wndproc.log`: sessioni di diagnosi intermedie.
- IDA non ha aperto il database né il PE originale (`ida_open` failed); per la funzione `0x004D1D70` ho verificato il disassemblato mirato con `llvm-objdump`.

## Decisioni autonome

Ho esteso con prudenza le guardie `__arm__` a `__i386__` solo dove la traduzione LLASM richiede il guest-ABI bridge (callback, API dinamiche e COM/Smacker). Ho lasciato invariati i rami `int 0x80`/`statfs` che sono specifici dell’ABI Linux x86, il resolver fail-closed e gli stub sconosciuti. Ho fermato il bring-up sul SIGSEGV finale; non ho eseguito input desktop né prova SDL reale perché il menu non è confermato.

## Prossimo passo

Rilanciare la diagnosi dal fault finale: eseguire lo smoke dummy senza `SC_BRIDGE_TRACE` sotto GDB con `handle SIGSEGV stop print nopass`, salvando registri, stack host e backtrace; poi risalire dal PC `0x56F24`/EIP guest `0x3F` al target e caller effettivi (non dedurre il VA dal solo PC), usando IDA sul PE originale se disponibile oppure `llvm-objdump`/`rg` mirati. Correggere solo la causa ABI/guest dimostrata, ricostruire e ripetere lo smoke fino a confermare il menu; solo allora fare il test SDL2 reale di 5 secondi senza input desktop.
