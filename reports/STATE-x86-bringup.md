# Stato x86 bring-up (21/11/2026)

- Build entry/runtime/link riusciti; ELF32 EXEC Intel 80386.
- Entry x86 ricostruiti con `llc -data-sections`: mantiene `.data..L_data` / `.rodata..L_*` invece di aggregare i globals guest e le tabelle host.
- `runtime/arm/gen_game_layout.py` è tornato al mapping per sezioni dedicate; ramo ARM invariato.
- `.guest_exe_data`: `0x0050C000 + 0x1d1694`; la successiva regione PE dati parte esattamente da `0x006DD694`. Nessuna sovrapposizione logica. Storm dati: `0x15059000 + 0x7fd8`, includendo `stru_1505BDC0`.
- `dispatch-ir.py --dump-map`: 102376 voci, 0 collisioni.
- Fix x86 in `tools/build-runtime-x86.sh`: caller fixed-arity stdcall; caller variadico cdecl. Evidenza: `InitializeCriticalSection__c` usa `ret $4`; prima `call1` cdecl causava stack cleanup doppio.
- Smoke trace attuale exit 71, `build/qa/run-x86-bringup.log`: InitializeCriticalSection arg `0x1505bdc0`, return 1; poi STOP fail-closed target `0x18001546` (thunk host InitializeCriticalSection), pur essendo entro range linker `0x18000000..0x1803a065`.
- Crash pre-fix GDB: EIP/EDI `0x0e000000`, EBP `0x101cee70`; diagnosi supporta corruzione del flusso/stack dal caller cdecl, superata dopo stdcall.
- Non raggiunti MPQ, SetDisplayMode, Smacker/menu/FPS. Nessuna prova x11.
- **Prossimo passo:** breakpoint GDB a ingresso `reasm_indirect_resolve` e verifica runtime di target/range/flag inizializzati; correggere il rifiuto del thunk host senza allentare D19, quindi rilinkare e ripetere smoke.
- Dettagli/comandi/log: `reports/REPORT-x86-bringup.md`.
