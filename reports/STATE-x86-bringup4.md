# Stato x86 bring-up 4 — 21/11/2026

- Stack alignment: `x86_bridge_common`, `reasm_indirect`, `reasm_extern`, `arm_com_bridge_common` allineano `%esp` a 16 prima dei call C. Test runtime PASS.
- Fix1 in `runtime/arm/bridge_arm.c`: `WaitForSingleObject__c` e `DialogBoxParamA__c` dichiarate stdcall su i386 soltanto. Build/link PASS; ELF32 EXEC i386; dump-map 102376 voci, 0 collisioni.
- Primo crash worker: PC `0xF786A7EC` puntava a bytes ASCII di log nello stack host. Slot vicino conteneva return `0x100AE500` (`loc_15016E23`); guest EIP `0x5E000004`. Disassemblato mostra wait a `loc_15016E13`; shim stdcall finiva con `ret 8`, caller cdecl: causa return corruption provata. Fix1 supera questo crash.
- `run-x86-bringup4-fix1.log`: apre `Stardat.mpq`, `patch_rt.mpq`, `BroodWar.mpq`, `BrooDat.mpq`; poi SIGSEGV: PC `0x1802DE07` (`SetLastError__c`, `mov fs:0x34,eax`), fault address 0; EIP guest `0x71000001`. Probabile FS host non installato sul worker guest (GDB precedente: FS=0); causa del valore/ramo che chiama SetLastError non ancora chiarita.
- Nessun menu/FPS; SDL reale non eseguito.
- Log: `build/qa/run-x86-bringup4-fix1.log`; GDB precedente `build/qa/gdb-x86-bringup4-{thread,fault}.log`.

## Decisioni autonome

Non impostare FS host né alterare resolver: prossimo fix proposto è usare TEB emulato per Get/SetLastError anche su i386. Soglia contesto raggiunta: fermo qui.

## Prossimo passo

In `runtime/shim-port/elf/win32_shim.c` includere `__i386__` nel ramo `__arm__` di `GetLastError`/`SetLastError`, instradandoli a `X86_ReadFsDword`/`X86_WriteFsDword` (verificare include/prototipi; lasciare ARM invariato). Poi rebuild shim/runtime/link, eseguire `tests/x86_runtime_check/check.sh` e ripetere smoke 15 s su dummy in `build/qa/run-x86-bringup4-fix2.log`; GDB sul fault successivo. SDL reale solo dopo aver raggiunto il menu.

## Procedura

Un `git status --short` fu tentato per errore nel primo turno e fallì subito perché la directory non è un repository Git; nessuna modifica di stato né altri comandi Git.
