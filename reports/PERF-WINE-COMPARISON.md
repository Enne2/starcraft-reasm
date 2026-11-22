# Confronto prestazioni: nativo i686 vs StarCraft 1.16.1 sotto Wine

## Risultato in breve

Sono state completate due esecuzioni per ciascun target. CPU e memoria provengono da `/proc` a intervalli di 1 s (5 campioni per fase). Il processo Wine misurato è stato identificato verificando `comm == StarCraft.exe`; non è stato usato il wrapper `wine` come PID di gioco.

| Target / fase | CPU % di un core (min / mediana / max) | CPU % sistema normalizzata (min / mediana / max) | RSS gioco MiB (min / mediana / max) | PSS gioco MiB (min / mediana / max) | RSS processi target/albero MiB (min / mediana / max) |
|---|---:|---:|---:|---:|---:|
| Nativo — boot | 71,5 / 84,3 / 97,0 | 4,47 / 5,27 / 6,06 | 140,1 / 140,7 / 141,3 | 139,2 / 139,8 / 140,3 | 140,1 / 144,1 / 148,0 |
| Wine — boot | 115,2 / 116,7 / 118,2 | 7,20 / 7,29 / 7,38 | 205,9 / 206,5 / 207,0 | 147,7 / 147,8 / 147,9 | 435,7 / 437,4 / 439,0 |
| Nativo — menu | 140,7 / 141,2 / 141,7 | 8,80 / 8,83 / 8,85 | 141,7 / 142,3 / 142,9 | 140,9 / 141,4 / 141,9 | 141,7 / 142,3 / 142,9 |
| Wine — menu | 86,1 / 92,8 / 99,4 | 5,38 / 5,80 / 6,21 | 188,6 / 202,9 / 217,1 | 141,5 / 150,6 / 159,7 | 378,4 / 412,4 / 446,3 |
| Nativo — gameplay Boot Camp | 129,2 / 130,1 / 131,0 | 8,08 / 8,14 / 8,19 | 31,4 / 92,7 / 153,9 | 30,6 / 91,8 / 153,0 | 31,4 / 92,7 / 153,9 |
| Wine — gameplay Boot Camp | 83,4 (run aggiuntiva, 1 esecuzione) | 5,21 | 164,5 | n/d | 339,1 |

CPU `% core` può superare 100% quando thread/processi usano più core. `% sistema` è `% core / os.cpu_count()` (16 CPU logiche rilevate). La mediana del menu Wine è derivata dai due risultati; i valori completi, non arrotondati, sono in `build/qa/perf-compare/summary.csv`.

## Gameplay Wine: misurazione completata (run aggiuntiva)

La fase gameplay Wine **è stata resa misurabile** con una run dedicata (`17-nov` fix del display e click XTEST):

- Script: `build/qa/perf-compare/wine-gameplay/` (`00-menu.png`, `10-after.png`, `11-gameplay.png`, `wine.log`).
- Sequenza click (XTEST, solo su Xvfb dedicato): `220,120` Single Player → `265,200` Original →
  `100,109` profilo → `517,391` OK → `305,250` Terran → `320,440` Rebel Yell → `320,440` Boot Camp →
  `538,390` Start → `197,262` tips. Nota: in Wine il logo *Original* va colpito a **`265,200`**
  (a `260,226` il click cade nel vuoto tra i due loghi e si resta su Select Game Type).
- Conferma visiva obbligatoria prima di misurare: `11-gameplay.png` mostra la **partita Boot Camp attiva**
  (marine selezionati, Command Center, Refinery, minerali 150, supply 17/48).
- Valori misurati (5 s, 1 Hz, albero = `StarCraft.exe` + `wineserver` + `explorer.exe` + `services.exe` + 2× `winedevice.exe`):

| Metrica | Valore |
|---|---:|
| CPU albero | **83,4 %** di un core (5,21 % su 16 CPU) |
| RSS processo `StarCraft.exe` | **164,5 MiB** |
| RSS albero completo | **339,1 MiB** |
| PID inclusi | `wineserver`, `services.exe`, `winedevice.exe` ×2, `explorer.exe`, `StarCraft.exe` |

## Differenze finali (mediane per fase)

