# Report — x86 bring-up 3 (frigate)

## Esito sintetico

La causa del salto a `0x18140AEE` è stata isolata e corretta in `runtime/shim-port/elf/win32_shim.c`: i wrapper Win32 chiamavano helper runtime compilati cdecl attraverso prototipi dichiarati stdcall. La correzione è limitata a `__i386__`; ARM mantiene chiamate e percorsi invariati.

Il binario ricostruito supera `GetModuleHandleA` e le successive chiamate `GetProcAddress`, ma lo smoke raggiunge `VirtualAlloc` e termina con un secondo SIGSEGV in `vm_split`: `movaps` usa uno stack host non allineato. MPQ, `SetDisplayMode` e menu non raggiunti. Dopo aver raccolto la prova del secondo crash, ho applicato una correzione x86-only in `runtime/arm/gen_bridge_stubs.py` per riallineare lo stack ai confini delle chiamate C; **non è stata ancora ricompilata né verificata**.

## Diagnosi e fix di `0x18140AEE`

`ida_list` non mostrava sessioni IDA aperte; ho usato `llvm-objdump`, GDB e LLASM mirato come previsto dal preludio.

- Disassembly del binario a `0x10041A10–0x10041B40`: `loc_1500870C` passa attraverso `GetModuleHandleA`, quindi prepara la continuazione `0x10041AA0` per `GetProcAddress("FlsAlloc")`.
- GDB sul bridge x86: indice import `12`, `_cpu=0x0E000000`, stack guest `0x021FFEA8` con return VA `0x10041A10` e argomento `0x18127248` (`kernel32.dll`). L’argomento è corretto; il wrapper restituisce `0x7000A100`.
- Prova ABI: `/home/enne2/dev/reasm-toolkit/runtime/elf/reasm_elf.h` dichiara `reasm_elf_GetModuleHandleA`, `reasm_elf_LoadLibraryA` e `reasm_elf_GetProcAddress` `REASM_STDCALL`; i corpi runtime in `runtime/arm/elf_support_arm.c` sono cdecl, verificato per `GetModuleHandleA` dal `ret` a `0x18003C61`. Il codice compilato del wrapper `GetModuleHandleA__c` mostrava `sub $4,%esp` dopo la chiamata al helper (vecchio binario, `0x1802F4AF`): cleanup incoerente che corrompeva lo stack e portava EIP alla stringa `.rodata` `GetModuleHandleA` a `0x18140AEE`.
- Fix x86-only: `win32_shim.c` chiama i tre helper tramite puntatori esplicitamente cdecl sotto `__i386__`; gli altri target usano alias alle chiamate originali. Il binario rebuild mostra ora la chiamata `reasm_elf_GetModuleHandleA` seguita dal salvataggio di EAX (senza il precedente `sub $4,%esp`). Nel trace, `GetModuleHandleA` torna, le quattro query successive a `GetProcAddress` tornano `0`, poi prosegue con `TlsAlloc`, `EnterCriticalSection`, `HeapAlloc` e `VirtualAlloc` senza tornare a `0x18140AEE`.

## Smoke e secondo blocco

- Build/runtime e link completati con exit 0 dopo il rebuild di shim, runtime e binario.
- `readelf -h build/starcraft-x86`: ELF32, EXEC, Intel 80386.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102376 voci ordinate/univoche, zero collisioni.
- Smoke dummy: `timeout 15` con `SC_SMACKER_SO`, `SC_SKIP_INTRO=1`, `SC_FPS_LOG=1`, `SC_BRIDGE_TRACE=1`, `REASM_TRACE=1`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`; exit osservato `9`. Log: `build/qa/run-x86-bringup3.log`.
- GDB sul secondo crash (`build/qa/gdb-x86-bringup3-vm.log`): PC `0x1802CE74`, funzione `vm_split`/`vm_reserve_locked`, istruzione `movaps %xmm0,0x20(%esp)`. ESP=`0xFFFFC118` e destinazione=`0xFFFFC138` (non allineata a 16 byte); fault address riportato `0`. Backtrace: `vm_split → vm_reserve_locked → VirtualAlloc → call4 → arm_bridge_common_c → x86_bridge_common`. Il crash è dunque un problema di allineamento stack host al confine della bridge, distinto dal mismatch cdecl/stdcall.
- Patch appena apportata in `runtime/arm/gen_bridge_stubs.py` (solo assembly emesso da `--arch x86`): salva i registri via frame pointer, riallinea ESP prima delle chiamate C e aggiunge padding corretto per gli argomenti. `--arch arm` non è stato modificato. Mancano rebuild, disassembly e smoke dopo questa patch.
- Nessuna prova finestra SDL reale: menu dummy non raggiunto. MPQ e `SetDisplayMode` non raggiunti.

## File modificati / evidenze

- Modificato `runtime/shim-port/elf/win32_shim.c` per le chiamate helper cdecl x86-only.
- Modificato `runtime/arm/gen_bridge_stubs.py` per il riallineamento x86-only, ancora da verificare.
- Log GDB del blocco originario: `/tmp/gdb-x86-bridgecall.log` e `/tmp/gdb-x86-bridge-return2.log`; smoke: `build/qa/run-x86-bringup3.log`; GDB secondo crash: `build/qa/gdb-x86-bringup3-vm.log`.
- Nessun git, sotto-agente, input desktop o modifica a `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/indirect_dispatch.S`.

## Decisioni autonome

Ho corretto il mismatch ABI solo sul percorso i386, mantenendo il dispatcher fail-closed (D19) e senza alterare l’accettazione dei target indiretti. Dopo la prova del secondo fault ho scelto il fix conservativo: allineare lo stack prima delle chiamate C nella bridge x86, senza cambiare la logica ARM. Ho fermato l’iterazione al gate di report richiesto: la patch di allineamento resta non verificata.

## Prossimo passo

Eseguire in ordine: `TMPDIR=/dev/shm tools/build-shim-x86.sh && TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh`; verificare con GDB/objdump lo stack alignment della bridge (call-site ESP multiplo di 16) e ripetere lo smoke dummy di 15 s. Se il crash `movaps` è superato, proseguire isolando il successivo blocco fino a MPQ, `SetDisplayMode`, SCode/blitter/Smacker e menu; solo dopo il menu dummy eseguire `timeout 5` con SDL reale senza input desktop.
