# Report — SComp Huffman #552

## Stato finale
**Non completato: non è stato possibile validare un decoder Huffman C11 bit-exact.** Il percorso nativo per `0x01`, `0x41`, `0x81` è disabilitato e restituisce `-1`, così `storm_552_arm_body` usa il fallback esatto `orig_storm_552`. Il prototipo FGK resta nel file ma non è raggiungibile per queste maschere; non va riattivato né considerato bit-exact.

Il tentativo isolato di riattivazione è stato lanciato su un singolo campione `0x81` con timeout di 2 s: è scaduto senza terminare. Il decoder prototipale non è quindi adatto nemmeno al batch differenziale; il fallback è stato mantenuto. Non sono state fatte modifiche al sorgente in questo follow-up.

## Oracolo / dati
Decompilati nel database IDA `build/lift-full/storm.orig.dll.i64`: `0x1501CDB0`, `0x1501C980`, `0x1501CF40`, `0x15004000`, `0x15003E30`, `0x1501C7F0`, `0x1501D090` (quest'ultimo pseudocodice segnala uno stack non ricostruito e va letto con cautela).

Evidenze principali:
- `0x1501CF40` estrae il tipo dalla parola di bit LSB-first, costruisce l'albero e tratta `257` come escape, `256` come terminatore. Il flag di adattività è `type == 0`; l'aggiornamento avviene dopo l'escape e dopo ogni simbolo quando tale flag è attivo.
- `0x15004000` usa una lookup di 7 bit (127), prosegue il cammino nell'albero per codici più lunghi e aggiorna la cache. Il prototipo C segue un bit alla volta, ma ciò da solo non garantisce la stessa struttura/tie-breaking dell'albero.
- `0x1501C7F0` crea il nuovo simbolo e il leaf NYT e chiama `0x15003E30`; `0x15003E30` incrementa il nodo e gli antenati e può scambiare nodi nella lista/albero.
- `0x1501C980` inizializza la lista dei pesi dalla tabella `0x15050030 + 258*type`, quindi fonde i primi due elementi della lista come left/right e reinserisce il parent secondo il peso. L'ordine/tie-breaking non è equivalente al semplice ordinamento canonico.
- La tabella è nel PE `.rdata` (VA `0x15045000`, raw `0x45000`); `type 7/8` iniziano ai raw offset `0x5073E/0x50840`. Le 258 entry sono state verificate direttamente nel DLL. I campioni `0x41/0x81` iniziano con Huffman type `8` (`src` dopo la maschera: `08...`).

## Verifica finale
`SC_SCOMP_SAMPLES=/tmp/scomp-real-now ./tests/x86_scomp_test/check.sh`: PASS; **4.292 settori, 3.798 identici, 0 differenze, 494 fallback, 0 salti** (`0x08`: 3.798, `0x41`: 66, `0x81`: 428). L'obiettivo di zero fallback non è raggiunto.

Benchmark corrente del solo percorso PKWARE `0x08`: 3.798 settori; 5 run, mediana 0,085717 s, min 0,084648 s, max 0,086700 s, throughput mediano 143,68 MiB/s. Nessun A/B valido sui 494 settori, poiché il decoder nativo non termina in sicurezza.

Compilazione test C11 con clang x86-64: PASS (inclusa nella suite). Compilazione `clang -m32 -O2 -Wall -Wextra -DSC_SCOMP_TEST_ONLY ... -c runtime/common/storm_scomp.c`: PASS; oggetto ELF 32-bit i386.

Nessuna modifica al sorgente, preservato il fallback. Il database IDA riaperto per questa analisi è `build/lift-full/storm.orig.dll.i64`.

## Decisioni autonome
Ho scelto di mantenere il fallback liftato per ogni maschera Huffman finché la corrispondenza byte-per-byte non sia dimostrata; il campione isolato che non termina rende non sicura la riattivazione del prototipo. Nessuno speedup Huffman è rivendicato.
