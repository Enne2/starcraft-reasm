# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, costanti, convenzioni o indirizzi del binario originale, l'oracolo primario sono i database IDA del progetto (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`). Prima di usare tool IDA controlla sempre `ida_list` e NON toccare sessioni IDA aperte per altri lavori.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/` né file fuori dalla tua proprietà.
   - NON leggere per intero `build/lift-full/StarCraft.ll` (77 MiB) o `storm.ll` (21 MiB): usa solo `head -n 30` / `grep` / `tools/dispatch-ir.py` / `opt` / `llc`.

---

# Missione `scx86-ir`: Passo 1 — Verifica `fastcc`+`musttail` su `i686` e build degli oggetti IR

Leggi `PLAN-MULTIARCH.md` (sezioni 3, 4, 6 Passo 1) e `tools/build-entry-obj.sh`.

## File di proprietà (esclusivi di questo task)
- `build/layout-i686.ll`
- `tests/x86_fastcc_check/` (nuovo micro-test sintetico)
- `tools/build-entry-x86.sh` (nuovo script per x86-32, ispirato a `tools/build-entry-obj.sh`)
- `build/lift-full/entry-x86/` (directory output oggetti x86)
- `reports/REPORT-x86-step1.md`
- `reports/STATE-x86-ir.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Derivazione datalayout `i686-unknown-linux-gnu`**:
   - Esegui `clang --target=i686-unknown-linux-gnu -m32 -S -emit-llvm -x c /dev/null -o build/layout-i686.ll`.
   - Confronta il `target datalayout` risultante con `e-p:32:32-f64:32:64-n32` e con le prime 30 righe di `build/lift-full/StarCraft.ll` e `build/lift-full/storm.ll`.

2. **Micro-test obbligatorio `fastcc` + `musttail` su `i686` (`tests/x86_fastcc_check/`)**:
   - Scrivi un piccolo test sintetico con:
     - `%_cpu = type { [36 x i32] }` (144 byte come in `dispatch-ir.py`, o usa esattamente il layout di `StarCraft.entry.ll`),
     - una catena di funzioni `fastcc void @fnA(%_cpu* %cpu)` → `musttail call fastcc void @fnB(%_cpu* %cpu)` → `musttail call fastcc void @asm_trampoline(%_cpu* %cpu)`,
     - un caller C (`cdecl`, compilato con `clang -m32 -fno-pie -no-pie`) e uno stub assembly i386 che stampa/verifica il registro/stack in cui arriva `%cpu`, chiama una funzione C `cdecl`, ripristina `%cpu` e fa `jmp *%eax` verso una destinazione `fastcc`, ritornando infine al caller C senza corrompere lo stack.
   - Compila con `opt` + `llc -mtriple=i686-unknown-linux-gnu -relocation-model=static -filetype=obj`, linka ed esegui **nativamente su `frigate`**.
   - Ispeziona con `llvm-objdump -d` e documenta con precisione il contratto ABI verificato (quale registro riceve `%_cpu*` in `fastcc` su LLVM 22.1.8 `i686`, dove sta il return address, quali registri sono callee-saved vs caller-saved, e che `musttail` emetta `jmp` senza allocare frame).

3. **Build degli oggetti IR (`tools/build-entry-x86.sh` -> `build/lift-full/entry-x86/`)**:
   - Studia `tools/build-entry-obj.sh` e `tools/dispatch-ir.py` (head/lettura mirata).
   - Crea `tools/build-entry-x86.sh` (non toccare `tools/build-entry-obj.sh`!). Assicurati che:
     - generi gli `.entry.ll` con `tools/dispatch-ir.py` in `build/lift-full/entry-x86/` (riusa la logica/flag di `build-entry-obj.sh` ma con triple `i686-unknown-linux-gnu`, data-layout derivato da `clang --target=i686-unknown-linux-gnu -m32`, `-relocation-model=static` e nessuna flag ARM),
     - esegua `opt -passes=verify` e ottimizzazione (`-passes='default<O1>'` o `<O2>` come `build-entry-obj.sh`, documentando la scelta e il tempo),
     - compili con `llc -mtriple=i686-unknown-linux-gnu -relocation-model=static -filetype=obj` producendo `StarCraft.entry.o` e `storm.entry.o` (e gli eventuali `.o` ausiliari previsti da `build-entry-obj.sh`, es. tabelle dispatch/ordinali/data se separati!).
   - Verifica i prodotti con `readelf -h` (`ELF32`, `Intel 80386`, `REL`), `llvm-nm` (simboli chiave presenti) e `llvm-objdump -d` su almeno due funzioni (es. `loc_404C21` / `sc_loc_404C21_impl` o simboli reali presenti) confermando `jmp` per `musttail` e passaggio `%_cpu*` coerente col micro-test.

4. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-step1.md` con: contratto ABI `fastcc` verificato, output di `readelf -h` e disassemblato campione, comandi esatti, tempi/dimensioni `.o`, `## Decisioni autonome`, `## Prossimo passo`.
   - Scrivi `reports/STATE-x86-ir.md` (max 70 righe, formato standard: Fatto e verificato, File di proprietà, Comandi che funzionano, Aperto / prossimo passo, Trappole imparate).

## Gate di accettazione (oggettivi)
1. `tests/x86_fastcc_check` esegue nativamente su `frigate` con exit code `0` e dimostra il contratto `fastcc` + `musttail` (`jmp` + registro `%_cpu*` verificato da `llvm-objdump -d`).
2. `tools/build-entry-x86.sh` termina con exit code `0` e produce in `build/lift-full/entry-x86/` oggetti ELF32 i386 validi (`readelf -h` = `ELF32` / `Intel 80386`) per `StarCraft` e `storm`.
3. `reports/REPORT-x86-step1.md` e `reports/STATE-x86-ir.md` presenti e completi.
