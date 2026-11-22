# Stato — diagnosi campagne

- **Esito:** riproduzione riuscita in Xvfb id `diag-camp`; stop eseguito. Audio dummy; nessuna finestra su `:0`.
- **Immagini lette:** `reports/idle.png`, `hover-terran.png`, `hover-zerg.png`, `hover-protoss.png`. Idle nero; ogni hover mostra correttamente solo il personaggio corrispondente.
- **Sorgenti esaminate:** `runtime/shim-port/elf/smack_elf.c` (Open/ToBuffer/DoFrame/NextFrame/Wait), `runtime/common/storm_sdraw_arm.c`, `runtime/shim-port/elf/com_ddraw.c` (Blt/SetEntries).
- **Prova runtime:** i frame SMK hanno pixel non-zero; hash del frame e del buffer destinatario coincidono; flags ToBuffer zero. Nessun errore decodifica/flag o fail73. Il decoder è escluso come causa dimostrata.
- **IDA read-only:** `storm_457` `0x1502F500` → `sub_1502EFA0` `0x1502EFA0` → `storm_334` `0x15030AB0`; la catena di compositing è candidata, ma manca il confronto caller/flags/rect idle-hover.
- **Causa e fix esatti:** non stabiliti con le evidenze raccolte; nessuna patch autorizzata dai dati. Prossimo passo: tracciare in IDA StarCraft originale il callback campagna e confrontare ramo idle/hover fino al blit.
- Nessuna modifica a `runtime/` o `build/starcraft-x86`; nessun sotto-agente. Sessione IDA chiusa.

## Decisioni autonome

- Conservativa: nessun cambiamento speculativo a decoder, palette o blitter; report esplicita il limite invece di inventare una causa.
