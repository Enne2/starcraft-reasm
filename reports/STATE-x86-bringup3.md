# Stato x86 bring-up 3 — 21/11/2026

- `0x18140AEE` risolto: `win32_shim.c` chiamava helper cdecl tramite prototipi stdcall; mismatch provato da header toolkit, epiloghi compilati e GDB. Fix con puntatori cdecl solo `__i386__`; ARM invariato.
- Rebuild shim/runtime/link eseguiti con exit 0; ELF32/EXEC/i386. `dump-map`: 102376 voci, ordinate/univoche, 0 collisioni.
- Smoke dummy arriva oltre `GetModuleHandleA`, quattro `GetProcAddress`, `TlsAlloc`, critical section e `HeapAlloc`, poi `VirtualAlloc`; exit 9 per SIGSEGV, non menu.
- Nuovo fault GDB: `0x1802CE74`, `vm_split`, `movaps %xmm0,0x20(%esp)`; ESP `0xFFFFC118`, dest `0xFFFFC138` disallineata. Catena: `VirtualAlloc → call4 → arm_bridge_common_c → x86_bridge_common`.
- Applicata modifica x86-only `runtime/arm/gen_bridge_stubs.py` per riallineare ESP prima delle chiamate C. **Non ricostruita/testata** al momento del report.
- MPQ, `SetDisplayMode`, SCode/blitter/Smacker, menu e prova SDL reale: non raggiunti.
- Log: `build/qa/run-x86-bringup3.log`, `build/qa/gdb-x86-bringup3-vm.log`; dettagli in `reports/REPORT-x86-bringup3.md`.

## Decisioni autonome

Fix ABI cdecl limitato a i386; nessun allargamento del resolver né stub silenziosi. Per il secondo fault, riallineamento stack nella bridge x86; patch non verificata. Nessun git, sotto-agente o input desktop.

## Prossimo passo

Rebuild shim/runtime/link (`TMPDIR=/dev/shm tools/build-shim-x86.sh && TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh`), verificare allineamento GDB/objdump e ripetere smoke dummy 15 s; poi diagnosticare ogni blocco fino al menu. Solo al menu dummy, prova 5 s SDL reale senza input.
