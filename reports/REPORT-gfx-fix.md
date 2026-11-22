# Report — gfx-fix (parziale)

## Modifiche applicate
- `runtime/shim-port/elf/com_ddraw.c`: sostituito il dispatch di pochi ROP dest-only con valutazione generica del truth table ROP3 per bit, usando pattern/source/destination; supportati 8/16/24/32 bpp, pitch, limiti superficie, rect src/dst, `DDBLT_KEYSRC` e clip-list del clipper.
- `runtime/common/storm_sdraw_arm.c`: aggiunto dump opt-in `SC_SCODE_DUMP` per SCode kernel con `size1=0x4A`, `size2=0x2BC5` (dati e bytecode prima della patch dei salti).
- `runtime/shim-port/elf/smack_elf.c`: aggiunta trace opt-in `SC_SMACK_TRACE` delle chiamate `SmackToBuffer` con frame, dimensioni, destinazione, pitch, coordinate, altezza e flags.

## Verifiche
- `./tools/build.sh --arch x86 --skip-entry`: PASS (un warning preesistente in `input_elf.c` su `host_elf_ticks`).
- `./build/qa/gap-gfx/x86_gfx_test`: PASS; verifica clipper/Blt/BltFast e SetDIBits, **non** esercita i nuovi truth table ROP.
- `./tests/x86_runtime_check/check.sh`: PASS.
- `readelf -h build/starcraft-x86`: ELF32, EXEC, Intel 80386.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: PASS; 102392 voci ordinate/univoche, intervallo guest 00401000–15044C73.
- Avvio diagnostico `./tools/run-isolated.sh` id `gfx-fix`, Xvfb :173 e audio dummy; screenshot letto `reports/gfx-fix-current.png`: Main Menu visibile. Log `SC_SCODE_DUMP=1 SC_SMACK_TRACE=1` ha catturato kernel 0x4A/0x2BC5 e SmackToBuffer. Istanza fermata.

## Limiti / causa radice
Non completata la verifica visiva richiesta: non sono stati acquisiti gli stati hover ai quattro intervalli, campaign-select idle/hover, Select Game Type, Registry e MENU in-game. La traccia Smack catturata riguarda l'avvio/menu, non confronta ritratti idle vs hover né `storm_457`/`storm_334` per quei ritratti. Il dump SCode cattura il kernel corretto per dimensioni ma non è stato confrontato byte-per-byte con una cattura eseguita dall'oracolo originale; quindi non ho una prova per correggere la causa radice dei bug hover e campagne idle. Il nuovo test gfx non prova ancora i ROP generici.

## Decisioni autonome
Scelta conservativa: ho corretto il difetto ROP dimostrabile nel codice e aggiunto diagnostica opt-in; non ho inventato fix per hover/campagne né dichiarato risolti i tre sintomi senza confronto e schermate complete. Il build è valido, ma la missione resta parziale; servono test ROP dedicati e confronto visuale/oracolo per chiuderla.
