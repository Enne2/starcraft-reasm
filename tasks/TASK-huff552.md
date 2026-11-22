# Missione: reimplementazione nativa di SComp Huffman (Storm #552) con parità BYTE-EXACT

## Cosa esiste già (non riscoprirlo)
- `runtime/common/storm_scomp.c`: implementa copy, PKWARE DCL (mask 0x08) e ADPCM (0x40 mono / 0x80 stereo)
  **bit-exact**; per i mask **0x41** e **0x81** restituisce −1 e `storm_552_arm_body` ricade su
  `@orig_storm_552` (codice tradotto originale). `storm_scomp_decompress()` è il punto di ingresso.
- Le maschere sono combinazioni: `0x41 = 0x01|0x40` (Huffman + ADPCM mono), `0x81 = 0x01|0x80`
  (Huffman + ADPCM stereo). Manca **solo** il decoder Huffman (mask 0x01) e la catena Huffman→ADPCM.
- **Set di prova reale già catturato**: `build/qa/scomp-capture/` = 4292 settori MPQ reali con
  `<id>.meta` (mask, src_len, dst_cap, ret, dst_len), `<id>.src` (input) e `<id>.dst` (output **esatto**
  prodotto dall'originale). Ripartizione: 3798 × 0x08, **66 × 0x41**, **428 × 0x81**.
- Harness differenziale pronto: `tests/x86_scomp_test/{diff_real.c,check.sh}` (usa `SC_SCOMP_SAMPLES`).
  Comando: `SC_SCOMP_SAMPLES=$PWD/build/qa/scomp-capture tests/x86_scomp_test/check.sh`
  → oggi stampa `4292 settori, 3798 identici, 0 differenze, 494 fallback`.
- Copia privata del binario per IDA: `build/qa/scomp-capture/storm.orig.private.dll`
  (NON aprire `build/lift-full/*.i64` né sessioni IDA di altri lavori: usa SOLO questa copia).
- Riferimenti IDA già raccolti: `#552` a `0x1501D160`; catena `0x1501D090 → 0x1501CF40 / 0x1501C980`;
  aggiornamento FGK `0x15003E30`; tabella pesi a `0x15050030`; bitstream **LSB-first**.

## Obiettivo
Decoder Huffman nativo, **C11 portabile e CPU-agnostico** (nessun assembly, nessun `#ifdef` di architettura),
che renda i 494 settori **byte-identici** e porti il differenziale a:
`4292 settori, 4292 identici, 0 differenze, 0 fallback, 0 salti`.
Poi integralo in `storm_scomp_decompress()` come catena corretta (Huffman → ADPCM) mantenendo invariati
i percorsi 0x08/0x00/copy/ADPCM già verificati e il fallback su `@orig_storm_552` solo per input malformati.

## Metodo consigliato (oracolo prima di tutto)
1. **Misura il target**: per un settore 0x41/0x81 non sai ancora dove finisca l'output Huffman.
   Trovalo con GDB sulla copia privata del binario dentro il runtime già buildato (`build/starcraft-x86`)
   oppure, meglio, **isola la fase**: metti un breakpoint sull'ingresso della fase ADPCM dentro `#552`
   (la catena `0x1501D090` chiama prima Huffman e poi ADPCM) e **dumpa il buffer intermedio**:
   quello è l'output esatto del decoder Huffman che devi riprodurre. Salva i campioni intermedi in
   `build/qa/scomp-capture/huff-intermediate/` (`.in` = input Huffman, `.out` = output atteso, `.len`).
   In alternativa usa `SC_SCOMP_DUMP=1` per catturare nuovi settori.
2. **Scrivi prima un riferimento in Python** (`build/qa/scomp-capture/huff_ref.py`) e validalo byte-exact
   sui campioni intermedi: è molto più veloce iterare in Python che in C.
3. Parti dalla conoscenza pubblica dell'algoritmo (StormLib `src/huffman/huff.cpp`: albero adattivo FGK,
   tabella dei pesi, bitstream LSB-first, `TBitStream`, `InsertItem`/`UpdateTree`), ma **verifica ogni
   dettaglio sul binario** (tabella pesi `0x15050030`, numero di item, ordine di init, gestione EOS,
   riempimento finale e casi limite). Se StormLib diverge, **ha ragione il binario**: documenta la differenza.
4. Porta il riferimento in C11 (funzioni `static` in `runtime/common/storm_scomp.c`, oppure nuovo
   `runtime/common/storm_huff.c` incluso/compilato — se aggiungi un file, aggiorna `tools/build-runtime-x86.sh`
   **e** `tools/build-runtime-arm.sh` e `tools/build.sh` in modo che entrambi gli arch compilino lo stesso TU).
5. Verifica la catena: Huffman → ADPCM (riusa `adpcm_decode()` già bit-exact) sui 494 settori.

## Vincoli
- Modificare SOLO: `runtime/common/storm_scomp.c` (+ eventuale `storm_huff.c`), i `tools/build-*.sh`
  necessari per includerlo, `tests/x86_scomp_test/*`, e file dentro `build/qa/scomp-capture/`.
  NON toccare `host_elf.c`, `snp_elf.c`, `win32_shim.c`, `storm_sdraw_arm.c`, gli altri test, né `build/starcraft-x86`
  (puoi ricostruirlo con `./tools/build.sh --arch x86 --skip-entry` per la verifica finale).
- Il working tree può contenere modifiche di altre sessioni: non fare `revert`/`stash`/`reset`.
- Nessun byte di gioco nel repository (la capture resta in `build/`, che è git-ignored).
- C11 portabile; compila sia `--target=i686-unknown-linux-gnu` sia host x86-64 (`-std=c11 -Wall -Wextra`).

## Verifiche richieste (tutte da riportare con numeri)
1. `SC_SCOMP_SAMPLES=$PWD/build/qa/scomp-capture tests/x86_scomp_test/check.sh` →
   **4292/4292 identici, 0 differenze, 0 fallback**.
2. `tests/x86_scomp_test/test_scomp` (sintetici) ancora PASS.
3. Benchmark: prima/dopo su tutti i settori (il `bench_real` esiste già); riporta MiB/s e confronto
   con il fallback su `@orig_storm_552` (misura il tempo del percorso originale per i 494).
4. Compilazione i686 e x86-64 del/dei TU: PASS.
5. `./tools/build.sh --arch x86 --skip-entry` PASS e, se la catena viene esercitata a runtime,
   avvio isolato `./tools/run-isolated.sh start --id huff-test --skip-intro 1 --smack 1` + `stop`,
   controllando che nel log **non** compaia nessun `[scomp] fallback`.

## Deliverable
- Patch ai sorgenti + `reports/REPORT-huff552.md` (analisi differenze StormLib/binario, tabella pesi
  documentata con indirizzo, formato bitstream, casi limite, numeri prima/dopo) e `reports/STATE-huff552.md`.
- Se qualcosa resta impossibile, **dichiara esattamente** cosa e perché, con gli indirizzi IDA studiati.
