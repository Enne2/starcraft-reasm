# Report — `scx86-close-play`

Data verifica: sabato 21 novembre 2026. Tre run su `build/starcraft-x86` tramite esclusivamente `tools/run-isolated.sh --id close-play` (Xvfb privati, audio dummy). `llvm-objdump -d` eseguito sui PE originali StarCraft e Storm; nessuna sessione IDA aperta. Tutte le istanze arrestate; PID assenti.

## Esito

- **Briefing/ritratti**: percorso Single Player → Original → Terran → Boot Camp raggiunto. `07-briefing-portraits.png` riproduce due slot neri e uno visibile con scanline; `36-title-briefing.png` mostra tutti i riquadri neri al momento dello scatto. Nessun fix applicato.
- **Evidenze Smacker/SDraw**: `game.log` mostra `SmackOpen` valido, asset ritratti 60×56 (10 frame), `SmackBufferOpen` 0xD20 byte e `DoFrame`/`ToBufferRect`. I soli `ToBuffer #`/hash sono limitati dai contatori e precedono i ritratti: causa in `SmackToBuffer`, palette, stride o `storm_334/357` non provata. Il kernel SCode `size1=0x4A` ricade nell’interprete, ma il log non prova un nesso causale con i ritratti.
- **Save/Load**: nel primo run, campo `testsave`, click Save e Invio non chiudono il dialogo; nessun `.snx`/`.sav` nel sandbox. Load vuoto e disabilitato. Salvataggio/ricaricamento reale non verificati; screenshot `12`–`17`.
- **Minerali**: nel terzo run SCV selezionato a `(289,207)`, pan via minimappa `(100,360)`, ordine di raccolta con click destro sul campo a `(270,71)`. HUD: 150 prima dell’ordine, 158 dopo 15 s, 262 dopo altri 185 s: **+112 minerali** rispetto al valore iniziale, raccolta verificata. Screenshot `53-scv.png`, `55-mining-order.png`, `56-minerals-after-15s.png`, `57-minerals-after-3min.png`.
- **Refinery**: nel secondo run SCV selezionato `(289,207)`, Build Basic Structure `(522,455)`; tooltip `Build Refinery` su `(615,375)`, costo 100. Pan minimappa `(99,384)`; ghost verde sul geyser a `(600,54)`. Click avvia la costruzione; `47-refinery-construction.png` e `48-refinery-progress.png` dopo 8 s mostrano SCV e Refinery in costruzione; minerali 150→50. Costruzione avviata, non attestata completata. Un click precedente a `(607,65)` era invalido.
- **Stabilità/patch**: nessun STOP/fault/SCode fatale; solo avviso kernel SCode `0x4A` con fallback interpreter. Nessun codice modificato né rebuild; `proc_range` e D19 invariati.

## Decisioni autonome

Senza prova causale sufficiente per i ritratti o la persistenza, non ho applicato fix speculativi. Ho ordinato la raccolta esplicita e collocato la Refinery solo dopo aver verificato il ghost verde sul geyser. Non dichiaro completati Save/Load o la costruzione della Refinery.

## Evidenze

Screenshot `build/qa/close-play/01-start.png`–`57-minerals-after-3min.png`; log finale `build/isolated/close-play/game.log`. Stato sintetico: `reports/STATE-close-play.md`.
