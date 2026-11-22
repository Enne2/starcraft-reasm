# Preludio obbligatorio
- Oracolo primario: IDA / `llvm-objdump` / `storm.orig.dll` / `StarCraft.orig.exe`.
- Solo diagnosi (NON modificare `runtime/` né `build/starcraft-x86`).
- Usa SOLO `./tools/run-isolated.sh` (`--id diag-transp`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Diagnosi Bug 3 — Trasparenza interfacce e menu (sfondo nero invece di semitrasparente)
Molte interfacce e menu (es. dialoghi, pannelli UI, menu in-game / briefing / registry) invece di avere lo sfondo semitrasparente (tint/alpha blending su palette 8-bit) hanno lo sfondo completamente nero.

1. Avvia `./tools/run-isolated.sh start --id diag-transp --skip-intro 1 --smack 1` e naviga verso una schermata con pannelli/dialoghi semitrasparenti (es. `Select Game Type`, `Registry`, o in-game `MENU` / `Save Game`). Cattura screenshot con `shot` e leggili con `read`.
2. Ispeziona `runtime/common/storm_sdraw_arm.c` (tutti i kernel `SCode`, `storm_334`, `storm_313`, `ROP`, `LUT` 64 KiB, `dst` vs `src` blending!), `runtime/shim-port/elf/com_ddraw.c` (`Blt`, `DDBLT_KEYSRC`, `DDBLT_ROP`, `Fill`), `runtime/shim-port/elf/gdi_elf.c`, `runtime/shim-port/elf/dlg_elf.c`, per capire dove la semitrasparenza viene persa o disegnata come nero (`0x00`).
3. Identifica la causa radice esatta (file, funzione, riga) e la soluzione precisa da applicare.
4. Chiudi `./tools/run-isolated.sh stop --id diag-transp`.
5. Scrivi `reports/DIAG-transp.md` e `reports/STATE-diag-transp.md` (max 70 righe).
