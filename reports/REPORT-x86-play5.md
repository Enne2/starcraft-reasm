# Report — scx86-play5 (21/11/2026)

## Esito

Causa radice esatta dello STOP `0x0435A008` dopo Briefing → Start identificata nel ramo backward-copy di `memmove`/`memcpy` (`0x00408FD0..0x004092B8`, specificamente `loc_409194`). Per ordine del supervisore a soglia 180k token, la sessione è stata fermata subito dopo la diagnosi, con `play5` chiuso e il fix pronto da applicare al prossimo turno.

## Diagnosi esatta di `0x0435A008` (`loc_409194` / `@loc_409268` in `%__text`)

1. **Percorso chiamante (`loc_46A4D2` → `0x00469E50` → `0x00408FD0`)**:
   - `llvm-nm` mostra che `0x0813D4A0` è `loc_42D8EF`.
   - `loc_46A4D2` chiama `0x00469E50`, che a `0x00469EAA` invoca `0x00408FD0` (`memmove`/`memcpy`) per spostare in avanti di 8 byte un blocco di `0x18` (24) byte (`dst = src + 8`, `esi = 0x0066FF88`, `edi = 0x0066FF90`, `len = 0x18`).
   - Poiché `edi > esi` e `edi < esi + ecx` (copia sovrapposta), `0x00408FEA` prende il ramo backward `jb 0x40916C` (`loc_40916C`).
   - Con `edi` allineato a 4 byte e `len = 24`, `loc_40917C` calcola `ecx = 24 >> 2 = 6` (`< 8`) e salta a `loc_409194`:
     - `409194: f7 d9` → `negl %ecx` (`_ecx = -6 = 0xFFFFFFFA`)
     - `409196: ff 24 8d 68 92 40 00` → `jmpl *0x409268(,%ecx,4)`

2. **Perché `loc_409194` legge `0x0435A008` invece della tabella PE a `0x00409250`**:
   - In `build/lift-full/entry-x86/StarCraft.entry.ll` ( riga 97123), `loc_409194` emette:
     `%6 = add i32 ptrtoint (i8* @loc_409268 to i32), %5`
   - A differenza di `@loc_40912C` (che è una funzione `void(%_cpu*)*` già intercettata da `RE_TABLE_BASE` in `tools/dispatch-ir.py`), `@loc_4090B0`, `@loc_40911C`, `@loc_409268`, `@loc_4092B8` (righe 4888–4891) sono definiti da `llasm` come alias `i8*` dentro la struct globale compattata `%__text* @_text`:
     - `@loc_4090B0 = private alias i8, i8* getelementptr (..., @_text, i32 232)`
     - `@loc_40911C = private alias i8, i8* getelementptr (..., @_text, i32 268)`
     - `@loc_409268 = private alias i8, i8* getelementptr (..., @_text, i32 344)`
     - `@loc_4092B8 = private alias i8, i8* getelementptr (..., @_text, i32 352)`
   - Poiché `%__text` impacchetta solo i frammenti dati estratti dal `.text` senza preservare gli offset relativi nè i byte di codice intermedi, `@loc_409268` (+344) si trova solo 8 byte prima di `@loc_4092B8` (+352) e 76 byte dopo `@loc_40911C` (+268)!
   - Quando `loc_409194` indicizza `@loc_409268` con `ecx = -6` (`-24` byte), legge da `@_text + 344 - 24 = @_text + 320` (fuori dalla entry `@loc_409268`), caricando `0x0435A008` invece del VA guest reale in `.guest_exe_text_0` (`0x00409268 - 24 = 0x00409250`)!

## Isolamento e stato

- Solo `./tools/run-isolated.sh` usato; `./tools/run-isolated.sh stop --id play5` eseguito, nessun processo `starcraft-x86` o `Xvfb` attivo.
- `qmem_store` aggiornato (`5eb203b7-6b13-4213-a707-337415cde624`).

### Decisioni autonome

Fermata immediata all'ordine di soglia 180k token senza avviare la ricompilazione completa di `StarCraft.entry.ll`, lasciando il fix esatto documentato in `## Prossimo passo`.

## Prossimo passo

1. In `tools/dispatch-ir.py`, estendere `RE_TABLE_BASE` (o la riscrittura di `ptrtoint`) per intercettare anche `ptrtoint (i8* @loc_([0-9A-Fa-f]+) to i32)` quando il VA cade in `.text` (`0x00401000 <= va < 0x004FE000` per `StarCraft.ll`, o in generale per gli alias `@loc_<VA>` verso `@_text`), sostituendolo con l'intero del VA guest `va` (poiché `.guest_exe_text_0` è mappata esattamente ai VA originali `0x00401000..0x004FE000` con i byte completi del PE).
2. Ricostruire con `./tools/build.sh --arch x86` (senza `--skip-entry`) e verificare `./tests/x86_runtime_check/check.sh`.
3. Rilanciare `./tools/run-isolated.sh start --id play5 --skip-intro 1 --smack 1`, navigare fino a Briefing → Start (`click 538 390`) e verificare con `shot` + `read` l'ingresso nel Tutorial / Prima Missione.
