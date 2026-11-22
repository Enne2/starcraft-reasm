# Stato — scx86-play

- **Punto raggiunto:** Main Menu → Single Player → StarCraft Original → Registry → modal nuovo personaggio.
- **Non raggiunti/verificati:** profilo valido, campagna Terran, briefing, tutorial/prima missione.
- **Runner obbligatorio:** `./tools/run-isolated.sh`; Xvfb privato, SDL audio dummy, sandbox caratteri per ID. Tutte le istanze terminate.
- **Comando iniziale:** `./tools/run-isolated.sh start --id scx86-play --skip-intro 1 --smack 1 --script campaign --trace 1`.
- **Coordinate verificate a 640×480:** Single Player (220,120); Original (260,226); campo profilo (320,227); OK profilo (235,313).
- Marker script non sono prova visiva: connessione (150,102/518,391), Terran (313,256), briefing Start (520,400) restano da verificare.
- **Blocco:** conferma profilo termina con STOP fail-closed `0x90909090`; top stack `0x0050CF90`; log con VA guest `0x004118F1`, `0x0041188E`. Nessuna modifica al codice, D19 invariato.
- File profilo vuoti causati da testo duplicato nelle prove restano confinati a `build/isolated/<id>/sandbox/characters/`.
- **Prossimo passo:** nuovo ID isolato senza script; copiare nel sandbox una copia di profilo `.spc` valido da 44 byte e navigare con un solo `cmd` per volta. Al ripetersi del crash, GDB mirato al flusso `0x004118E4–0x004118F1`/`0x90909090`; non aggirare il resolver. Poi avanzare Terran → briefing → missione.
- Evidenze: `reports/REPORT-x86-play.md`, log e screenshot in `build/isolated/scx86-play{,-manual}/`.
