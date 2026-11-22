# Preludio obbligatorio
- Oracolo primario: IDA / `llvm-objdump` / `storm.orig.dll` / `StarCraft.orig.exe`.
- Solo diagnosi (NON modificare `runtime/` né `build/starcraft-x86`).
- Usa SOLO `./tools/run-isolated.sh` (`--id diag-camp`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Diagnosi Bug 2 — Personaggi neri nella schermata scelta campagne
Nella schermata scelta campagne (`Terran`, `Zerg`, `Protoss`), i personaggi/immagini restano completamente neri quando sono fermi (quando il mouse non è sopra di essi / non sono selezionati).

1. Avvia `./tools/run-isolated.sh start --id diag-camp --skip-intro 1 --smack 1` e naviga fino alla schermata scelta campagne (`click 220 120` → `click 260 226` → `click 100 109` → `click 515 393`).
2. Cattura screenshot con mouse fuori dai personaggi (`move 10 10` -> `idle.png`) e poi con hover su Terran/Zerg/Protoss (`hover-terran.png`, ecc.) e leggili con `read`.
3. Ispeziona `runtime/shim-port/elf/smack_elf.c`, `runtime/common/storm_sdraw_arm.c`, `runtime/shim-port/elf/com_ddraw.c` (`SmackOpen`, `SmackToBuffer`, `SmackBufferToBufferTrans`, `SmackDoFrame`, `SmackNextFrame`, `flags`, `SetEntries`, `Blt` / `SBlt*`) per capire perché i frame idle/statici restano neri mentre l'hover li disegna.
4. Identifica la causa radice esatta (file, funzione, riga) e la soluzione precisa da applicare.
5. Chiudi `./tools/run-isolated.sh stop --id diag-camp`.
6. Scrivi `reports/DIAG-campaign.md` e `reports/STATE-diag-campaign.md` (max 70 righe).
