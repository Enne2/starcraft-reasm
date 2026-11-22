# Preludio obbligatorio
- Oracolo primario: `build/lift-full/storm.orig.dll` (`build/lift-full/storm.orig.dll.i64` già esistente!).
- **Proprietà esclusiva:** `runtime/common/storm_scomp.c`, `tests/x86_scomp_test/`.
- **Vincoli:** 100% C11 portabile, CPU-agnostic, cross-OS, **0 differenze su tutti i 4,292 settori reali MPQ**! NON lanciare altri sottoagenti.

# Missione: Completamento Huffman nativo `0x01`/`0x41`/`0x81` in `storm_scomp.c`

Leggi `reports/REPORT-opt-scomp.md` e `runtime/common/storm_scomp.c`.
L'agente precedente ha già scritto lo scheletro FGK in `storm_scomp.c` e mappato gli indirizzi esatti in `storm.orig.dll`:
- `0x1501D160` (`#552` chain `0x08 -> 0x01 -> 0x80 -> 0x40`)
- `0x1501D090`, `0x1501CDB0`, `0x1501C980`, `0x1501CF40`, `0x15004000`, `0x15003E30`, `0x1501C7F0`
- Tabella pesi a `0x15050030` (file offset `0x45030` + `258*type`, o `0x50030` file offset — verifica in `readelf`/PE section `.rdata`!).

1. Decompila in IDA (`build/lift-full/storm.orig.dll.i64`) ESATTAMENTE `0x1501CDB0`, `0x1501C980`, `0x1501CF40`, `0x15004000`, `0x15003E30`, `0x1501C7F0`, `0x1501D090` e confronta **linea per linea** col prototipo FGK già presente in `runtime/common/storm_scomp.c` (soprattutto: ordine nodi nella lista iniziale `0x1501C980`, ordine figli left/right, `type == 0` raw mode / byte bit-order in `0x1501CF40`, aggiornamento `0x15003E30` dopo ogni byte, gestione `type` > 0 e multi-pass `0x41`/`0x81`!).
2. Correggi il decoder FGK in `runtime/common/storm_scomp.c` finché **`SC_SCOMP_SAMPLES=/tmp/scomp-real-now ./tests/x86_scomp_test/check.sh`** produce **`4292` identici, `0` differenze, `0` fallback**!
   - *Nota di igiene:* se durante i test sul prototipo un singolo settore diverge, testa prima sul singolo file (`diff_real` o piccolo test mirato con timeout 2s) prima di lanciare tutti i 494! E se a fine budget qualche caso limite non fosse identico, NON lasciare codice morto divergente: pulisci o lascia fallback sicuro.
3. Misura speedup A/B sui `494` settori (`0x41`/`0x81`), verifica compilazione C11 64-bit host + `-m32`, e aggiorna `reports/REPORT-opt-scomp.md` e `reports/STATE-opt-scomp.md`.
