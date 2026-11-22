# REPORT — SComp Huffman (#552, mask 0x01) nativo, parità byte-exact

Data: 2026-11-22 · Macchina: frigate (Fedora Linux 44, AMD Ryzen 7 H 255 / Radeon 780M) · Progetto: `starcraft-reasm-generic`

## 1. Risultato

| Metrica | Prima | Dopo |
|---|---|---|
| `diff_real` (4292 settori reali) | 3798 identici, **494 fallback**, 0 diff | **4292 identici, 0 fallback, 0 differenze, 0 salti** |
| Settori 0x41 (Huffman+ADPCM mono) | 66 in fallback | 66 nativi identici |
| Settori 0x81 (Huffman+ADPCM stereo) | 428 in fallback | 428 nativi identici |
| Settori 0x08 (PKWARE) | 3798 nativi | 3798 nativi (invariato) |
| Catena Huffman→ADPCM | assente | nativa, streaming, senza buffer intermedio |

Verifica di riferimento (comando richiesto):

```
SC_SCOMP_SAMPLES=$PWD/build/qa/scomp-capture tests/x86_scomp_test/check.sh
x86_scomp_test: PASS (copy paths + synthetic ADPCM + Huffman guards; unsupported codecs use lifted fallback)
diff_real: 4292 settori, 4292 identici, 0 differenze, 0 fallback, 0 salti
  mask 0x08: 3798
  mask 0x41: 66
  mask 0x81: 428
bench_real: codec=PKWARE mask=0x08 sectors=3798 output=12.315 MiB; 5 runs (seconds): median=0.087187 min=0.085068 max=0.090219 span=0.005151; median throughput=141.25 MiB/s
bench_real: codec=Huffman+ADPCM mask=0x41/0x81 sectors=494 output=1.930 MiB; 5 runs (seconds): median=0.035869 min=0.035565 max=0.036088 span=0.000524; median throughput=53.80 MiB/s
```

Riferimento Python indipendente (`build/qa/scomp-capture/huff_ref.py`, oracolo di sviluppo):

```
huff_ref: 494 combinati, 494 identici, 0 differenze, 0 salti
```

## 2. Metodo (oracolo prima di tutto)

Nessun GDB: come concordato, l'oracolo è stato la **composizione** `Huffman(src) → ADPCM(·) == <id>.dst`
sui 494 settori reali, con l'ADPCM già verificato. Il buffer intermedio non è servito perché è risultato
corretto fin dal primo tentativo: l'errore era nell'ADPCM (vedi §5).

Ordine di lavoro:
1. IDA (solo copia privata `build/qa/scomp-capture/storm.orig.private.dll`) su `#552` e sui cinque
   helper Huffman; dump della tabella pesi `0x15050030`.
2. Riferimento Python (`huff_ref.py`) con albero FGK portato da StormLib `src/huffman/huff.cpp` e
   tabelle reali del binario → 97/494 identici: il Huffman era già corretto, l'ADPCM no.
3. Correzione della semantica di canale ADPCM (§5) → 494/494 in Python.
4. Port in C11 nello stesso TU `runtime/common/storm_scomp.c` (nessun nuovo file, nessuna modifica agli
   script di build) → 4292/4292.

## 3. Mappa IDA del codec Huffman

Tutti gli indirizzi sono VA della copia privata (imagebase 0x15000000). Funzioni rinominate e annotate
nel database `build/qa/scomp-capture/storm.orig.private.dll.i64` (salvato).

