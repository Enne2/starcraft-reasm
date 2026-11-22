# Stato — diag-hover

- Task: diagnosi sola lettura Bug 1 hover menu.
- Runner: `./tools/run-isolated.sh`, ID `diag-hover`, avviato con `--skip-intro 1 --smack 1`; stop completato.
- Screenshot verificati con `read`: `build/isolated/diag-hover/before.png`, `hover-0ms.png`, `hover-150ms.png`, `hover-400ms.png`, `hover-1000ms.png`.
- Riproduzione: testo menu si occlude/interrompe durante l'animazione hover e torna visibile parzialmente; il nero pieno non è provato dalle catture.
- Log: SMK2 decode e copia nel buffer attivi; kernel SCode fallback `0x4A / 0x2BC5`, width `0x140`.
- IDA `storm.orig.dll`: confermato percorso `storm_457` → `sub_1502EFA0` → `storm_334`; database temporaneo chiuso/salvato.
- Root cause precisa non dimostrata: manca bytecode SCode osservato durante l'artefatto e confronto diretto con output originale. Ipotesi principale: interpretazione/compositing SCode, da confermare.
- Nessuna modifica a `runtime/` o `build/starcraft-x86`; nessun sotto-agente; nessun input su `:0`; zero suono.
- Report: `reports/DIAG-hover.md`.
- ## Decisioni autonome
  Nessun fix speculative: acquisire bytecode SCode e confrontare la semantica prima di intervenire.
