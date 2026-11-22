# Stato gfx-fix2

- Aggiunto test dedicato ROP3 in `tests/x86_gfx_test.c` per SRCCOPY/SRCAND/SRCPAINT/SRCINVERT/PATCOPY con src/dst/pattern reali; `build/qa/gap-gfx/x86_gfx_test`: PASS.
- Build richiesta PASS; `x86_runtime_check` PASS; ELF32 EXEC i386; dispatch map: 102392 voci ordinate/univoche.
- Isolamento `gfx-v3` chiuso. Capture hover: `build/qa/gfx-v3-hover.png`; Select Game Type: `build/qa/gfx-v3-afterclick.png`; Registry: `build/qa/gfx-v3-registry-retry.png`.
- Non completati: hover temporizzato a 0/150/400/1000 ms, campagna idle/hover, MENU in-game; nessun nesso causale dei pannelli neri stabilito. Nessuna patch speculativa SCode/Smack/GDI.
- Vedi `reports/REPORT-gfx-fix2.md` per evidenze e limiti.
