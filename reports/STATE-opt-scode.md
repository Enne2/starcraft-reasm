# Stato — SCode LUT 1×1

- **Implementato:** matcher fail-closed e kernel C `run_1x_lut` in `runtime/common/storm_sdraw_arm.c`.
- **Bounds fast-check:** span src/dst validati prima dell’interprete; LUT da 64 KiB prevalidata quando accessibile. `scode_rd*`/`scode_wr*` bypassano `proc_range` solo dentro lo span, altrimenti ricadono sul check originale.
- **Interlacciato:** `0x04000000` escluso dal fast path; fallback interpreter preservato e testato con pitch alternati.
- **Parità:** zero byte diversi per width 1–364, quattro allineamenti, stride signed e 3 righe; registri e campi descriptor confrontati.
- **Build/test:** `tests/x86_scode_lut_test/check.sh` PASS per i386 gcc e host x86-64 clang. Benchmark 364×4, 100 iterazioni: 21.96× i386, 34.09× x86-64 (microbenchmark locale).
- **Fingerprint:** accetta solo i template/target esatti verificati dal dump e dalla struttura stub; varianti diverse e byte alterati fanno fallback.
- **Limite noto:** code3 non è presente nei file dump forniti; test code3 generato dal template code1 come specificato nella richiesta, non catturato indipendentemente a runtime. Un code3 differente viene rifiutato dal matcher.
- **Non eseguito:** `./tools/build.sh`, modifica o rebuild di `build/starcraft-x86`, avvio del gioco, IDA, sotto-agenti.
- **Artefatti QA:** `build/qa/opt-scode/test_scode_lut_32` e `test_scode_lut_64`.
- **Report completo:** `reports/REPORT-opt-scode.md` (33 righe).