| Funzione | Indirizzo | Ruolo |
|---|---|---|
| `SCompHuffDecompress` | `0x1501D090` | entry della fase 0x01: reset albero, build, decode di un settore |
| `SCompHuffResetTree` | `0x1501CDB0` | azzera 515 item (24 byte l'uno) e i due list head (+12372, +12384), `MinValidValue = 1` |
| `SCompHuffBuildTree` | `0x1501C980` | costruisce l'albero dai pesi `0x15050030 + 258*type`, aggiunge EOS(256)/NYT(257) e i nodi interni |
| `SCompHuffDecodeOneByte` | `0x15004000` | decode di un simbolo con quick-link a 7 bit (cache, solo ottimizzazione) |
| `SCompHuffDecodeStream` | `0x1501CF40` | loop di decodifica: type byte, EOS/NYT, stop a `dst_cap` |
| `SCompHuffIncWeights` | `0x15003E30` | aggiornamento FGK (peso +1, riordino lista, swap di sottoalberi) |
| `SCompHuffInsertNewBranch` | `0x1501C7F0` | sostituisce la foglia NYT con il ramo (vecchio NYT + byte nuovo) |
| `SCompAdpcmDecode` | `0x1501C0F0` | ADPCM (già portato; qui è stata corretta la semantica di canale) |

Dispatcher `#552` (`0x1501D160`), tabella inversa `0x15050B4C..0x15050B6B` percorsa **verso il basso**:
ordine di applicazione **0x08 → 0x01 → 0x80 → 0x40**. Quindi `0x41 = 0x01|0x40` = Huffman → ADPCM mono,
`0x81 = 0x01|0x80` = Huffman → ADPCM stereo. Ogni fase riceve `dst_cap` = capacità originale del
chiamante (`v19 = v20 = *a2`, `0x1501D2B3`), non la lunghezza corrente.

## 4. Tabella dei pesi `0x15050030`

9 righe × 258 byte (= 2322 byte), indicizzate dal **primo byte del payload** (data type). Il builder legge
`&unk_15050030 + 258*a2` (`0x1501CA28`) e consuma solo le prime 256 entry; gli ultimi 2 byte di ogni riga
sono `0, 0`.

- Dump completo: `build/qa/scomp-capture/huff_weights.json` (estratto con IDAPython `get_bytes`).
- `SHA256` della tabella concatenata (2322 byte): `40aa56a35549261f51545f334eedeffa6188ecfc3ed04f4ff83faf83b9184c05`
- Somma per riga (type 0…8): `12, 1009, 1028, 782, 1553, 4550, 2917, 5562, 9949`

**Confronto con StormLib**: le nove `DataDistribution_*` di `src/huffman/huff.cpp` (Sparse, Binary, Text,
General, ADPCM_4, ADPCM_6, Stereo_3, Stereo_4, Stereo_5) sono state confrontate byte per byte con le
righe del binario: **0 differenze su 9×256 byte**. Nessuna divergenza da documentare sui pesi.

I settori catturati usano solo `type = 8` (487) e `type = 7` (7); il codice C porta comunque tutte e 9 le
righe e rifiuta `type > 8` (nel binario non c'è bounds check: `0x1501C980` indicizza oltre la tabella —
nostro comportamento: `return -1` → fallback sul liftato, nessun OOB).

## 5. Il vero bug: avanzamento di canale ADPCM (non Huffman)

L'implementazione ADPCM preesistente aggiornava il canale stereo con un toggle dopo **ogni** codice:
`if (channels == 2) channel ^= 1;`. Il binario fa diversamente:

- switch **all'inizio di ogni iterazione** (`0x1501C163`: `if (channels == 2) channel = !channel`);
- **un secondo switch dentro i rami 0x81 e 0x82** (`0x1501C1A2` per 0x81 via `LABEL_13`, `0x1501C1AF`
  per 0x82), quindi i due switch si annullano: **i controlli 0x81/0x82 non avanzano il canale**;
- `0x80` (hold) e tutti i codici dati (0x00..0x7F) avanzano il canale;
- i codici 0x83..0xFF non hanno switch interno, quindi avanzano come i dati;
- l'indice iniziale è `channels - 1` (`0x1501C150`), così il primo codice va al canale 0.

Effetto osservato sul settore `00000094` (mask 0x81, type 8): i primi due codici della traccia reale sono
`0x81, 0x5E`; con il toggle ingenuo il secondo `0x81` finiva sul canale 1 e il settore divergeva dal byte 4
dell'output. Con la semantica esatta: 494/494.

I settori grezzi 0x40/0x80 del corpus **non** contengono questa combinazione, per questo il difetto non era
emerso nella verifica precedente (l'ADPCM "già bit-exact" lo era solo sui dati che aveva visto).

## 6. Formato del bitstream Huffman (verificato)

- `payload[0]` = data type (0…8); i bit iniziano da `payload[1]`, **LSB-first**.
- Il binario riempie un buffer a 16 bit (`0x1501CF5E-0x1501CF76`: `buf |= *(uint16*)ptr << count; count += 16;
  ptr += 2`) e consuma 8 bit per volta con `count` che può restare negativo; il port usa un lettore a byte
  (`huff_get1`/`huff_get8`) — **equivalente** perché l'ordine dei bit è identico.
- Simboli: `0..255` letterali, `256` = EOS, `257` = NYT seguito dal byte grezzo negli 8 bit successivi.
- Stop: EOS, oppure `dst_cap` byte prodotti (`0x1501CF4F`, il check `--a4 == 0` avviene dopo l'emissione).
- **Regola di aggiornamento pesi** (è la parte che più spesso si sbaglia portando FGK da altre fonti):
  `0x1501CFAC` salva `*tree = (type == 0)`; dopo un escape l'update è guardato da `*tree == 0`
  (`0x1501D009`), dopo un simbolo normale da `*tree != 0` (`0x1501D037`). Conseguenza: per i tipi
  non-sparse (7, 8) **i pesi si aggiornano solo agli escape**, esattamente come fa `THuffmannTree::Decompress`
  di StormLib. Una variante "aggiorna sempre" produce 397 settori sbagliati.
- Il quick-link a 7 bit di `0x15004000` è una cache pura: viene invalidata confrontando `MinValidValue`
  (tree+4, incrementata da `0x15003E30`), quindi il port cammina l'albero direttamente e ottiene gli
  stessi simboli.

## 7. Implementazione C11 (CPU-agnostica)

Tutto in `runtime/common/storm_scomp.c` (nessun nuovo file ⇒ `tools/build-*.sh` invariati):

- `huff_weights[9][258]`: tabella del binario (riga per riga, con l'indirizzo citato nel commento).
- `huff_tree` (515 item, `items[258]`, list head sentinella) + `huff_unlink`, `huff_insert_after`,
  `huff_higher`, `huff_new_item`, `huff_fixup`, `huff_increment`, `huff_build`, `huff_new_branch`:
  port 1:1 di `0x15003E30`/`0x1501C980`/`0x1501C7F0` = `RemoveItem/LinkTwoItems/FindHigherOrEqualItem/
  FixupItemPosByWeight/IncWeightsAndRebalance/InsertNewBranchAndRebalance` di StormLib.
  `ItemsUsed < 515` (HUFF_ITEM_COUNT) come StormLib.
- `huff_bits`, `huff_get1`, `huff_get8`: lettore LSB-first.
- `huff_decode(payload, len, cap, push, ctx, &out_len)`: loop di `0x1501CF40`, output tramite callback.
- `huff_plain()`: mask 0x01 (sink diretto su `dst`).
- `huff_then_adpcm()`: mask 0x41/0x81 — i byte Huffman vengono **alimentati in streaming** alla macchina
  a stati `adpcm_state` (nessun buffer intermedio, nessuna dimensione massima arbitraria). Il risultato è
  identico a materializzare l'intermedio (verificato sui 494).
- ADPCM rifattorizzato in `adpcm_state`/`adpcm_init`/`adpcm_emit`/`adpcm_feed` con la semantica di §5;
  `adpcm_decode()` per i mask 0x40/0x80 resta e usa lo stesso `adpcm_feed`.
- Dispatcher `storm_scomp_decompress()`: invariati copy/0x00/0x08/0x40/0x80; aggiunti 0x01, 0x41, 0x81.
  `storm_552_arm_body()` ora prova il nativo anche per 0x01/0x41/0x81 (e per 0x08, che prima non era
  nell'elenco: la maschera 0x08 passava dal nativo solo perché 0x08 non era filtrata… ora è esplicita).
- Guardia: `type > 8` o bitstream troncato ⇒ `-1` ⇒ fallback su `@orig_storm_552` (input malformati).
- Nuova strumentazione: `SC_SCOMP_TRACE=1` stampa `[scomp] fallback mask=0x.. src_len=.. dst_cap=..` su
  stderr quando una chiamata ricade sul liftato (prima non esisteva alcun log: la verifica "nessun
  fallback nel log" non era misurabile).

Memoria statica: `huff_tree` (~12.5 KiB) in `static`, come i `static pkw_huff` già presenti; il runtime e
l'harness sono single-thread e l'originale riusa lo stesso blocco da 13.7 KiB per ogni chiamata.

## 8. Benchmark

`bench_real` ora verifica anche i **byte** (non solo lunghezza/ret) e misura i due gruppi separatamente,
5 run, mediana (frigate, clang -O2):

| Codec | Settori | Output | Mediana | Throughput |
|---|---|---|---|---|
| PKWARE 0x08 (invariato) | 3798 | 12.315 MiB | 0.087187 s | 141.25 MiB/s |
| Huffman+ADPCM 0x41/0x81 (nuovo) | 494 | 1.930 MiB | 0.035869 s | 53.80 MiB/s |

Baseline dichiarata prima della modifica (memoria condivisa, stesso corpus): PKWARE mediana 0.087730 s,
140.38 MiB/s → invariato entro il rumore.

**Limite dichiarato**: il tempo del percorso di fallback (`arm_guest_call_fn` → `orig_storm_552` tradotto)
non è misurabile nell'harness standalone, perché il liftato vive solo dentro il runtime guest
(`build/starcraft-x86`) e non è linkabile nel test differenziale. Prima della modifica quei 494 settori
eseguivano una traduzione completa della routine originale (~10³–10⁴ operazioni lifted per settore) più il
costo di `arm_guest_call_fn`; ora sono 494 chiamate native. La cifra "prima" equivalente non è quindi un
numero misurato ma un'eliminazione di 494 transizioni guest per avvio.

## 9. Compilazioni

| Target | Comando | Esito |
|---|---|---|
| i686 (come `tools/build-runtime-x86.sh`) | `clang --target=i686-unknown-linux-gnu -m32 -fno-pie -std=c11 -O2 -Wall -Wextra -c runtime/common/storm_scomp.c` | PASS, 0 warning |
| host x86-64, harness | `clang/gcc -std=c11 -O2 -Wall -Wextra -DSC_SCOMP_TEST_ONLY -c runtime/common/storm_scomp.c` | PASS, 0 warning |
| ARMv6 (come `tools/build-runtime-arm.sh`) | `clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm -O1 -Wall -Wextra -c …` | PASS (`ELF 32-bit LSB relocatable, ARM`) |
| runtime x86 completo | `./tools/build.sh --arch x86 --skip-entry` | PASS (`creato: build/starcraft-x86`) |

Nota: la compilazione host x86-64 **senza** `-DSC_SCOMP_TEST_ONLY` non compila, ma è preesistente e non
dipende da questa modifica: `runtime/common/cpu_llasm.h:53` ha `_Static_assert(offsetof(_cpu,_ind_target)==136)`
che vale solo per il layout a 32 bit (verificato compilando la versione `git show HEAD:` dello stesso file:
stessi errori). Il TU runtime è costruito solo con `--target=i686-unknown-linux-gnu -m32`.

ARM end-to-end non eseguito: `build/sysroot` non è presente in questo workspace (stage 1 estrae da
un'immagine Raspberry Pi OS). È stata verificata la compilazione del TU con le stesse flag dello stage 4.

## 10. Smoke test runtime

```
SC_SCOMP_TRACE=1 ./tools/run-isolated.sh start --id huff-test2 --skip-intro 1 --smack 1
  → display=:268, game alive, 73 righe [fps] (~124-130 fps)
grep -c scomp build/isolated/huff-test2/game.log → 0        # nessun "[scomp] fallback"
./tools/run-isolated.sh stop --id huff-test2 → stopped
```

`/proc/<pid>/environ` del processo di gioco conferma `SC_SCOMP_TRACE=1`: la traccia era attiva, e non ha
stampato nulla. Il meccanismo di traccia non è stato validato con un controllo positivo (avrebbe richiesto
di forzare un fallback nella build); è verificato per ispezione del codice e per presenza del simbolo nel
binario (`trace_fallback`, chiamata nel ramo `result < 0` di `storm_552_arm_body`).

## 11. File toccati

- `runtime/common/storm_scomp.c` — decoder Huffman, catena Huffman→ADPCM streaming, fix canale ADPCM,
  nuovi percorsi nel dispatcher e in `storm_552_arm_body`, traccia `SC_SCOMP_TRACE`.
- `tests/x86_scomp_test/test_scomp.c` — tre guardie nuove (type fuori range, bitstream troncato, catena
  troncata) + messaggio di PASS aggiornato.
- `tests/x86_scomp_test/bench_real.c` — benchmark dei due gruppi di codec, verifica byte-exact per run.
- `build/qa/scomp-capture/huff_ref.py`, `huff_weights.json` — oracolo Python e dump della tabella pesi
  (in `build/`, git-ignored: nessun byte di gioco nel repository).
- `build/qa/scomp-capture/storm.orig.private.dll.i64` — database IDA con rename/commenti (in `build/`).

Nessuna modifica a `host_elf.c`, `snp_elf.c`, `win32_shim.c`, `storm_sdraw_arm.c`, agli altri test o agli
script di build. Nessun commit, nessun `revert`/`stash`/`reset`.

## 12. Cosa resta aperto

1. **Tempo del percorso originale** (punto 3 della missione): non misurabile nell'harness (§8).
2. **Controllo positivo della traccia di fallback**: non eseguito (§10).
3. **Maschere combinate non presenti nel corpus** (es. 0x09, 0x48, 0x49, 0x89 …): la catena nativa copre
   0x01/0x41/0x81; per qualsiasi altra combinazione si ricade intenzionalmente sul liftato. Se in futuro
   servisse 0x09 (PKWARE+Huffman) la catena si può estendere riusando `pkw_explode` e `huff_plain`, ma
   l'ordine e la capacità delle fasi andrebbero verificati su un settore reale: oggi non ci sono campioni.
4. **Type 0 (sparse)** e tipi 0…6: implementati ma non esercitati da alcun settore reale (StarCraft usa
   solo 7 e 8). La regola di update specifica per lo sparse è presa dal binario, non validata su dati.
