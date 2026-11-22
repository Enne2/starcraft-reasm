# Report — bring-up x86 (stato aggiornato, menu non raggiunto)

## Esito

Build e link x86 riescono. La segmentazione dati guest ora è corretta e non sovrapposta; il primo crash su `InitializeCriticalSection` è stato ricondotto a un mismatch stdcall/cdecl nei caller shim e scompare dopo il fix. Il nuovo smoke supera l'API (risultato `1`) ma si arresta con lo STOP fail-closed di dispatch a `0x18001546` (thunk host `InitializeCriticalSection`), quindi non arriva ancora a MPQ, display, Smacker o menu.

## Modifiche applicate

- `tools/build-entry-x86.sh`: aggiunto `llc -data-sections`. Il test con `storm.optimized.ll` conferma che il backend i686 conserva le sezioni explicit IR `.data..L_data`, `.rodata..L_rdata`, `.rodata..L_text` quando si abilita l'opzione. Ricostruiti gli entry object (`StarCraft` 141 s, `storm` 20 s).
- `runtime/arm/gen_game_layout.py`: la modifica sperimentale di fallback che mappava l'intero `.data` aggregato è stata rimossa. Si usa nuovamente il mapping per sezioni guest dedicate, identico al ramo ARM.
- `tools/build-runtime-x86.sh`: prima di compilare il caller shim i686, le chiamate fixed-arity generate ricevono un function-pointer type `__attribute__((stdcall))`; il caller variadico resta cdecl. Questo evita il doppio pop: la prova macchina mostra `InitializeCriticalSection__c` termina con `ret $4`, mentre il generated `call1` era dichiarato cdecl.

## Layout, build e gate ELF

- `TMPDIR=/dev/shm tools/build-entry-x86.sh` — exit 0.
- `TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh` — exit 0.
- ELF: `build/starcraft-x86` è ELF32 EXEC Intel 80386.
- `readelf -SW`: `.guest_exe_data` a `0x0050C000`, size esatta `0x1d1694`; `.guest_exe_data_2` a `0x006DD694`, size `0x96c`; la prima termina esattamente dove inizia la seconda. Il LOAD è arrotondato a pagina, ma il segmento ELF effettivo non invade la regione PE successiva. Storm `.guest_storm_data` a `0x15059000`, size `0x7fd8`, seguito da `.guest_storm_data_6` a `0x15060fd8`, size `0x28`: `stru_1505BDC0` cade correttamente nel range dati Storm. Nessun overlap logico coi LOAD PE.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102.376 voci, ordinate/univoche, zero collisioni host/guest.

## Diagnosi crash e smoke

- Primo smoke dopo la sola mappatura dati: SIGSEGV host a `0x0E000000`; argomento `InitializeCriticalSection` era corretto `0x1505bdc0` (log ponte precedente riportava argomento errato `0x18333b3c`). `gdb -batch` ha mostrato `EIP=EDI=0x0E000000`, `EBP=0x101cee70` (`loc_1503F6C3`), stack host già senza frame guest utile: l'accesso era un salto verso la base CPU low-arena.
- Il disassembly del thunk API (`0x1802f850`, `InitializeCriticalSection__c`) termina con `ret $4`. Il caller generated `call1` in `build/obj-x86/shim_check/shim_calls.i686.c` chiamava invece attraverso tipo cdecl. L'ipotesi è stata verificata dal cambio deterministico di esito dopo l'annotazione stdcall: la bridge trace ora stampa `InitializeCriticalSection @ 0x101cee70`, arg `0x1505bdc0`, risultato `0x00000001`; il SIGSEGV scompare.
- Smoke corrente (trace): exit **71**, log `build/qa/run-x86-bringup.log`. D19 STOP: target non mappato `0x18001546` (symbol `InitializeCriticalSection`, host bridge thunk), EIP chiamante non reverse-mappato, `[guest ESP]=0x101CEE70`. La sorgente `reasm_indirect_resolve` dovrebbe accettare indirizzi in `[reasm_text_lo=0x18000000,reasm_text_hi=0x1803A065)`, che comprende `0x18001546`; inoltre `reasm_dispatch_init()` è chiamata prima di `storm_loader_init()`. Non è ancora spiegato perché il target venga rifiutato. Lo STOP preserva fail-closed e non va aggirato aggiungendo target/map fittizi.
- GDB completo (run pre-fix) in `build/qa/gdb-x86-bringup.log`; analisi condizionale mirata in `build/qa/gdb-x86-target.log`. Il breakpoint condizionale sul target `0x1505bdc0` non scattò prima del crash, ma ha documentato registri, pila e caller.
- Non raggiunti MPQ, SetDisplayMode, Smacker, menu o FPS; nessun test SDL x11 perché dummy non supera il primo STOP.

## Decisioni autonome

- Conservati i segmenti host `reasm_va_*` fuori dai segmenti dati guest tramite le sezioni dedicate LLASM e `-data-sections`.
- Mantenuto il dispatcher fail-closed; nessun target inventato/stub silenzioso. Nessun git, sotto-agente o input desktop.

## Prossimo passo

Indagare in GDB `reasm_indirect_resolve` al target `0x18001546`: confrontare `g_arm_lo/g_arm_hi`, i flag inizializzati da `reasm_dispatch_init`, argomento effettivo e return path della bridge; aggiungere un breakpoint incondizionato all'ingresso del resolver e acquisire i locals/branch. Correggere solo il difetto x86 dimostrato; ricostruire runtime/link, ripetere dump-map e smoke 15 s. Poi proseguire iterativamente fino a MPQ/display/Smacker/menu; x11 solo dopo un menu dummy.
