# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `gdb`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/b2-camp/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Risolvere Bug 2 — Personaggi neri in idle su Campaign Select

Usa l'ottima analisi già fatta in `reports/SOL-b2-camp.md` e `reports/STATE-b2-camp.md`:
1. **Identifica esattamente cosa disegna i 3 personaggi in Campaign Select (`idle` vs `hover`):**
   - In `build/qa/b2-camp/StarCraft.orig.exe.i64` (già esistente!), decompila/ispeziona il chiamante di `sub_4EF100` (`0x4EF100`), e i controlli del dialogo Campaign Select (`glue\campaign\` / `xTerran` / `Terran` / `Zerg` / `Protoss` / `sub_4EE2D0` / `sub_4EEC30` / `sub_4EEE00` / `sub_4EED10` / `sub_4F7AE0` / `sub_4D4470`).
   - Nella copia privata `build/qa/b2-camp/src/`, sistema il logger `SC_B2_TRACE=1` (una riga per blit, SOLO `width < 320` o fuori 640x480, più path/nome entry MPQ in `storm_267`/`storm_268`/`SFileOpenFileEx` + handle SMK in `SmackOpen` / `SmackToBuffer` / `SmackWait` / `SmackDoFrame` / `storm_457`!).
   - Avvia `SC_BIN="$PWD/build/qa/b2-camp/starcraft-x86-b2" SC_B2_TRACE=1 ./tools/run-isolated.sh start --id b2-camp-trace --skip-intro 1 --smack 1`, naviga fino a Campaign Select verificando ogni schermata con `shot`, poi cattura log + screenshot in **idle** (`move 10 10`) e poi in **hover** (`move 130 220`) nello stesso run!
2. **Trova la causa radice e verifica il fix:**
   - Confronta `idle` vs `hover`: quale SMK/GRP/SDlg/storm_457/SmackWait/storm_334/palette non disegna in `idle` e perché?
   - Implementa la correzione nella copia privata `build/qa/b2-camp/src/`, ricompila (`--redefine-syms` se shim!), linka `build/qa/b2-camp/starcraft-x86-b2` con path assoluti di `entry-x86/*.entry.o`, verifica `idle-fixed.png` (mouse `10,10`) con `read` dimostrando che i 3 personaggi sono visibili anche da fermi!
3. **Consegna:**
   - Ferma `./tools/run-isolated.sh stop --id b2-camp-trace`.
   - Salva il diff unificato (`diff -u` contro i file originali in `runtime/`) in `build/qa/b2-camp/fix.patch`.
   - Scrivi `reports/SOL-b2-camp.md` e `reports/STATE-b2-camp.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
