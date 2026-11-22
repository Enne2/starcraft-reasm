# Stato — scx86-play4 (21/11/2026)

- TIMERPROC guest instradato tramite `arm_guest_call4` in `runtime/shim-port/elf/win32_shim.c`.
- SCode #334 verificato: PE Storm export ordinale #334 → `0x15030AB0`; `storm.llasm:1393` lo definisce. GDB `build/qa/gdb-play4-scode2.log`: kernel Boot Camp 60×56, LUT base nulla, descriptor flags `0x04000000`, `p[0]=0x40`; SCode diretto `8A 06 FF C6 8A E0 66 89 07 81 C7 02...`, byte→word senza LUT.
- Fix `runtime/common/storm_sdraw_arm.c`: supportati `8A 06`/`8A E0`, accessi dati verificati con `proc_range` al punto d’uso; full-LUT check solo per fast path LUT noto. Fail-closed mantenuto.
- Build `./tools/build.sh --arch x86 --skip-entry`: **PASS**; `./tests/x86_runtime_check/check.sh`: **PASS**.
- Navigazione isolata 640×480: menu → Single Player `(220,120)` → Original `(260,226)` → profilo `enne2ù` `(100,109)` + OK `(517,391)` → Terran `(305,250)` → story → click `(320,440)` → title card BOOT CAMP.
- Briefing raggiunto: obiettivi Build 3 Supply Depots / Build a Refinery / Gather 100 Vespene Gas. Start `(538,390)`, Skip Tutorial `(150,445)`, Replay `(380,419)`, Cancel `(545,430)`.
- Screenshot briefing `build/qa/play-screenshots/play4-17-after-scode-fix.png`; dopo Start `play4-18-after-start.png`.
- Nuovo STOP dopo Start: target indiretto `0x0435A008`, non in mappa/sezioni tradotte; anello indica caller guest `0x0046A4D2` e `0x0040912C`. Log `build/isolated/play4/game.log`. Partita/HUD non raggiunti.
- `run-isolated.sh stop --id play4` eseguito; processo assente. Nessun audio/display reale.

## Prossimo passo

Rilanciare `play4` solo via runner isolato; attaccare GDB prima della navigazione e porre breakpoint condizionale `reasm_indirect_unresolved` per `target==0x0435A008`. Acquisire backtrace host, CPU/stack guest, origine del valore e memoria associata; correlare `0x0046A4D2`/`0x0040912C` con PE originale in IDA/`llvm-objdump`. Correggere solo con causa provata e D19 intatto, rebuild + `x86_runtime_check`, poi riprendere da Boot Camp → Start.