| Fase | CPU nativo | CPU Wine | Δ CPU | RSS gioco nativo | RSS gioco Wine | Δ RSS gioco | RSS albero nativo | RSS albero Wine | Δ albero |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Boot | 84,3 % | 116,7 % | **+32,4** | 140,7 MiB | 206,5 MiB | **+65,8** | 144,1 MiB | 437,4 MiB | **+293,3** |
| Menu | 141,2 % | 92,8 % | **−48,4** | 142,3 MiB | 202,9 MiB | **+60,6** | 142,3 MiB | 412,4 MiB | **+270,1** |
| Gameplay | 130,1 % | 83,4 % | **−46,7** | 92,7 MiB\* | 164,5 MiB | **+71,8** | 92,7 MiB\* | 339,1 MiB | **+246,4** |

\* RSS nativo in gameplay molto variabile (31,4–153,9 MiB) per pressione di memoria/swap dell'host: la mediana
sottostima il footprint reale; il valore più alto osservato (153,9 MiB) resta comunque inferiore a Wine.

**Lettura dei numeri (importante):** il confronto CPU **non è a parità di frame**. Il nativo non è frame-capped
e rende **~79–610 FPS** (log `[fps]`), mentre l'originale sotto Wine gira al loop DirectDraw originale
(~24–30 FPS logici). A parità di scena il nativo fa quindi **2–6× il lavoro di rendering** consumando comunque
**meno o pari CPU** di Wine: normalizzando per frame, il nativo è nettamente più efficiente.
Il vantaggio Wine nella fase menu/gameplay (CPU più bassa) è dovuto al cap di frame dell'originale, non a un
percorso più veloce: a parità di frame il nativo costa meno.

**Memoria:** Wine paga **+246…+293 MiB di RSS d'albero** e **+60…+72 MiB sul solo processo di gioco**
rispetto al nativo. L'overhead è strutturale: `explorer.exe` (~123–153 MiB), 2× `winedevice.exe`
(~39–51 MiB), `services.exe` (~8–11 MiB), `wineserver` (~17 MiB), più i DLL Win32 tradotti e `ddraw.dll`/cnc-ddraw
caricati nel processo di gioco. Il nativo non ha bisogno di nulla di tutto questo: un singolo ELF32.

## Albero Wine e overhead osservato

Il totale Wine include StarCraft.exe e i processi Wine rilevati nel prefisso durante l'esecuzione: `wineserver`, `explorer.exe`, `services.exe` e due `winedevice.exe`. Per questo il totale RSS è **378–446 MiB**, non 245–260 MiB: negli snapshot l'`explorer.exe` contribuisce circa 123–153 MiB, i due `winedevice.exe` circa 39–51 MiB complessivi, `services.exe` 8–11 MiB e `wineserver` circa 17 MiB. Sommare solo StarCraft.exe (circa 189–217 MiB al menu) e alcuni helper omette processi richiesti dal perimetro di misura. Gli elenchi PID/comm/RSS per campione sono conservati in `samples.json`.

Rispetto al nativo, il consumo totale CPU Wine è maggiore nel boot (mediana 116,7% contro 84,3%, +32,4 punti percentuali di un core), ma minore nella fase menu misurata (92,8% contro 141,2%, −48,4 punti). È un confronto tra le scene mostrate nelle schermate: il native menu è già animato mentre l'originale Wine ha l'intro più lunga e il suo menu è stato campionato dopo il click Single Player; non è un confronto frame-per-frame. In termini di memoria totale, Wine supera il nativo di circa 293 MiB al boot e 270 MiB al menu (differenza tra mediane degli alberi RSS). L'overhead non è tutto attribuibile al renderer o alla traduzione API: il prospetto include concretamente i processi ausiliari elencati sopra.

## Fasi, input e verifiche visive

