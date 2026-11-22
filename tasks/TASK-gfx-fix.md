# Preludio obbligatorio
- Oracolo primario: IDA / `llvm-objdump` / `storm.orig.dll` / `StarCraft.orig.exe`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Fix & Verifica visiva dei 3 bug grafici (`hover`, `campagne idle`, `trasparenze`)

Leggi `reports/DIAG-hover.md`, `reports/DIAG-campaign.md`, `reports/DIAG-transp.md`.

1. **Bug 3 (`runtime/shim-port/elf/com_ddraw.c`):**
   - Correggi `s_Blt` / `rop_rect` per `DDBLT_ROP` implementando il truth-table ROP completo con `src`, `dst`, `pat` (mantenendo clipping, pitch, `DDBLT_KEYSRC`), invece di trattare i ROP come dest-only.
2. **Bug 1 (`hover`) & Bug 2 (`campagne idle`):**
   - Aggiungi un log diagnostico mirato (es. via `getenv("SC_SCODE_DUMP")` / `SC_SMACK_TRACE`) in `runtime/common/storm_sdraw_arm.c` e `runtime/shim-port/elf/smack_elf.c` per catturare:
     (a) il bytecode SCode esatto del kernel `0x4A / 0x2BC5` durante l'hover su `Single Player`,
     (b) le chiamate `storm_457` / `storm_334` / `SmackToBuffer` sui ritratti idle vs hover nella schermata scelta campagne.
   - Confronta con l'oracolo (`storm.orig.dll` / `StarCraft.orig.exe`) e correggi la causa radice di Bug 1 e Bug 2.
3. **Build & Verifica visiva completa:**
   - Ricompila con `./tools/build.sh --arch x86 --skip-entry` e verifica `./build/qa/gap-gfx/x86_gfx_test`, `./tests/x86_runtime_check/check.sh`, `readelf -h`, `dispatch-ir.py --dump-map`.
   - Verifica visivamente con `./tools/run-isolated.sh --id gfx-fix` + `shot` + `read`:
     1) Hover su `Single Player` (0ms, 150ms, 400ms, 1000ms): le scritte restano visibili senza diventare nere.
     2) Schermata scelta campagne (`idle` con mouse a `10,10`): i 3 personaggi sono visibili anche da fermi.
     3) Schermate con pannelli/dialoghi (`Select Game Type`, `Registry`, `MENU` in-game): sfondi semitrasparenti/tinti corretti e non neri opachi.
   - Chiudi `./tools/run-isolated.sh stop --id gfx-fix`.
4. **Consegna:**
   - Scrivi `reports/REPORT-gfx-fix.md` e `reports/STATE-gfx-fix.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
