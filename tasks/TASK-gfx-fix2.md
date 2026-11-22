# Preludio obbligatorio
- Oracolo primario: IDA / `llvm-objdump` / `storm.orig.dll` / `StarCraft.orig.exe`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Chiusura Bug 1 (hover), Bug 2 (campagne idle) e Bug 3 (trasparenze)

Leggi `reports/REPORT-gfx-fix.md`, `reports/DIAG-hover.md`, `reports/DIAG-campaign.md`, `reports/DIAG-transp.md`.

1. **Bug 3 — Test ROP3 + Verifica visiva trasparenze:**
   - Aggiungi in `tests/x86_gfx_test.c` test dedicati per `DDBLT_ROP` (almeno `SRCCOPY`, `SRCAND`, `SRCPAINT`, `SRCINVERT`, `PATCOPY` con `src`, `dst`, `pat` reali) e riesegui `./build/qa/gap-gfx/x86_gfx_test`.
   - Avvia `./tools/run-isolated.sh start --id gfx-v3 --skip-intro 1 --smack 1` e verifica con `shot` + `read` le schermate `Select Game Type`, `Registry` e `MENU` in-game. Se un pannello resta nero opaco, identifica dal log/GDB quale chiamata lo disegna (`DDBLT_ROP`, `storm_334` SCode o `gdi_elf.c`).
2. **Bug 1 (Hover) & Bug 2 (Campagne idle) — Analisi bytecode SCode & `storm_457`/`storm_334`:**
   - Con `SC_SCODE_DUMP=1 SC_SMACK_TRACE=1`:
     - Sul Main Menu fai `move 220 120` (hover `Single Player`), leggi il bytecode SCode `0x4A/0x2BC5` dal log e disassemblalo (`llvm-mc` / `objdump` / Python) per vedere esattamente cosa fa rispetto all'oracolo `storm.orig.dll`.
     - Sulla schermata scelta campagne (`idle` a `10,10` vs `hover` sui personaggi), confronta le chiamate `SmackToBuffer` / `storm_457` / `storm_334` per capire perché i personaggi a riposo restano neri.
   - Applica i fix esatti in `runtime/common/storm_sdraw_arm.c`, `runtime/shim-port/elf/smack_elf.c` e/o `com_ddraw.c`.
3. **Build & Verifica finale di tutti e 3 i bug:**
   - Ricompila con `./tools/build.sh --arch x86 --skip-entry`, riesegui `x86_gfx_test`, `x86_runtime_check/check.sh`, `readelf -h`, `dispatch-ir.py --dump-map`.
   - Cattura e leggi con `read` gli screenshot finali dopo i fix per: (1) hover `Single Player` 0/150/400/1000ms, (2) scelta campagne `idle` (mouse `10,10`), (3) `Select Game Type` / `Registry` / `MENU`.
   - Chiudi `./tools/run-isolated.sh stop --id gfx-v3`.
   - Scrivi `reports/REPORT-gfx-fix2.md` e `reports/STATE-gfx-fix2.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
