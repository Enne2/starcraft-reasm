# Preludio obbligatorio
- Oracolo primario: IDA (`storm.orig.dll` `0x15030AB0`), bytecode SCode reale in `build/qa/b1-hover/scode-disasm.txt`.
- **Proprietà esclusiva (NON toccare altri file di progetto):**
  - `runtime/common/storm_sdraw_arm.c`
  - `tests/x86_scode_lut_test/` (nuovo test unitario + parità bit-exact vs `run_scode`)
- **Vincoli fondamentali:**
  1. **Resa e comportamento identici al 100%** (0 byte di differenza rispetto a `run_scode()`).
  2. **Codice 100% C11 portabile, cross-OS e CPU-agnostic:** ZERO inline assembly, ZERO assunzioni di endianness non gestite o UB di strict-aliasing (`memcpy` per accessi multi-byte se necessario).
  3. **NON eseguire `./tools/build.sh` globale** né modificare `build/starcraft-x86` (lavora e testa in `build/qa/opt-scode/`).
  4. NON lanciare altri sotto-agenti.

# Missione: Fast-path C portabile & pre-validato per `storm_334` 1×1 LUT (`~35%` Menu / `~19%` Gameplay CPU)

Leggi `runtime/common/storm_sdraw_arm.c`, `reports/PROF-CPU.md`, `build/qa/b1-hover/scode-disasm.txt` (prime 150 righe).

1. **Analizza i kernel 1×1 LUT reali:**
   - Dal profiling, `storm_334_arm_body` + per-pixel `proc_range` (`scode_rd8`/`scode_rd32`/`scode_wr*`) + `run_scode()` occupano ~50% CPU al Menu e ~26% in Gameplay.
   - Guarda `build/qa/b1-hover/scode-disasm.txt` (kernel `size1=0x4A`, `size2=0x2BC5` con 3 blocchi `code1` prefix 0..3 byte, `code2` loop unrolled a 4 pixel, `code3` tail 0..3 byte):
     - Verifica tutte le varianti generate da Storm per i kernel 1×1 LUT (es. ordine `src` in `ESI` / `dst` in `EDI`, `lut[(src<<8)|dst]`, oltre a eventuali varianti `8A 0E 8A 17` / `8B 0E 8B 17` / `8A 0F 8A 16` e `0x04000000` righe interlacciate!).
2. **Implementa fast-path C portabile `match_1x_lut_kernel` + `run_1x_lut` + bounds fast-check in `run_scode`:**
   - **`match_1x_lut_kernel`:** verifica rigorosamente:
     - `flags == 0x40u`, `base != 0`, `size1`, `size2`, `size3`,
     - i template esatti dei byte generati in `code1` (prefix), `code2` (4-pixel block ripetuto, controllando ogni blocco rispetto al template esatto fino alla `jmp` finale!) e `code3` (tail), inclusa la corretta destinazione `stub` (`0x15030D30` o `0x15030D50`),
     - se anche 1 solo byte diverge dal fingerprint, fa fallback automatico su `run_scode()` (fail-closed 100% identico!).
   - **`run_1x_lut`:**
     - Prima dell'esecuzione valida **UNA VOLTA SOLA**:
       - `proc_range(base, 65536u, 0)` (intera LUT 64 KiB)
       - `range_2d(dst, rows, width, dst_delta, 1)` (incluso il caso interlacciato `0x04000000` dove si alternano `dst_delta` e `rd32(a2+48)` — verifica con esattezza tutte le righe toccate!)
       - `range_2d(src, rows, width, src_delta, 0)` (e relativo stride interlacciato `rd32(a2+52)`!)
     - Esegue il blending 1×1 in C11 puro e portabile (`d[i] = lut[((uint32_t)s[i] << 8) | d[i]]`, con unroll 4x/8x pulito senza UB, gestendo correttamente anche `0x04000000` righe pari/dispari!).
   - **Ottimizzazione sicura di `run_scode()` per qualsiasi altro kernel:**
     - Valida i range contigui `src` / `dst` / `base` (se `base != 0`) una volta a monte in `storm_334_arm_body` (passando gli span pre-validati `(s_lo, s_hi)`, `(d_lo, d_hi)`, `(b_lo, b_hi)` a `run_scode`), così `scode_rd*`/`scode_wr*` fanno un confronto inline O(1) senza lock/mutex e chiamano `proc_range()` SOLO se l'indirizzo esce dagli span già validati! (Comportamento 100% identico anche su fault!)
3. **Test di parità bit-exact & benchmark (`tests/x86_scode_lut_test/`):**
   - Scrivi `tests/x86_scode_lut_test/test_scode_lut.c` + `check.sh` che costruisce veri buffer `code1`/`code2`/`code3` (copiati dal dump reale `build/qa/b1-hover/scode-0x4a-0x2bc5.hex` + varianti `8A 0E/17`, allineamenti `dst & 3 == 0,1,2,3`, larghezze `1..364`, delta positivi e negativi, e flag `0x04000000` interlacciato!) ed esegue **sia `run_scode()` sia `run_1x_lut()` confrontando `0` byte di differenza** + misura speedup A/B!
   - Compila ed esegui in `build/qa/opt-scode/` sia con `-m32` sia con host 64-bit (`clang` nativo) per dimostrare la portabilità CPU-agnostic!
4. **Consegna:**
   - Scrivi `reports/REPORT-opt-scode.md` e `reports/STATE-opt-scode.md` (max 70 righe).
