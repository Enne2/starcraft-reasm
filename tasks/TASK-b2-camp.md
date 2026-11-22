# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/b2-camp/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (`--id b2-camp`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Bug 2 — Personaggi neri quando fermi nella schermata scelta campagne

Nella schermata scelta campagne (`Terran`, `Zerg`, `Protoss`), i tre personaggi restano completamente neri quando non hanno il mouse sopra, mentre si colorano e animano correttamente in hover.

1. **Trace completa idle vs hover:**
   - Avvia con `SC_SMACK_TRACE=1 SC_SCODE_DUMP=1` `./tools/run-isolated.sh start --id b2-camp --skip-intro 1 --smack 1`.
   - Naviga fino a Campaign Select (`click 220 120` → `click 260 226` → `click 100 109` → `click 515 393`), metti il mouse a `(10,10)` (`idle`), cattura `idle.png`, poi fai hover su Terran (`move 130 220`) e cattura `hover.png`.
   - Ispeziona in IDA (`StarCraft.orig.exe` su copia privata in `build/qa/b2-camp/` + `storm.orig.dll` `storm_457` `0x1502F500`, `sub_1502EFA0`, `storm_334` `0x15030AB0`) come StarCraft gestisce i ritratti della schermata scelta campagne:
     - Usa `storm_457` con un flag specifico in idle vs hover?
     - O usa `SmackOpen(..., flags=0x...)` / `SmackGoto` / `SmackBufferToBufferTrans` / `SetEntries` / `storm_313` / `storm_334` / `SDlg*` / `Wait`?
     - Confronta nel log e in GDB i valori esatti passati a `SmackOpen`/`SmackToBuffer`/`SmackDoFrame`/`storm_457`/`storm_334` per i 3 ritratti in `idle` vs `hover`.
2. **Prototipo & Verifica su binario privato:**
   - Copia i soli `.c` da modificare in `build/qa/b2-camp/src/`, compilali (`-m32`, con `--redefine-syms` se shim), assembla la link-dir privata `build/qa/b2-camp/{rt,sh}/` da `build/obj-x86/{runtime,shim-redef}/*.o` e linka `build/qa/b2-camp/starcraft-x86-b2` come in `tools/link-x86.sh`.
   - Testa con `SC_BIN="$PWD/build/qa/b2-camp/starcraft-x86-b2" ./tools/run-isolated.sh start --id b2-camp-fix --skip-intro 1 --smack 1`, cattura `idle-fixed.png` (mouse a `10,10`) e leggilo con `read` per dimostrare che TUTTI E TRE i personaggi sono visibili anche da fermi!
3. **Consegna:**
   - Ferma tutte le istanze `b2-camp*`.
   - Salva il diff pulito in `build/qa/b2-camp/fix.patch` (relativo alla root).
   - Scrivi `reports/SOL-b2-camp.md` e `reports/STATE-b2-camp.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