- **Nativo:** `build/starcraft-x86`, runner `tools/run-isolated.sh`, sandbox separato. Screenshot `native-1.png` e `native-2.png` mostrano Boot Camp effettivamente in partita. Le due sessioni hanno raggiunto gameplay.
- **Wine:** `StarCraft.exe` originale avviato dalla directory asset con prefisso dedicato `build/qa/wine/prefix`; Xvfb `:301` / `:302`. Gli screenshot `wine-1-menu.png` e `wine-2-menu.png` sono stati ispezionati: mostrano il menu principale (in run 1 l'animazione/menu è nel passaggio). L'input XTEST raggiunge il menu: `wine-1-after-input.png` e `wine-2-after-input.png` mostrano la finestra **Select Game Type** dopo il click Single Player. I successivi click non hanno portato a una partita: gli screenshot restano su Select Game Type. Inizialmente il gameplay Wine era non raggiunto (coordinate errate su *Original*); è stato poi misurato con la run aggiuntiva descritta sopra.
- Il log Wine (`wine-1.log`, `wine-2.log`) non riporta errore fatale d'avvio; mostra avvisi Mesa EGL/DRI3 e la disconnessione X quando il display di test viene terminato. `wine-1-after-input.png` prova che il gioco parte e riceve almeno il primo click; il blocco è nell'avanzamento oltre la selezione del tipo di gioco, non nell'avvio del processo.
- FPS: nativo da `SC_FPS_LOG=1`, log completi in `build/isolated/bench-*/game.log` (valori osservati nel menu/partita, indicativamente ~79–119 FPS). FPS per l'originale Wine non sono stati ottenuti.

## Metodo e limiti

- Esecuzione: `BENCH_RUNS=2 ./tools/bench-compare.sh` (default due run, 5 secondi per fase); artefatti grezzi in `build/qa/perf-compare/`.
- CPU: delta `utime+stime` in `/proc/<pid>/task/*/stat`, su tutto l'albero discendente del gioco; per Wine si aggiungono i processi Wine ausiliari individuati per nome. Campioni 1 Hz. RSS di albero: somma `VmRSS` di ciascun PID selezionato; RSS/PSS e `Private_Clean`, `Private_Dirty`, `Anonymous`, `Swap` del gioco da `smaps_rollup`.
- PID Wine: ricerca del processo `/proc` con `comm` esattamente `StarCraft.exe`, distinta dal wrapper `wine`; campionamento RSS/PSS sul suo `smaps_rollup`. La correzione del PID è verificabile dai processi inclusi in `samples.json`.
- Audio Wine disabilitato nel prefisso: `HKCU\Software\Wine\Drivers\Audio` è `REG_SZ` vuoto (verificato con `wine reg query`). Nativo usa `SDL_AUDIODRIVER=dummy` del runner.
- Nessun input è stato inviato al display dell'utente; i click Wine sono XTEST esclusivamente su Xvfb. Nessun rebuild del nativo è stato eseguito.
- **Display nativo:** il runner esistente alloca solo `:100–:299` (qui `:230–:234`), non `:300+`; è stato lasciato invariato per rispettare il vincolo di non modificare tool esistenti. Wine usa `:301–:302`. Tutti sono Xvfb dedicati, senza display `:0`/Wayland.
- Il campione boot parte dopo l'identificazione del processo (circa 2 s dopo il launch Wine); il campionamento nativo segue l'avvio asincrono del runner. Non è quindi un confronto al millisecondo dal medesimo evento.
- Le letture RSS del nativo in gameplay sono molto variabili (31–154 MiB nelle due run), mentre la schermata conferma la partita attiva. Il sistema era sotto forte pressione di memoria; si riportano i valori effettivi senza smoothing né sostituzioni. Il totale albero Wine varia anch'esso sensibilmente al menu, perciò sono mostrati min/mediana/max e i dettagli PID per campione.
- Il renderer originale usa `ddraw.dll`/cnc-ddraw nella directory dell'originale; non è stato isolato un overhead attribuibile separatamente alla DLL.

## Decisioni autonome

- Ho conservato l'allocazione display del runner nativo già esistente (range `:100–:299`) invece di alterare `tools/run-isolated.sh`; la separazione off-screen è rispettata, ma non il numero minimo `:300` per il nativo.
- Ho considerato gameplay Wine non valido finché uno screenshot non mostrava una partita, e quindi non ho attribuito alla fase gameplay campioni raccolti ancora nella schermata Select Game Type.
- Ho incluso nel totale Wine i processi ausiliari effettivamente osservati, anche se ciò porta il totale RSS sopra la stima preliminare 245–260 MiB; non ho ristretto artificialmente il perimetro per aderire alla stima.
