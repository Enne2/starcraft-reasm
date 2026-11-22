# Report — link e smoke x86, Passi 3–4 (checkpoint soglia contesto)

## Verifiche completate finora

- Creato `tools/link-x86.sh`. Comando link riuscito: `TMPDIR=/dev/shm tools/link-x86.sh`; usa il `ld.lld` rustup, clang target i686, `-m32 -fno-pie -no-pie`, `-Wl,-m,elf_i386`, `.text=0x18000000`, `-z muldefs`, gli oggetti `entry-x86`, tutti i 35 `build/obj-x86/runtime/*.o`, i 12 `shim-redef/*.o`, e `-lSDL2 -ldl -lpthread -lm -lgcc_s -lc`.
- Aggiunto `build/obj-x86/runtime/shim_calls.o`, necessario per risolvere `arm_shim_caller` referenziato da `bridge_arm.o` e `com_bridge_arm.o`.
- Aggiunto `runtime_bounds.ld` con `reasm_text_lo/hi`. Il link usa una copia `guest_data_link.ld` con `INSERT BEFORE .text`: con `INSERT AFTER .text` LLD falliva con “output file too large” (offset negativo dovuto all'ordine VMA dei segmenti guest e del .text host).
- `LC_ALL=C readelf -h build/starcraft-x86`: `ELF32`, `EXEC (Executable file)`, `Intel 80386`; entry `0x18000000`, interpreter `/lib/ld-linux.so.2`.
- `readelf -lW`: PT_LOAD guest header/data a `0x00400000`–`0x006EC000`; guest code exe da `0x08000000`, Storm code da `0x10000000`, Storm PE data da `0x15001000`–`0x15069000`; port `.text` a `0x18000000` (size `0x3a065`) e aree runtime successive. Nessuna sovrapposizione VMA; il LOAD del guest code ha span ampio `0x08000000`–`0x15045000` per la distanza VMA tra le sezioni.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: `102376` voci, VA ordinate/univoche, `0` entry host coincidenti con VA guest.

## Smoke e diagnosi

Comando eseguito dalla directory indicata, con `timeout 15`; risultato `SMOKE_EXIT=9` (SIGSEGV, non timeout). Log: `build/qa/run-x86-step3.log`. Milestone: runtime inizializzato, 9 regioni PE, header exe/Storm mappati, CPU/TEB/PEB e 327 bridge/32 UNK; avvio del DllMain Storm. Non raggiunge ancora CRT StarCraft/MPQ/menu né FPS.

Con `SC_BRIDGE_TRACE=1` (log `build/qa/run-x86-step3-bridge.log`) il primo bridge è `InitializeCriticalSection @ 0x101cee70`, argomento osservato `0x18333b3c`. Crash handler: SIGSEGV, PC/fault `0x0e000000`, stack guest EIP `0x18333b3c`. GDB ha mostrato `arm_bridge_common_c(cpu=0x0e000000,index=176)`, guest ESP `0x021fff98`, words `[ESP]=0x101cee70`, `[ESP+4]=0x18333b3c`; l'argomento bridge è quindi il contenuto errato, non la continuazione. Il `DllEntryPoint` Storm è `0x1503f6a0`; IDA sola lettura (`storm.orig.dll.i64`) conferma che inizializza quattro critical section a `0x1505bdc0` e poi chiama `sub_1501F710`. Disassembly tradotto `loc_1503F6A0` a `0x101ced70`: immediato `0x18333b3c` scritto nel campo guest ESI e passato come argomento; dovrebbe riferire il dato Storm `stru_1505BDC0` (`0x1505bdc0`).

La causa isolata nel bring-up: `gen_game_layout.py` non riconosceva la sintassi typed-pointer degli alias i686 e, inoltre, le sue sezioni attese `.data..L_data` non esistono negli oggetti x86 (hanno `.data`/`.rodata`). Il layout stampato finora includeva il codice e i gap PE ma non collocava la sezione dati tradotta al VA guest. Ho già aggiornato il regex per alias typed-pointer e `build-runtime-x86.sh` per passare gli `.entry.ll`; build runtime successiva si è fermata correttamente con errore esplicito: `.data..L_data assente o vuota`. Non ho ancora implementato né verificato il mapping delle sezioni `.data`/`.rodata` x86: è il blocco immediato.

## Modifiche già applicate

- Nuovo `tools/link-x86.sh`, con script bounds e riordino linker `INSERT BEFORE .text`.
- `tools/build-runtime-x86.sh`: compila il caller shim i686; passa `entry-x86/*.entry.ll` al layout generator.
- `runtime/arm/gen_game_layout.py`: regex alias esteso per la forma `%__data*` typed-pointer (non ancora sufficiente: formato sezioni da risolvere).
- Generato `build/starcraft-x86` dal link riuscito precedente; smoke e log sopra. Il binario al momento incorpora layout dati errato e non è un candidato valido.
- IDA aperta read-only su `build/lift-full/storm.orig.dll.i64`, sessione `34003ead`; `ida_list` iniziale era vuota, nessuna sessione concorrente toccata.

## Decisioni autonome

- Ho eseguito il link e il test headless richiesti; il fallimento guest non è stato aggirato con stub o dati sintetici. D19 resta rispettato.
- Nessun git, sotto-agente, modifica a `vendor/` o a `runtime/arm/indirect_dispatch.S`; nessun input desktop.

## Prossimo passo

1. Ispezionare solo header/sezioni ELF e alias puntuali dei due `.entry.o/.entry.ll`; adattare `gen_game_layout.py` per mappare correttamente le sezioni i386 `.data`/`.rodata` ai rispettivi VA guest, mantenendo invariato il ramo ARM.
2. Rigenerare runtime e link (`tools/build-runtime-x86.sh`, `TMPDIR=/dev/shm tools/link-x86.sh`); verificare con `readelf -lW` che gli oggetti dati tradotti siano ai VA `0x0050C000` e `0x15059000`, quindi rifare `--dump-map` e lo smoke di 15 secondi.
3. Se emerge un nuovo crash, acquisire log/GDB e consultare IDA in sola lettura dopo controllo sessioni; aggiornare report e STATE con l'esito finale.
