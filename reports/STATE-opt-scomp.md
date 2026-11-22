# Stato — opt-scomp

- Huffman nativo: **non validato e disabilitato**; `0x01/0x41/0x81` ritornano `-1` e ricadono su `orig_storm_552`. Riattivazione provata su un singolo campione `0x81`: timeout a 2 s; prototipo da non usare.
- `SC_SCOMP_SAMPLES=/tmp/scomp-real-now ./tests/x86_scomp_test/check.sh`: PASS, 4.292 settori, 3.798 identici, 0 differenze, 494 fallback (66 `0x41`, 428 `0x81`), 0 salti.
- Benchmark PKWARE soltanto: 3.798 settori, mediana 0,085717 s, throughput mediano 143,68 MiB/s (5 run). Nessun A/B affidabile sui 494.
- Clang x86-64 suite: PASS; compilazione `-m32` di `storm_scomp.c`: PASS, oggetto ELF i386. Questo non valida Huffman.
- Decompilati in IDA `0x1501CDB0`, `0x1501C980`, `0x1501CF40`, `0x15004000`, `0x15003E30`, `0x1501C7F0`, `0x1501D090`. Tabella pesi `0x15050030 + 258*type`; type 8 campioni `0x41/0x81`; ordine lista/tie-breaking e aggiornamento FGK richiedono replica esatta.
- Sorgente lasciato invariato; fallback Storm preservato. Report: `reports/REPORT-opt-scomp.md`.

## Decisioni autonome
Fallback conservato per garantire il comportamento originale invece di accettare output divergenti.
