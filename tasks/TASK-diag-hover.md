# Preludio obbligatorio
- Oracolo primario: IDA / `llvm-objdump` / `storm.orig.dll` / `StarCraft.orig.exe`.
- Solo diagnosi (NON modificare `runtime/` né `build/starcraft-x86`).
- Usa SOLO `./tools/run-isolated.sh` (`--id diag-hover`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Diagnosi Bug 1 — Animazione scritte in hover che diventano nere
Quando si passa il mouse sopra le scritte nei menu (es. `Single Player`, `Multiplayer`, `Campaign Editor` nel Main Menu, o opzioni in `Select Game Type`), le scritte diventano nere e riappaiono solo dopo un po'.

1. Avvia `./tools/run-isolated.sh start --id diag-hover --skip-intro 1 --smack 1`.
2. Cattura `before.png` sul Main Menu, poi muovi il mouse sopra `Single Player` (`move 220 120`) e cattura `hover-0ms.png`, `hover-150ms.png`, `hover-400ms.png`, `hover-1000ms.png` (leggili con `read` per vedere esattamente cosa succede!).
3. Ispeziona `runtime/shim-port/elf/smack_elf.c`, `runtime/common/storm_sdraw_arm.c` e `runtime/shim-port/elf/com_ddraw.c` (es. `SmackToBuffer`, `SmackBufferToBufferTrans`, `SmackDoFrame`, `SetEntries`, `SBlt*`, `SCode`, trasparenza/color-key/animazione `.smk` dei bottoni menu).
4. Identifica la causa radice esatta (file, funzione, riga, comportamento vs originale) e la soluzione precisa da applicare.
5. Chiudi `./tools/run-isolated.sh stop --id diag-hover`.
6. Scrivi `reports/DIAG-hover.md` e `reports/STATE-diag-hover.md` (max 70 righe).
