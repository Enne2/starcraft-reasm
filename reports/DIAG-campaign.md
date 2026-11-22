# Diagnosi — campagne nere a riposo

## Esito
- Riprodotto con `./tools/run-isolated.sh` (id `diag-camp`, `SC_SKIP_INTRO=1`, `SC_SMACK=1`, SDL audio dummy, display Xvfb dedicato). Nessuna finestra/audio dell'utente.
- Schermata raggiunta con la sequenza richiesta; screenshot controllati: `reports/idle.png`, `reports/hover-terran.png`, `reports/hover-zerg.png`, `reports/hover-protoss.png`.
- `idle.png`: i tre ritratti sono assenti/nero. Ciascun hover mostra il relativo personaggio e dettagli a colori; gli altri due restano assenti. Il runner è stato arrestato con `stop --id diag-camp`.

## Evidenze runtime e codice
- `build/isolated/diag-camp/game.log`: SMK2 aperti; `SmackToBuffer` usa `flags=0` (log e contratto in `runtime/shim-port/elf/smack_elf.c:494-519`). `SmackDoFrame` decodifica frame con pixel non-zero e hash sorgente=destinazione (log, righe 58, 73, 88; implementazione `:364-405`). Quindi apertura, pitch/copia e dati decodificati non spiegano il nero osservato.
- `SmackNextFrame` avanza e aggiorna la palette (`smack_elf.c:407-427`); nessun errore/flag Smack non supportato osservato. `SetEntries` conserva RGB e applica la palette (`com_ddraw.c:461-478`); i colori dei ritratti in hover confermano che non è una palette globalmente nera.
- Non esiste `SmackBufferToBufferTrans` nell'adapter (`smack_elf.c:583-597`); non va indicata come causa senza prova che Storm la richieda. `com_ddraw.c:s_Blt` (`:257-298`) e `s_BltFast` (`:300-313`) non mostrano un errore specifico ai ritratti.

## Causa radice: limite dell'evidenza
- **Non è possibile attribuire onestamente una causa radice esatta a un file/funzione/riga del runtime da questa prova.** I pixel arrivano correttamente al buffer SMACK, ma manca una traccia che dimostri se la routine Storm di compositing viene chiamata per i ritratti non-hover, e con quali rettangoli/sorgente/palette. Lo screenshot prova la correlazione con hover, non il punto in cui si perde il disegno.
- L'oracolo IDA su `build/lift-full/storm.orig.dll`: `storm_457` (`0x1502F500`) applica il frame e raggiunge `sub_1502EFA0` a `0x1502F8B8` solo nel ramo `flags & 0x200`; `sub_1502EFA0` (`0x1502EFA0`) enumera `SmackToBufferRect` e chiama `storm_334` per il blit SCode (`0x1502F055`, `0x1502F288`, `0x1502F42A`). È la catena da tracciare, ma non prova quale ramo manchi in idle. Nessun IDA database è stato lasciato aperto.
- `runtime/common/storm_sdraw_arm.c` implementa i kernel sostitutivi `storm_313_arm_body`/`storm_334_arm_body` (validazione flags/SCode in `storm_sdraw_arm.c:403-496`); nessun errore/fail73 è apparso nel log. Non c'è evidenza per cambiare le semantiche del blitter.

## Soluzione precisa
- **Nessuna patch è giustificata ora.** Tracciare nel binario originale StarCraft il callback/menu di selezione e confrontare idle vs hover: valore dei flags video (`a1+32`), invocazione di `storm_457`/`sub_1502EFA0`, rettangoli `SmackToBufferRect` e surface SCode. Correggere solo il primo punto in cui il percorso idle diverge; non alterare decoder, palette o `Blt` sulla base degli screenshot.

## Decisioni autonome
- Conservativa: diagnosi soltanto; nessuna modifica a `runtime/` o `build/starcraft-x86`. Evitata una causa/fix speculativi perché la prova identifica il confine (compositing dopo il buffer SMACK), non il ramo responsabile.
