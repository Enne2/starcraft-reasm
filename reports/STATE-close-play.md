# STATE — scx86-close-play

- `build/starcraft-x86`; tre run solo `run-isolated.sh`, Xvfb privati/audio dummy. Tutte chiuse, PID assenti.
- Briefing raggiunto. Ritratti difettosi riprodotti: 2 slot neri/1 scanline (`07-briefing-portraits.png`); altra cattura con tutti neri (`36-title-briefing.png`). Causa non isolata: log Smack limitato; SCode size1=0x4A in interpreter senza nesso causale dimostrato. Nessun fix.
- Save/Load non riusciti: `testsave`, Save/Invio non chiudono; nessun `.snx`/`.sav`; Load vuoto/disabilitato (`12`–`17`).
- Raccolta verificata: terzo run, SCV `(289,207)`, click destro minerali `(270,71)` dopo pan `(100,360)`. Minerali 150→158 (15 s)→262 (altri 185 s), +112 dopo ordine. Evidenze `53`, `55`–`57`.
- Refinery avviata nel secondo run: SCV `(289,207)`, Build Basic Structure `(522,455)`, tooltip Refinery `(615,375)`, geyser ghost verde `(600,54)`. `47-refinery-construction.png`, `48-refinery-progress.png`; 150→50 minerali. In costruzione, non verificata completa.
- Nessun STOP/fault; avviso SCode 0x4A fallback interpreter. No code edit/rebuild; D19/`proc_range` invariati.
- Screenshot `build/qa/close-play/01`–`57`; log `build/isolated/close-play/game.log`.
- Decisione: nessun fix speculativo; Save/Load e completamento Refinery non dichiarati.
