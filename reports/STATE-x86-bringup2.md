# Stato x86 bring-up 2 — 21/11/2026

- Build/link: PASS, ELF32 EXEC Intel 80386; dump-map 102376 voci, 0 collisioni.
- Fix precedente: resolver x86 accetta continuation solo nei bounds linkati `.guest_exe_code`/`.guest_storm_code`; handler x86 TEB per Get/SetLastError. ARM invariato, D19 fail-closed.
- Trace `SC_BRIDGE_TRACE=1 REASM_TRACE=1`: ultimo import osservato `GetModuleHandleA @ 0x10041A10`, argomento `kernel32.dll` (`0x18127248`), risultato `0x7000A100`; poi SIGSEGV `PC=0x18140AEE` in stringa read-only `CurrentProcessId\0GetModuleHandleA`.
- `0x1812723C` è stringa `FlsAlloc`. `loc_1500870C` disassemblata prepara `GetProcAddress` (`0x18001384`), argomento `FlsAlloc`, continuation `0x10041AA0`; non compare però GetProcAddress nella trace.
- Mappa nm: `1002A120→loc_150065B7`; `1002A410→loc_15006627`; `1002B6B0→loc_1500677B`; `100EB600→loc_1501F75E`; `101CF0A0→loc_1503F6E0`; `1003A4E0` da risolvere esattamente al prossimo GDB/lookup.
- GDB: EIP `18140AEE`, CPU `EBX=0x0E000000`, EBP `0x10041A10`, EAX/EDX `0x7000A100`; host stack contiene `GetModuleHandleA__c`. Breakpoints GetProcAddress bridge/caller non scattati. Causa che trasferisce EIP nei dati non ancora provata.
- Log: `build/qa/run-x86-trace.log`, `build/qa/gdb-x86-sigsegv2.log`; riferimenti e limiti in `reports/REPORT-x86-bringup2.md`.
- Non raggiunti MPQ, SetDisplayMode, Smacker, menu; finestra SDL reale non provata.

## Decisioni autonome

Nessun allargamento dispatcher o target fittizio. Diagnosi senza git, sub-agent, input desktop o modifiche a file vietati.

## Prossimo passo

Rilanciare GDB nel sandbox con `SC_BRIDGE_TRACE=1 REASM_TRACE=1`; breakpoint su `0x10041A10`, `0x18001384`, bridge common solo con `EAX==0x83`, `GetProcAddress__c`, `reasm_indirect`/resolver e SIGSEGV. Registrare argomenti/stack guest e fare single-step dal blocco `loc_1500870C`: stabilire se/come raggiunge il thunk e quale istruzione trasferisce EIP a `0x18140AEE`. Correlare con LLASM/objdump, applicare solo fix dimostrato, rebuild/link e smoke dummy; SDL reale solo dopo il menu.
