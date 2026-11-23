# Report — intro ARMv7 nera: scrittura e ordine del present

## Esito
La causa immediata del canvas vuoto è identificata: il percorso video SCode scrive un frame non nero sul canvas; subito dopo il percorso Storm esegue un riempimento full-screen a indice zero. Ho provato a presentare il canvas immediatamente dopo il kernel SCode 2×, ma la verifica RGB accoppiata non è passata: **non dichiaro il fix verificato né committato**.

## Evidenze

- Prima (baseline misurata dall’utente): al present `nz_idx=0`, palette sana; media RGB ARM dell’intro `0`.
- Stessa traccia dopo il blit SCode: `src=0x5580`, `dst=0xB1B9`, canvas/filmrect `0xB1B9` valori non-zero.
- Watchdog one-shot `mprotect` sul canvas, in esecuzione ARM, ha catturato la scrittura successiva: `host_pc=0x18A2CE14`, fault nell’area protetta. `llvm-addr2line`/disassembly del build la mappano a `storm_313_arm_body`, store del pattern ROP.
- Gli argomenti loggati per quel fill sono: `dst=canvas`, `src=0`, `width=0x280`, `rows=0x1E0`, `pattern=0`, `ROP=0x00F00021` — copia del pattern zero sull’intero 640×480.
- Oracolo IDA, database privato `build/lift-full/storm.orig.dll`: `storm_313` @ `0x15036260` contiene il fast path ROP; `storm_388` @ `0x15023210` lo chiama per il fill; `storm_343` @ `0x15035550` invoca `storm_388` (`0x150355E8`). `storm_457` @ `0x1502F500` chiama `storm_343(0)` a `0x1502F769` quando `(flags & 0x200000) != 0 && (flags & 0x800000) == 0`.
- Sequenza osservata: SCode lascia il rettangolo film non-zero, poi il riempimento azzera il canvas prima di un present misurato (`canvas_write_ticks=0xE`, `nz_film_86_393=0`). Questo spiega il present nero senza attribuirlo a palette, riallocazione o fill SCode full-screen.

## Modifica e verifica tentata

È stata aggiunta una presentazione host subito dopo il blit SCode 2× sul canvas. Build ARM riuscita e deploy verificato sul Pinebook; ultimo SHA locale/remoto: `887e5c665e65e89050421bddc82486e04928f09838dae7c9219f3ee004920b5e`.

- Dopo la modifica, `[present-dbg]` ha campionato anche contenuto non-zero durante l’intro, per esempio `nz_idx=0x21F3`, `nz_film_86_393=0xEDB4`, palette `0xEC`; in altri campioni successivi il fill/una transizione porta il canvas a zero.
- Cattura accoppiata richiesta al present successivo al primo frame SCode non-zero: **mean RGB=0, max=0**, anche nel rettangolo film. Quindi non prova l’obiettivo `mean > 0.02`; la cattura può ricadere sul present successivo al fill. Non è evidenza sufficiente per dichiarare risolto.
- Baseline x86 precedente all’ultima modifica: `mean=0.202437`, `max=0.979335`. La build x86 della patch corrente **non passa il link** (`undefined symbol: host_elf_canvas_watch_arm`, chiamata da `storm_sdraw_arm.c`); quindi la regressione x86 non è misurata né certificata.

## Decisioni autonome
Ho scelto di non committare né pushare: il criterio numerico ARM post-fix non è soddisfatto dalla cattura RGB accoppiata e la regressione x86 va ripetuta sulla patch finale. Mantengo la diagnosi del writer come provata, ma considero la correzione della presentazione ancora da verificare.
