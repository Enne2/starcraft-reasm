# Stato — scx86-play5 (21/11/2026)

- **Causa radice di `0x0435A008` trovata**:
  - `0x0813D4A0` = `loc_42D8EF`.
  - Da `loc_46A4D2` → `0x00469E50` (`0x00469EAA`), `StarCraft` chiama `0x00408FD0` (`memmove`/`memcpy`) con `esi = 0x0066FF88`, `edi = 0x0066FF90` (`dst = src + 8`), `len = 0x18` (24 byte).
  - Poiché `edi > esi` e `edi < esi + len`, `0x00408FEA` prende il ramo backward `jb 0x40916C` (`loc_40916C` → `loc_40917C` → `loc_409194`), dove `ecx = 24 >> 2 = 6` viene negato (`_ecx = -6 = 0xFFFFFFFA`) e usato in `jmp *0x409268(,%ecx,4)`.
  - In `build/lift-full/entry-x86/StarCraft.entry.ll` (riga 97123), `loc_409194` usa `%6 = add i32 ptrtoint (i8* @loc_409268 to i32), %5`.
  - `@loc_4090B0`, `@loc_40911C`, `@loc_409268`, `@loc_4092B8` (righe 4888–4891) sono `private alias i8, i8* getelementptr (..., %__text* @_text, i32 <offset_compresso>)` (`232`, `268`, `344`, `352`), NON funzioni `void(%_cpu*)*` catturate da `RE_TABLE_BASE`!
  - Poiché `%__text` compatta solo i frammenti dati estratti da `llasm`, l'indice negativo `-24` da `@loc_409268` (`@_text + 344 - 24 = @_text + 320`) esce dalla sotto-tabella e legge `0x0435A008` invece dei byte del PE a `0x00409268 - 24 = 0x00409250` in `.guest_exe_text_0`.
- **Isolamento**: `./tools/run-isolated.sh stop --id play5` eseguito; nessun processo `starcraft-x86` o `Xvfb` attivo.
- **Memoria**: `qmem_store` aggiornato (`5eb203b7-6b13-4213-a707-337415cde624`).

## Prossimo passo

1. In `tools/dispatch-ir.py`, estendere `RE_TABLE_BASE` per sostituire anche `ptrtoint (i8* @loc_<VA> to i32)` quando `@loc_<VA>` è un alias dentro `@_text` (`.text` del PE, `0x00401000..0x004FE000`), usando il VA intero `0x<VA>` (mappato in `.guest_exe_text_0`).
2. Eseguire `./tools/build.sh --arch x86` (senza `--skip-entry`) e `./tests/x86_runtime_check/check.sh`.
3. Rilanciare `./tools/run-isolated.sh start --id play5 --skip-intro 1 --smack 1`, cliccare Briefing → Start (`click 538 390`) e verificare con `shot` + `read` l'ingresso nel Tutorial / Prima Missione.
