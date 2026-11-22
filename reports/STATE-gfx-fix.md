# Stato — gfx-fix (parziale)

- Modificati: `runtime/shim-port/elf/com_ddraw.c`, `runtime/common/storm_sdraw_arm.c`, `runtime/shim-port/elf/smack_elf.c`.
- ROP3 generale usa P/S/D con indice truth-table P,S,D; conserva pitch, clipping superficie/clipper, rect src/dst e color-key sorgente.
- `SC_SCODE_DUMP=1`: dump kernel size1=0x4A / size2=0x2BC5; `SC_SMACK_TRACE=1`: trace ToBuffer.
- Build x86 skip-entry: PASS; warning non bloccante `input_elf.c:54`.
- `x86_gfx_test`: PASS (non copre nuovi ROP); `x86_runtime_check`: PASS.
- `readelf`: ELF32/EXEC/i386. dispatch map: 102392 voci, ordinate e univoche.
- Runner solo `./tools/run-isolated.sh`, id `gfx-fix`, display :173, dummy audio; fermato. Screenshot letto: Main Menu iniziale visibile.
- Non verificati visualmente: hover a 0/150/400/1000 ms; campagne idle e hover; Select Game Type, Registry e MENU in-game.
- Diagnostica acquisita, ma non confrontata con `storm.orig.dll`/`StarCraft.orig.exe`; non è dimostrata la causa né la correzione di hover/campagne idle.
- Conseguenza: task parziale, nessuna affermazione che i tre bug siano risolti.
- Decisione autonoma: fix ROP e logging documentato; nessun fix speculativo per i bug con causa non provata.
- Report: `reports/REPORT-gfx-fix.md`.
