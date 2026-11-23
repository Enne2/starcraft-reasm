# Report: race-select Terran/Zerg/Protoss

## Esito
Non risolto. Il commit `6621246` (present coalesced/tick) non ha portato la luminanza Terran al target nella schermata di selezione; Zerg e Protoss corrispondono circa ai riferimenti. La ricerca va ora limitata al percorso di disegno del ritratto Terran (film Smacker → ToBuffer/ToBufferRect → destinazione → blitter SCode), come indicato dal supervisore.

## Misure
- Ambiente: build x86 esistente, `tools/run-isolated.sh`, Xvfb privato, gioco in background, `SDL_AUDIODRIVER=dummy`; commit `6621246`.
- Schermata raggiunta con Single Player → Original → profilo → Ok. Screenshot: `build/qa/race-select-measure.png`.
- Comando misure (crop Gray, media normalizzata):
  - Terran, `160x140+170+120`: **0.0109446** (riferimento ~0.010; obiettivo >0.02).
  - Zerg, `160x150+20+250`: **0.0392342** (riferimento 0.038; obiettivo >0.02).
  - Protoss, `160x180+440+120`: **0.0553763** (riferimento 0.056; obiettivo >0.02).
- Un passaggio hover sul Terran ha misurato 0.0175487 nell’area; non equivale a un ritratto correttamente renderizzato e non cambia la conclusione idle.
- Istanza sandbox fermata dopo le misure.

## Evidenze e cosa è escluso
- Le misure riproducono i valori di riferimento forniti: il present-per-Blt di `6621246` non ha risolto la mancanza del ritratto Terran. Il problema residuo non va più attribuito genericamente alla presentazione.
- Nel log `build/isolated/race-select-measure/game.log` compaiono ripetutamente due rifiuti `ToBuffer fuori buffer`:
  - `bytes=0x10D80, pitch=0xC4, height=0xDC` (441 occorrenze);
  - `bytes=0x98A00, pitch=0xE8, height=0xD0` (440 occorrenze).
  Sono una pista concreta sul mapping/capacità delle destinazioni, ma non è ancora dimostrato quale chiamata appartenga al ritratto Terran. Non correggere dimensioni o buffer senza correlazione con handle/film e chiamante.
- Il trace SCode è limitato a 400 righe e nel log non c’è una correlazione univoca tra `SmackOpen`/handle, chiamata ToBuffer rifiutata e Blt del ritratto. Causa precisa ancora aperta.

## Prossimo passo
Aggiungere/acquisire una traccia mirata e limitata alla schermata campagna che associ per ogni film: handle e header SMK, destinazione registrata, parametri e risultato di `ToBuffer`/`ToBufferRect`, chiamante/indirizzo e successive scritture SCode nell’area Terran. Confrontare idle vs hover Terran; poi fare lo stesso solo se necessario per Zerg/Protoss. Nessun cambio di decoder, palette o dimensioni buffer finché la destinazione del film Terran non è identificata.

## Stato repository
Nessuna modifica al runtime o build; nessun commit/push. Il file non tracciato preesistente `runtime/shim-port/elf/storm_sdraw.c` è stato lasciato intatto.

---

## ESITO (2026-11-24) — RISOLTO

Causa provata: `SmackToBuffer` confrontava `pitch × destheight` con la tabella globale dei buffer
aperti invece che con il contratto della chiamata → le destinazioni del gioco (con padding) erano
rifiutate e i frame dei ritratti **non scritti** (log: `ToBuffer fuori buffer:
bytes/pitch/height=0x98A00/0xE8/0xD0` ×439 e `0x10D80/0xC4/0xDC` ×439). Il limite è ora per-chiamata
e la validazione anti-overrun usa l'ultimo byte realmente scritto.

Misure dopo la correzione (crop identici a quelli di questo report): Terran **0,0538** (era 0,0109),
Zerg **0,0968** (0,0392), Protoss 0,0666 (0,0554) — tutte > 0,02 a riposo, su x86 **e** ARM.
Dettagli: `reports/REPORT-graphics-final.md`.
