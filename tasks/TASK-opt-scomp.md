# Preludio obbligatorio
- Oracolo primario: IDA (`build/lift-full/storm.orig.dll` / copia privata in `build/qa/opt-scomp/storm.orig.dll`).
- **Proprietà esclusiva (NON toccare altri file di progetto):**
  - `runtime/common/storm_scomp.c`
  - `tests/x86_scomp_test/` (`test_scomp.c`, `diff_real.c`, `bench_real.c`, `check.sh`)
- **Vincoli fondamentali:**
  1. **Output 100% bit-exact** rispetto a `@orig_storm_552` su tutti i `4,292` settori reali MPQ (inclusi i `494` settori `0x41`/`0x81`!) + unit test.
  2. **Codice 100% C11 portabile, cross-OS e CPU-agnostic** (niente asm inline, attenzione a endianness/unaligned `memcpy`, niente dipendenze da header OS-specifici).
  3. **NON modificare altri file di `runtime/` o `tools/`**, e NON lanciare altri sotto-agenti.

# Missione: Reimplementazione nativa C11 di Huffman `0x01` / `0x41` / `0x81` in `storm_scomp.c` (`~19-20%` Boot CPU)

Leggi `runtime/common/storm_scomp.c`, `reports/PROF-CPU.md`, `tests/x86_scomp_test/diff_real.c`, `tests/x86_scomp_test/test_scomp.c`.

1. **Analisi oracolo Storm `#552` Huffman (`0x01`, `0x41`, `0x81`):**
   - In `storm.orig.dll` (copia privata in `build/qa/opt-scomp/`), decompila/ispeziona il decoder Huffman usato da `#552 SCompDecompress` (le funzioni attorno a `0x15004990` / `0x15004CC0` / `0x1501C290` / `0x1501FDDF`, più le tabelle Huffman statiche in `.rdata`/`.data` di `storm.orig.dll`!).
   - Verifica esattamente:
     - formato header/tabelle Huffman di Storm (`type` byte `0..7`? tabella pesi predefinita di Storm Huffman?),
     - bit ordering (LSB-first), canonical/adaptive Huffman tree construction, decodifica simboli + eventuali combinazioni `0x41` (`Huffman -> ADPCM mono`) e `0x81` (`Huffman -> ADPCM stereo`) e multi-compression chain di Storm (`cmp_mask` bit-by-bit in ordine inverso o lista di passaggi — guarda come `storm_orig` applica le maschere `0x01`, `0x08`, `0x40`, `0x80`, `0x02`?),
     - eventuali casi limite (buffer output insufficiente, blocco corrotto -> fallback sicuro su `orig_storm_552` o ritorno `0` identico all'originale!).
2. **Implementa Huffman nativo in `runtime/common/storm_scomp.c`:**
   - Scrivi il decoder Huffman Storm in C11 puro, CPU-agnostic e portabile, collegandolo in `storm_552_decompress()` per `0x01`, `0x41`, `0x81` (mantenendo `@orig_storm_552` fallback per maschere sconosciute o eventuali casi non coperti).
3. **Verifica bit-exact & benchmark (`tests/x86_scomp_test/check.sh`):**
   - Estendi `tests/x86_scomp_test/test_scomp.c` per coprire esplicitamente `0x01`, `0x41`, `0x81`.
   - Esegui `./tests/x86_scomp_test/check.sh` (su `4,292` settori reali MPQ!):
     - Obiettivo: **`4,292` identici, `0` differenze, `0` fallback** (i `494` settori `0x41`/`0x81` devono passare tutti in C nativo bit-exact!) + misura speedup A/B su quei `494` settori!
   - Compila ed esegui `test_scomp` anche a 64-bit nativo (`clang` host) per verificare la portabilità CPU-agnostic!
4. **Consegna:**
   - Scrivi `reports/REPORT-opt-scomp.md` e `reports/STATE-opt-scomp.md` (max 70 righe).
