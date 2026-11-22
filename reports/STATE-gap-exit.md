# Stato — scx86-gap-exit

- **Esito:** parziale; no fix on-exit/exit 0. D19 invariato.
- **Build testata:** copia `build/qa/gap-exit/starcraft-x86-exit` da `build/starcraft-x86`.
- **Isolamento:** `run-isolated.sh` su Xvfb `:132`, audio dummy; menu raggiunto; istanza fermata via `stop`.
- **Exit:** click sintetico a circa `(510,328)`. Log `build/isolated/gap-exit/game.log`: STOP target guest `0x004FDE60`, caller `0x00404F26`, non `0x1805F2D4`; niente exit 0.
- **Writer verificato staticamente:** `__onexit_lk` scrive EDI in `[0x006DD684]` a `0x004060B4`; caller a `0x004FD3AB` passa `0x004FDE60`. Codice callback PE a `0x004FDE60–0x004FDE6B` valido, non mappato dalla dispatch corrente.
- **Diagnosi:** gap di entry nella dispatch map, non prova di callback arbitrario/corrotto. Per fix: tradurre/promuovere entry `0x004FDE60`, rigenerare `reasm_va_va/reasm_va_fn`, verificare e ritestare. Non applicato: pipeline fuori proprietà; `--map-extra` privo di un simbolo tradotto da referenziare.
- **GDB:** watchpoint su `0x006DD684` ha visto il decremento di doexit (host PC `0x0801AD9F`), non lo store iniziale; log `build/qa/gap-exit/gdb-watch.log`.
- **DirectSound:** nuovo `tests/x86_dsound_test.c`; `clang -m32` e test PASS. Copertura COM richiesta + 40 buffer / LRU su 32 voci stub; nessun audio reale. Binary `build/qa/gap-exit/x86_dsound_test`.
- **SDL:** SDL2 compat (symlink target `libSDL2-2.0.so.0.3200.74`) e SDL3 (`libSDL3.so.0.4.20`) esportano Show/HideWindow e PumpEvents. `host_elf.c` usa solo `SDL_WINDOW_SHOWN` + hint bypass; la run Xvfb crea/presenta la finestra. Hidden/pump/show non provato; nessuna modifica.
- **File modificato:** `tests/x86_dsound_test.c`. Nessuna modifica a runtime, resolver, pipeline o host_elf.
- **Report:** `reports/REPORT-gap-exit.md`.

## Decisioni autonome

Fail-closed conservato; nessuna whitelist o fallback non verificato. Nessuna modifica SDL non misurata. Il task resta incompleto finché il dispatch gap non viene corretto nella pipeline e Exit non termina con codice 0.
