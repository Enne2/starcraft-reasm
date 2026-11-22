# Bug 2 — Campaign Select idle: diagnosi (fix funzionale non dimostrato)

## IDA — copia privata
Sessione IDA `28f80e04`, binario `build/qa/b2-camp/StarCraft.orig.exe`:
- `sub_4EF100` @ `0x4EF100` prepara la schermata; chiama `sub_4EE2D0` @ `0x4EE2D0` (`0x4EF275`), quindi `sub_4EEC30`, `sub_4EEE00` e `sub_4EED10`.
- Xref chiamante `sub_4E0710` @ `0x4E071B`.
- `sub_4EE2D0` seleziona e carica `dlgs\\terran.grp`, `dlgs\\protoss.grp` o `dlgs\\zerg.grp` via `sub_4197B0` @ `0x4197B0`; il callback `sub_4D2B30` @ `0x4D2B30` apre/legge/chiude la risorsa tramite Storm #265/#401/#269/#253.
- Queste funzioni provano l'inizializzazione e il caricamento GRP, ma non dimostrano quale asset/entry disegni i tre personaggi né il motivo della differenza idle/hover.

## Trace e riproduzione
- Creato binario privato `build/qa/b2-camp/starcraft-x86-b2`; runner usato solo con `./tools/run-isolated.sh`, display Xvfb isolato `:201`, audio dummy.
- Navigazione documentata da screenshot: `trace-start.png`, `trace-single-player.png`, `trace-select-game-type.png`, `trace-registry-selected.png`, `trace-campaign.png`.
- Nello stesso run: `idle-trace.png` con `move 10 10` mostra i personaggi non visibili; `hover-trace.png` con `move 130 220` mostra l'artwork Zerg, non gli altri due. Run arrestato e verificato: game/Xvfb non attivi.
- Logger privato: trace `storm_313`/`storm_334` in una riga per blit con i filtri richiesti. Tracce: `trace-idle.log`, `trace-hover.log`. I primi log SMK associano handle `0x11`–`0x18` a stream 184x128, 216x136, 252x124, 320x116, 260x176, 264x148, 160x140, 204x132; ToBuffer/DoFrame/Wait vengono eseguiti.
- Il wrapper SFileOpenFileEx (#268) è stato predisposto nel sorgente privato, ma nessuna riga `[sfile]` è comparsa: la mappatura MPQ-handle→nome entry non è stata acquisita. Alcuni log SMK legacy emettono `\\n` letterale.

## Esito
Causa radice non determinata: la prova dimostra disegno GRP/Storm e stream SMK attivi, ma non collega le chiamate/asset ai ritratti o mostra la condizione che li omette in idle. Non applicato un fix funzionale né prodotto `idle-fixed.png`; non è corretto dichiarare risolto. `build/qa/b2-camp/fix.patch` contiene esclusivamente strumentazione diagnostica privata, non un fix del difetto.

## Decisioni autonome
Ho scelto l'opzione conservativa: nessuna modifica funzionale speculativa a decoder, palette o blitter. L'evidenza è insufficiente per giustificare un fix; `runtime/`, `tools/`, `build/obj-x86/` e `build/starcraft-x86` restano intatti.
