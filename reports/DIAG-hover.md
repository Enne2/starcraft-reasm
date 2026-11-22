# Diagnosi — hover menu scuro

## Esito
- Riproduzione completata con `./tools/run-isolated.sh start --id diag-hover --skip-intro 1 --smack 1`; sessione poi fermata.
- `before.png`: voci verdi regolari. Dopo `move 220 120`, a 150 ms il testo `Single Player` è quasi interamente assente mentre compare l'animazione sovrapposta; a 400 ms riappare; a 1000 ms diverse lettere sono ancora intermittenti. Quindi il sintomo osservabile è perdita/occlusione temporanea dei pixel, non prova di un testo convertito stabilmente in nero.
- Screenshot: `build/isolated/diag-hover/{before,hover-0ms,hover-150ms,hover-400ms,hover-1000ms}.png`.

## Evidenze
- `game.log`: Smacker apre/decodifica più stream `SMK2`; `SmackToBuffer` e `SmackDoFrame` lavorano (hash sorgente/destinazione uguali nei primi frame). Nessun errore di decode, flag ToBuffer non supportato o errore palette riportato. Le animazioni continuano a oltre 130 FPS nel run: prova di avanzamento, non di timing originale.
- `runtime/shim-port/elf/smack_elf.c:342-361, 364-404`: `copy_frame` copia il frame indicizzato nel buffer registrato; `DoFrame` aggiorna palette e copia. È il buffer intermedio, non il compositing finale dello schermo.
- `runtime/shim-port/elf/com_ddraw.c:461-478`: `p_SetEntries` converte PALETTEENTRY in RGB e aggiorna palette host voce per voce. `host_elf_set_palette` (`runtime/shim-port/elf/host_elf.c:467-474`) conserva i 24 bit RGB.
- `game.log` segnala il kernel SCode fallback `size1=0x4A`, `size2=0x2BC5`, `width=0x140`, `flags=0x40000`. Quindi il blit SCode emulato è un candidato concreto nel percorso di compositing.
- Oracolo Storm originale: IDA, `storm.orig.dll`, sessione temporanea chiusa dopo l'analisi. `storm_457` a `0x1502F500` chiama `SmackDoFrame` e applica palette (`0x1502F53F-0x1502F5A9`), poi chiama `sub_1502EFA0` (`0x1502F8B5-0x1502F8B8`). `sub_1502EFA0` a `0x1502EFA0` calcola rettangoli e invoca `storm_334` (`0x1502F288`, `0x1502F42A`); l'originale esegue il codice SCode generato. `storm_334` (`0x15030AB0`) aggiorna entry/jump per kernel e allineamento. Questo conferma che il passaggio SCode è il punto da confrontare.
- `runtime/common/storm_sdraw_arm.c:216-344` è l'interprete SCode; `run_scode` implementa un sottoinsieme di istruzioni, mentre il kernel osservato va nel fallback. La cattura non contiene il byte dump del kernel né un confronto pixel-per-pixel con il rendering originale.

## Conclusione e fix
La diagnosi localizza il sospetto primario nel compositing del frame hover tramite kernel SCode emulato, non nella lettura SMK o nel semplice aggiornamento RGB della palette. **La causa radice esatta e la differenza rispetto all'originale non sono dimostrabili da queste evidenze**: il log non registra i byte di `size1/size2`, e gli screenshot non distinguono un blit opaco errato da un aggiornamento parziale/ordine di palette. Non attribuisco quindi il difetto a una specifica opcode o a trasparenza indice 0 senza prova.

Fix preciso da validare prima di applicarlo: acquisire il bytecode SCode del kernel 0x4A/0x2BC5 quando il testo scompare e confrontarne ogni operazione con il kernel originale generato da Storm; quindi correggere solo la semantica divergete nell'interprete (o il percorso di presentazione coinvolto). **Non** cambiare `copy_frame` per saltare pixel zero: quel buffer è intermedio e la trasparenza va verificata nel passaggio di compositing. Nessun file in `runtime/` modificato.

## Decisioni autonome
Ho seguito l'opzione conservativa: nessuna modifica al codice, dato che la cattura disponibile non prova una correzione esatta. Nessuna finestra su `:0`, nessun audio, nessun sotto-agente.
