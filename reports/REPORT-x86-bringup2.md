# Report — x86 bring-up 2 (frigate), aggiornamento trace SIGSEGV

## Esito

Build/link i386 riusciti e fix al primo STOP ancora presenti. Lo smoke supera DllMain Storm e stampa il setup risorse, poi crasha prima di MPQ/SetDisplayMode/menu con `SIGSEGV PC=0x18140AEE`. In questo aggiornamento ho eseguito solo diagnosi; nessun nuovo fix applicato.

## Fix già applicati e gate

- `tools/link-x86.sh`: bounds linker separati `.guest_exe_code` e `.guest_storm_code`.
- `runtime/arm/indirect_dispatch.c`: sul solo `__i386__`, accetta continuazioni nei due segmenti tradotti linkati; niente intervallo tra segmenti o target arbitrari.
- `runtime/arm/gen_bridge_stubs.py`: `--arch x86` dà precedenza agli handler TEB guest di `GetLastError`/`SetLastError`; logica ARM invariata.
- `TMPDIR=/dev/shm tools/build-runtime-x86.sh` e `TMPDIR=/dev/shm tools/link-x86.sh`: exit 0. `readelf`: ELF32/EXEC/Intel 80386. `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102376 voci, ordinate/univoche, 0 collisioni.

## Evidenze trace e mappa indirizzi

Trace completa: `build/qa/run-x86-trace.log` (892 righe; `SC_BRIDGE_TRACE=1 REASM_TRACE=1`, driver SDL dummy). Variabili trovate con `rg` in `runtime/arm/` e shim: `SC_BRIDGE_TRACE`, `REASM_TRACE`, `SC_FPS_LOG` e trace specializzati (input/mouse/audio). Sequenza finale:

```text
GetModuleHandleA @ 0x10041a10
  0x18127248
[trace] GetModuleHandleA(kernel32.dll) da 0x180094EE -> 0x7000A100
SIGSEGV PC 0x18140AEE
```

`gdb x/s`: `0x18127248` = `"kernel32.dll"`; `0x1812723c` = `"FlsAlloc"`; `0x18140ADE` inizia `"CurrentProcessId\0GetModuleHandleA..."`, quindi `0x18140AEE` cade nei byte della stringa `GetModuleHandleA` (non in codice eseguibile). `readelf -SW`: `.rodata` host da `0x1803CCF0`, quindi questi indirizzi sono dati read-only.

`llvm-nm -n` mappa la catena dello stack guest come segue:

- `0x1002A120` → `loc_150065B7`
- `0x1002A410` → `loc_15006627`
- `0x1002B6B0` → `loc_1500677B`
- `0x100EB600` → `loc_1501F75E`
- `0x101CF0A0` → `loc_1503F6E0`
- `0x1003A4E0` è un blocco Storm del codice tradotto (da risolvere esattamente col prossimo lookup `llvm-nm -n`)

Disassembly `llvm-objdump -d` e `build/lift-full/storm.llasm` circoscrivono `loc_1500870C` (`0x10041A10`): dopo il ritorno di `GetModuleHandleA`, usa il modulo non nullo, prepara `GetProcAddress` al thunk `0x18001384`, passa il puntatore `0x1812723C` (`"FlsAlloc"`) e salva continuation `0x10041AA0`; poi il flusso dovrebbe entrare nel bridge GetProcAddress. Lo stub a `0x18001384` è `GetProcAddress` nel simbolismo corrente di `llvm-nm` (le etichette del backtrace GDB nel run precedente erano ambigue per unwind corrotto).

La sorgente `runtime/shim-port/elf/win32_shim.c` mostra `GetModuleHandleA`: prova `win32_ui_module_handle(n)`, altrimenti `reasm_elf_GetModuleHandleA(n)`. Il valore tracciato è `0x7000A100`. `GetProcAddress` prova i provider Smack/DirectSound/SNP, UI e infine `reasm_elf_GetProcAddress`. Non c'è una riga `GetProcAddress` nella trace prima del crash.

## GDB

Log: `build/qa/gdb-x86-sigsegv2.log`. Al SIGSEGV: `EIP=0x18140AEE`, `EBX=0x0E000000` (CPU guest), `EBP=0x10041A10` (`loc_1500870C`), `EAX=EDX=0x7000A100`, `ECX=0x6C`, `ESI=0`, `EDI=0x18145DC4`. Stack host in cima: `0x18127248, 0, 0, 0x18000BC1, 0x1802F480 (GetModuleHandleA__c), 0x0E000000`. Il breakpoint automatico su `x86_bridge_common` index `0x83` e su `GetProcAddress__c` non è scattato; il processo arriva al SIGSEGV prima di un ingresso osservato nel bridge GetProcAddress. La stampa GDB del backtrace include frame casuali/corrotti e non viene usata come prova del caller.

## Causa e limiti

La causa immediata è un trasferimento di controllo a byte di stringa in `.rodata`; la causa che mette quel valore nel flusso **non è ancora dimostrata**. Le evidenze restringono il punto a tra il ritorno `GetModuleHandleA(kernel32.dll) -> 0x7000A100`, il successivo blocco `loc_1500870C` e il tentativo previsto a `GetProcAddress("FlsAlloc")`. Non allargare i range del dispatcher e non trattare la stringa come target valido. IDA MCP non era riuscito ad aprire il DB originale né il PE nel turno precedente; per questa diagnosi sono stati usati `llvm-nm`, `llvm-objdump`, `gdb`, `readelf`, `strings`, `rg` mirato e LLASM, senza leggere per intero i `.ll`.

Smoke trace: exit 9, log completo `build/qa/run-x86-trace.log`. Lo smoke non-trace corrispondente è `build/qa/run-x86-bringup2.log`. MPQ, `SetDisplayMode`, Smacker/menu e prova SDL reale non raggiunti; display non è headless, ma il test reale è subordinato al raggiungimento del menu dummy.

## Decisioni autonome

Conservare D19 fail-closed; nessun target inventato, nessun workaround permissivo. Diagnosi limitata a letture, GDB e trace; nessun git, sub-agent, input desktop o file vietato toccato.

## Prossimo passo

Rilanciare GDB dal `build/asset-sandbox` con `SC_BRIDGE_TRACE=1 REASM_TRACE=1` e breakpoints condizionali su `*0x10041A10`, `*0x18001384`, `*0x18000E30` (fermare e stampare solo `EAX==0x83`), `GetProcAddress__c`, `reasm_indirect`, `reasm_indirect_resolve` e SIGSEGV. Al breakpoint `0x10041A10` salvare `cpu->_eax`, `_edi`, `_esp`, le 4 parole guest in cima allo stack e l'indirizzo di ritorno; seguire step-instruction fino al salto a `0x18001384`, verificando se il breakpoint stub scatta. Se non scatta, fermarsi sull'istruzione esatta che cambia EIP; se scatta, ispezionare argomenti di `GetProcAddress__c`, return address host e primo target del resolver. Correlare l'istruzione con `storm.llasm`/`llvm-objdump` e solo dopo applicare un fix mirato; rebuild/link e smoke dummy prima di qualunque test SDL reale.
