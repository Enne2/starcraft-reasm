# STATE — SComp Huffman #552 (mask 0x01) nativo

Data: 2026-11-22 · Progetto: `starcraft-reasm-generic` · Macchina: frigate

## Stato

**COMPLETO** per l'obiettivo byte-exact sui 494 settori 0x41/0x81 catturati dal gioco.

```
diff_real: 4292 settori, 4292 identici, 0 differenze, 0 fallback, 0 salti
  mask 0x08: 3798   mask 0x41: 66   mask 0x81: 428
huff_ref (Python): 494 combinati, 494 identici, 0 differenze, 0 salti
test_scomp: PASS   •   build.sh --arch x86 --skip-entry: PASS
bench: PKWARE 141.25 MiB/s (invariato) · Huffman+ADPCM 53.80 MiB/s (prima: fallback sul liftato)
```

## Cosa è stato fatto

1. IDA sulla copia privata `build/qa/scomp-capture/storm.orig.private.dll` (sessioni IDA degli altri
   lavori non toccate): decompilate `#552` `0x1501D160` e la catena Huffman
   `0x1501D090 → 0x1501CDB0 / 0x1501C980 → 0x1501CF40` (+ `0x15004000`, `0x15003E30`, `0x1501C7F0`),
   più ADPCM `0x1501C0F0`. Funzioni rinominate (`SCompHuff*`, `SCompAdpcmDecode`) e commentate,
   tabelle `0x15050030`, `0x150509C8`, `0x15050948` nominate; database salvato e chiuso.
2. Tabella pesi estratta (`huff_weights.json`, 9×258 byte, SHA256 `40aa56a3…4c05`): identica alle
   `DataDistribution_*` di StormLib → nessuna divergenza da documentare sui pesi.
3. Riferimento Python `build/qa/scomp-capture/huff_ref.py` (FGK + ADPCM) usato come oracolo veloce.
4. Port C11 in `runtime/common/storm_scomp.c`: albero FGK su item indicizzati, lettore LSB-first,
   `huff_decode` con callback, `huff_plain` (0x01) e `huff_then_adpcm` (0x41/0x81, streaming senza
   buffer intermedio). ADPCM rifattorizzato in `adpcm_state`/`adpcm_feed`.
5. **Bug reale trovato**: l'ADPCM preesistente avanzava il canale stereo dopo ogni codice; il binario
   switcha in cima al loop e di nuovo dentro 0x81/0x82 (quindi i controlli non avanzano). Era questa la
   causa dei 397 settori divergenti, non il Huffman. Dettagli e indirizzi nel report.
6. Guardie nuove in `test_scomp.c` (type fuori range, bitstream troncato, catena troncata) e benchmark
   byte-exact in `bench_real.c` per i due gruppi di codec.
7. `SC_SCOMP_TRACE=1` come strumento: stampa `[scomp] fallback …` quando si ricade sul liftato (prima non
   esisteva alcun log). Smoke isolato con la variabile attiva: 0 righe di fallback, gioco a ~127 fps.

## Punti d'ingresso utili

- Decoder nativo: `runtime/common/storm_scomp.c` → `huff_decode`, `huff_build`, `huff_increment`,
  `huff_new_branch`, `huff_plain`, `huff_then_adpcm`, `adpcm_feed`.
- Dispatcher: `storm_scomp_decompress` (mask 0x00 copy, 0x01 Huffman, 0x08 PKWARE, 0x40/0x80 ADPCM,
  0x41/0x81 Huffman→ADPCM; tutto il resto → `-1` → `@orig_storm_552`).
- Verifica: `SC_SCOMP_SAMPLES=$PWD/build/qa/scomp-capture tests/x86_scomp_test/check.sh`.
- Oracolo Python: `python3 build/qa/scomp-capture/huff_ref.py`.

## Limiti noti / prossimi passi

- Tempo del percorso originale (liftato) non misurato: non linkabile nell'harness standalone.
- Controllo positivo della traccia `[scomp] fallback` non eseguito (avrebbe richiesto una build forzata).
- Maschere combinate non presenti nel corpus (0x09, 0x48, 0x49, 0x89, …) restano sul fallback.
- Tipi Huffman 0…6 implementati ma non esercitati (StarCraft usa solo 7 e 8): la regola di update
  sparse viene dal binario, non da dati reali.
- Nessun commit creato (solo working tree). File modificati: `runtime/common/storm_scomp.c`,
  `tests/x86_scomp_test/{test_scomp.c,bench_real.c}`, più `build/qa/scomp-capture/{huff_ref.py,
  huff_weights.json,storm.orig.private.dll.i64}` (git-ignored) e i due report.
