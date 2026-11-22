# Stato attività — PROF-RAM

## Completato
- Analisi read-only dell'ELF corrente `build/starcraft-x86`: `readelf -SW`, `readelf -lW`, `llvm-nm -S --size-sort`; dimensioni sezioni, mapping PT_LOAD e simboli `.bss`/`.data` riportati in `reports/PROF-RAM.md`.
- Creati solo in `build/qa/prof-mem/` il profiler C ELF32, `.so` i386 e wrapper `SC_BIN`. Build verificata con `file`; preload impostato nel wrapper, mai nell'ambiente globale del runner.
- Avvio con `./tools/run-isolated.sh start --id prof-mem --skip-intro 1 --smack 1`. Sessione Xvfb privata `:213`, audio dummy. Main menu/campaign flow percorso tramite control-file; Boot Camp raggiunto e screenshot gameplay con 8 unità selezionate.
- Quattro snapshot `smaps_rollup`/`smaps`/`pmap -x` e profiler signal dumps: Boot/menu iniziale, menu, gameplay, gameplay selezionato. Output e screenshot in `build/qa/prof-mem/captures/`; profilo grezzo in `build/isolated/prof-mem/profiler.bin`.
- Istanza arrestata con `./tools/run-isolated.sh stop --id prof-mem`; `run-isolated.sh list` mostra `alive=0`, `ps -C starcraft-x86` non mostra processi vivi.
- Report finale: `reports/PROF-RAM.md`.

## Esito quantitativo principale
- Boot/menu: VmSize 1,764,552 kB; RSS 130,640 kB; PSS 129,809 kB.
- Menu: VmSize 1,764,552 kB; RSS 130,052 kB; PSS 129,221 kB.
- Gameplay: VmSize 1,743,980 kB; RSS 133,556 kB; PSS 132,725 kB.
- Gameplay con selezione attiva: VmSize 1,743,980 kB; RSS 133,704 kB; PSS 132,873 kB.
- Le circa 998 MiB di riserva `PROT_NONE` dominano VmSize ma non RSS. L'aumento gameplay vs menu è circa 3.7 MiB RSS, principalmente anonimo/private dirty.

## Limiti da mantenere espliciti
- Il profiler è un'implementazione diagnostica locale, non patch di progetto. La hash table puntatori non usa tombstone su `free`; live/peak per callsite possono essere sottostimati dopo collisioni. Conteggi e byte churn sono cumulativi indicativi. Non usare i live bytes del `.bin` come misura affidabile né come prova di risparmio.
- `smaps_rollup` e `pmap` sono la base per la RAM residente; le categorie file-backed calcolate come RSS−Anonymous sono approssimate.
- L'ELF non contiene linker map/sezioni separate per ripartire tutto `.text` fra codice tradotto EXE, Storm, shim/runtime e LLASM; il report separa le tabelle riconosciute e dichiara il resto non attribuito.
- Dati riferiti a un processo e una run Fedora/Xvfb con llvmpipe; driver/backend e cache shader possono cambiare la quota renderer.

## File prodotti
- `build/qa/prof-mem/prof_mem.c`
- `build/qa/prof-mem/prof_mem.so`
- `build/qa/prof-mem/starcraft-prof-mem.sh`
- `build/qa/prof-mem/captures/` (snapshot e immagini)
- `reports/PROF-RAM.md`
- `reports/STATE-prof-mem.md`

Nessun file sorgente, script di progetto o binario `build/starcraft-x86` è stato modificato.
