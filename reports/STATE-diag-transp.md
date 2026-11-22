# Stato diagnosi trasparenza

- Runner `./tools/run-isolated.sh start --id diag-transp --skip-intro 1 --smack 1`; Xvfb `:172`, audio dummy. Navigato Main Menu → `Select Game Type` → Original/Registry. Screenshot letti: `build/isolated/diag-transp/shot-gametype.png`, `shot-registry.png`, `shot-black.png`.
- `./tools/run-isolated.sh stop --id diag-transp` eseguito; processo assente.
- Difetto semantico certo: `runtime/shim-port/elf/com_ddraw.c:262–269` manda `DDBLT_ROP` a `rop_rect` dest-only prima del normale blit; `rop_rect:181–184` usa `pat` anche per ROP che devono leggere source. Soluzione: implementare il ROP su source/destination/pattern. Trace assente: legame con schermate non ancora provato.
- IDA `storm.orig.dll`: #313 `0x15036260`, #334 `0x15030AB0`; dispatch #334 a SCode dinamico (`0x15030D2A`). Nessun orientamento LUT errato provato. `storm_sdraw_arm.c` non modificato.
- GDI dialogi: `dlg_elf.c:403` usa brush stock 1, grigio (`gdi_elf.c:617–620`); non è spiegazione del nero Registry.
- Solo diagnosi/report; nessuna modifica a `runtime/` o `build/starcraft-x86`.
- Report dettagliato: `reports/DIAG-transp.md`.
