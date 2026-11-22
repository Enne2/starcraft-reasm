# Report — fast path SCode LUT 1×1

## Sintesi
- Aggiunto un fast path C11 per un fingerprint stretto del kernel LUT byte→byte, con fallback al `run_scode()` esistente per ogni mismatch.
- Aggiunti span per-invocazione per accessi src/dst/base: gli accessi dentro gli span validati evitano `proc_range()` per pixel; gli accessi fuori span mantengono il controllo originale.
- Nessun assembly inline; il fast path usa accessi a byte e assemblaggio LE esplicito per i blocchi da quattro pixel.

## Evidenze e implementazione
- File di codice: `runtime/common/storm_sdraw_arm.c`.
- Oracolo locale letto: `build/qa/b1-hover/scode-disasm.txt` (prime 150 righe) e `build/qa/b1-hover/scode-0x4a-0x2bc5.hex`; il dump conferma `code1=0x4A`, `code2=0x2BC5`, tre pixel prefix da 23 byte e blocchi `code2` da 70 byte.
- Disassemblato `storm.orig.dll` con `llvm-objdump` nel range `0x15030AB0–0x15032000`; la funzione a `0x15030AB0` conferma la selezione del flag interlacciato e i continuation stub `0x15030D30`/`0x15030D50` (in particolare setup/dispatch `0x15030D0D–0x15030D2A` e stub `0x15030D30–0x15030D7E`). Nessuna sessione IDA aperta o modificata.
- `match_1x_lut_kernel` richiede `flags=0x40`, base non nullo, flag descriptor di allineamento `0x00040000`, assenza di `0x04000000`, dimensioni esatte, template byte esatto di code1/code2/code3, entry/continuation coerenti e stub finale `0x15030D30`. Il fingerprint non corrispondente resta su `run_scode()`.
- Il path ottimizzato è limitato a width 1–364. Verifica span di src/dst per tutte le righe e LUT da 64 KiB prima di eseguire. Sovrapposizioni tra gli span dati fanno fallback.
- **Limite di evidenza:** il dump statico fornito non contiene code3. Il test costruisce code3 ripetendo il template pixel di code1 (come indicato nella richiesta); non ho acquisito un dump runtime indipendente di code3. Se code3 diverge, il matcher non accetta il kernel e usa il fallback.

## Verifiche
- `tests/x86_scode_lut_test/check.sh`: PASS su i386 (`gcc -m32`) e host x86-64 (`clang`), compilazione `-std=c11 -Wall -Wextra -Werror`.
- Parità testata contro `run_scode()`: tutte le width 1–364, `dst & 3` = 0..3, stride positivi/negativi, 3 righe; zero differenze nei buffer e nei registri/descriptor confrontati.
- Interlacciato `0x04000000`: matcher rifiuta il fast path; interpreter confrontato con riferimento LUT sulle righe alternate, PASS.
- Test fail-closed: alterazioni code1 `8A 0E/17`, code2 `8B 0E/17` e un byte code3 rifiutate.
- A/B misurato, 100 iterazioni 364×4, timer monotonic: i386 interpreter 3,084,470 ns vs C 140,479 ns (**21.96×**); clang x86-64 3,832,940 ns vs C 112,438 ns (**34.09×**). Microbenchmark locale, non misura del gioco.
- `git diff --check` PASS prima della stesura finale; nessuna build globale e nessuna modifica a `build/starcraft-x86`.

## Decisioni autonome
- Ho mantenuto un fingerprint conservativo: le varianti non coperte da tutti i template esatti restano sul fallback, senza tentare equivalenze basate solo sugli opcode iniziali.
- Il test host 64-bit neutralizza soltanto gli assert ABI di `_cpu` nel translation unit del test; il layout runtime non è stato modificato. Il target i386 compila con gli assert ABI attivi.

## File
- `runtime/common/storm_sdraw_arm.c`
- `tests/x86_scode_lut_test/test_scode_lut.c`
- `tests/x86_scode_lut_test/check.sh`
- `reports/REPORT-opt-scode.md`, `reports/STATE-opt-scode.md`
- Output QA: `build/qa/opt-scode/`.
