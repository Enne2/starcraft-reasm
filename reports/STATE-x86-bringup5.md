# Stato x86 bring-up 5 — frigate

- Build shim/runtime/link PASS; `tests/x86_runtime_check/check.sh` PASS.
- ELF32 / EXEC / Intel 80386. Dispatch-map: 102376 entry, ordinate/univoche, 0 collisioni.
- Fix TEB: `win32_shim.c` Get/SetLastError usano TEB emulato con `SC_PORT_EMULATED_TEB`; build shim abilita macro.
- Fix callback: `win32_ui.c` SendMessage/CallWindowProc usano `arm_guest_call4` su i386.
- API dinamiche: GetProcAddress instrada i nomi al bridge thunk. COM/DDraw, DSound e Smacker abilitano su i386 `arm_com_thunk`/wrapper vtable.
- Audit bridge: soli `extern ...__c` = WaitForSingleObject e DialogBoxParamA, entrambi con `SHIM_STDCALL`; nessuna modifica a `bridge_arm.c`.
- Smoke dummy fix3: SetDisplayMode, PRIMARY surface, DirectSound, SmackOpen/BufferOpen e FPS 27–56 raggiunti; non confermato menu.
- Ultimo smoke: `build/qa/run-x86-bringup5-fix3.log`, exit 124; SIGSEGV a PC/address `0x56F24`, EIP guest `0x3F` dopo circa 15 s. Nessun test SDL2 reale.
- Log GDB e dettagli: `reports/REPORT-x86-bringup5.md`; log intermedi in `build/qa/gdb-x86-bringup5-*.log`.

## Decisioni autonome

Fix limitati ai bridge guest-ABI comprovati. Resolver/stub fail-closed invariati. Fermato il bring-up al fault finale, nessun input desktop.

## Prossimo passo

Ripetere smoke dummy sotto GDB (`handle SIGSEGV stop print nopass`, senza `SC_BRIDGE_TRACE`) e catturare registri, stack e backtrace per risalire dal PC `0x56F24`/EIP `0x3F` a target/caller effettivi; verificare il guest con IDA o disassemblato mirato. Correggere solo la causa dimostrata, ricostruire e raggiungere/confermare menu; poi test SDL2 reale 5 s.
