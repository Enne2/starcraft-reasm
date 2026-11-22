# Report — gfx-fix2 (parziale)

## Esito
- Aggiunto `test_rop3()` in `tests/x86_gfx_test.c`: esercita `DDBLT_ROP` con SRCCOPY, SRCAND, SRCPAINT, SRCINVERT e PATCOPY, usando pixel src/dst non nulli e pattern `0xC3`; verifica i pixel risultanti.
- Compilazione QA i686 aggiornata in `build/qa/gap-gfx/`; `./build/qa/gap-gfx/x86_gfx_test`: **PASS**, inclusi clipper, SetDIBits e i cinque ROP3.
- `./tools/build.sh --arch x86 --skip-entry`: PASS; un warning preesistente su `host_elf_ticks` in `input_elf.c`.
- `./tests/x86_runtime_check/check.sh`: PASS. `readelf -h build/starcraft-x86`: ELF32 EXEC Intel 80386. `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86`: 102392 voci ordinate/univoche.

## Verifica visiva isolata
- Run avviato esclusivamente tramite `./tools/run-isolated.sh start --id gfx-v3 --skip-intro 1 --smack 1`, con `SC_SCODE_DUMP=1 SC_SMACK_TRACE=1`; runner fermato.
- Hover Main Menu: `build/qa/gfx-v3-hover.png`, testo Single Player visibile; un solo capture, non campioni temporizzati 0/150/400/1000 ms.
- `Select Game Type`: `build/qa/gfx-v3-afterclick.png`; pannello nero uniforme con bordo rosso e loghi/testi presenti. `Registry`: `build/qa/gfx-v3-registry-retry.png`; area lista nera, un profilo visibile. Nessuna verifica del MENU in-game.
- SCode diagnostic dump attivo; il log contiene dump dei buffer ma non è stata isolata una correlazione pixel/call-site che dimostri quale ROP, SCode o GDI disegni i pannelli neri. Disassemblato l'originale Storm `storm_334` da `0x15030AB0` con `llvm-objdump`; non è stato eseguito confronto byte-per-byte del kernel originale/runtime specifico all'hover.

## Limiti / cause e fix
La correzione ROP3 generale preesistente in `com_ddraw.c` è ora coperta dai test, ma l'associazione con i pannelli visualizzati non è dimostrata. Bug hover non chiuso: il singolo hover risulta leggibile, ma mancano le quattro acquisizioni temporizzate e un confronto causale. Bug campagne idle non chiuso: questa sessione non ha raggiunto la schermata selezione campagne né confrontato le chiamate idle/hover `SmackToBuffer`/`storm_457`/`storm_334`. Non ho applicato patch speculative a SCode, Smack o GDI.

## Decisioni autonome
Scelta conservativa: mantenere il comportamento attuale dove manca prova causale, evitando fix speculativi; dichiarare la missione parziale anziché attribuire i sintomi al ROP solo per correlazione visiva. Nessun sotto-agente, nessuna finestra su `:0`, nessun audio.
