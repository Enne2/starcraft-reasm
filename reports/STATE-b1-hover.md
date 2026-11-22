# STATE — b1-hover

- Bug visivamente riprodotto su binario privato: `Single Player` è quasi tutto coperto a 150 ms e torna leggibile a 400 ms.
- QA build `build/qa/b1-hover/starcraft-x86-b1` (~217 MiB), da copie `rt/`, `sh/`, `link/`; nessuna modifica a runtime/build di progetto.
- Screenshot letti: `verified-before.png`, `verified-hover-{0ms,150ms,400ms,1000ms}.png`.
- SCode: `0x4A/0x2BC5`, width `0x140`, rows `0x60`, LUT `0x20AE0010`; dst `0x20590010`, src `0x20607AA4`.
- Verifica QA dei risultati contro `LUT[(src<<8)|dst]`: zero mismatch sui campioni. Lookup sorgente zero/destination `0x2A` restituisce `0x2A`.
- DirectDraw trace: 0 `s_Blt`; lock/unlock/sync/present osservati sulla primaria con canvas `px` allineato (`0x19F76A70`, 640×480, pitch 640); trace limitata ai primi eventi.
- IDA Storm: `storm_457` `0x1502F500` → `sub_1502EFA0` `0x1502EFA0` → `storm_334` `0x15030AB0`; loop stub `0x15030D30`, terminazione `0x15030D80`. Root cause ancora aperta.
- `b1-hover-fix` fermato su richiesta. Nessun fix.patch verificato; `fix.patch` non creato.
- Report: `reports/SOL-b1-hover.md`.

## Prossimo passo
Rilancio esatto: `cd /home/enne2/dev/starcraft-reasm-generic && SC_BIN="$PWD/build/qa/b1-hover/starcraft-x86-b1" SC_SMACK_TRACE=1 ./tools/run-isolated.sh start --id b1-hover-fix --skip-intro 1 --smack 1`. Attendere il menu, poi correlare hover 0/150/400/1000 ms con trace mirata del callsite StarCraft originale di return `0x080AFF90`, `storm_457`/`sub_1502EFA0`/`storm_334`, byte sorgente/destinazione SCode e sequenza Lock→Unlock→Present. Confrontare i pixel della stessa area con l'oracolo originale; solo allora correggere e testare la copia QA.
