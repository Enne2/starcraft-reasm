# Stato — b2-camp

- IDA privata `StarCraft.orig.exe.i64` (sessione `28f80e04`): `sub_4EF100` prepara Campaign Select; `sub_4EE2D0` seleziona `dlgs\\{terran,protoss,zerg}.grp` tramite `sub_4197B0` e callback `sub_4D2B30`. Xref chiamante: `sub_4E0710`.
- Screenshot navigazione: `build/qa/b2-camp/trace-{start,single-player,select-game-type,registry-selected,campaign}.png`.
- Stesso run isolato `b2-camp-trace`: `idle-trace.png` (mouse 10,10) senza personaggi; `hover-trace.png` (130,220) mostra l'artwork Zerg. Run fermato; game e Xvfb morti.
- Trace log: `trace-idle.log`, `trace-hover.log`; logger 313/334 in una riga/blit, con width/out-of-640x480 filters. SMK streams e DoFrame/Wait/ToBuffer attivi, ma entry MPQ non mappate: wrapper SFile #268 non ha prodotto righe.
- Il root cause resta non provato. Non c'è fix funzionale né `idle-fixed.png`; non dichiarare il bug risolto.
- `fix.patch` contiene solo differenze diagnostiche QA contro `runtime/`; nessun file condiviso modificato. Binario ricompilato/linkato solo in `build/qa/b2-camp/`.

## Decisioni autonome
Ho mantenuto la scelta conservativa: non introdurre un fix speculativo senza mappatura dell'asset/call-site idle-hover e senza dimostrare la causa. Seguire `SOL-b2-camp.md` per evidenze e limiti.
