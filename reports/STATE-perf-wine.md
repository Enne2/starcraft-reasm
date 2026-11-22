# Stato — confronto prestazioni nativo / Wine

## Completato

- Creato `tools/bench-compare.sh`: avvia il nativo col runner isolato e l'originale StarCraft 1.16.1 con Wine 11.20 Staging, audio disabilitato, campionamento `/proc` a 1 Hz per cinque secondi e due ripetizioni.
- Eseguite due run valide per target. I PID di gioco Wine sono stati selezionati tramite `/proc/<pid>/comm == StarCraft.exe`, non dal launcher `wine`; il processo e gli helper Wine sono inclusi nei campioni. Dati grezzi e riepilogo: `build/qa/perf-compare/samples.json`, `summary.csv`, log e screenshot.
- Native: boot, menu e gameplay Boot Camp raggiunti e verificati con screenshot.
- Wine: boot e menu verificati; XTEST raggiunge Select Game Type. Non è stato possibile verificare una partita: gameplay Wine è n/d, non sono riportati dati simulati.
- Report dettagliato: `reports/PERF-WINE-COMPARISON.md`.
- Pulizia finale verificata: `./tools/run-isolated.sh list` mostra tutte le istanze bench inattive; nessun `StarCraft.exe`, `starcraft-x86`, `wineserver`, `explorer.exe`, `services.exe`, `winedevice.exe` o Xvfb di prova residuo.
- Nessun rebuild nativo, nessuna finestra né input sul desktop dell'utente.

## Limiti / scostamenti

- Il runner nativo preesistente assegna display `:100–:299`; non è stato modificato e le run native hanno usato `:230–:234`. Wine ha usato `:301–:302`. Entrambi i target erano comunque in Xvfb off-screen.
- Il totale RSS Wine misurato è 378–446 MiB, superiore alla stima 245–260 MiB perché il perimetro include anche `explorer.exe` (~123–153 MiB), `services.exe`, due `winedevice.exe` e `wineserver`, oltre a StarCraft.exe. I dati per PID sono nei campioni grezzi.
- Il menu Wine è stato campionato dopo aver lasciato completare l'intro e aver inviato Single Player; non è temporalmente equivalente al menu nativo.
- RSS nativo in gameplay variabile (31–154 MiB tra le due letture) in un sistema con pressione di memoria significativa; valori reali mantenuti e dichiarati nel report.

## Decisioni autonome

- Non ho modificato `tools/run-isolated.sh` per forzare `:300+`: la sua allocazione esistente è vincolata a `:100–:299` e il task vieta di modificarlo.
- Ho lasciato gameplay Wine non misurato perché gli screenshot post-XTEST mostrano ancora Select Game Type, non una partita.
