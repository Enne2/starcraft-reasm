# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` (`0x004FDE60..0x004FDE6B`, `0x004FD3A0..0x004FD3C0`, `0x00404F09`) e `rg` mirato su `build/lift-full/StarCraft.llasm` / `code_entry_points.sci`.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai aggiungere target fittizi o allentare `reasm_indirect_resolve`! Se `0x004FDE60` va tradotta, promuovila tramite `code_entry_points.sci` / lifting reale o inspect come funziona `StarCraft.ll` attorno a `0x004FDE60`.
4. **Isolamento**: MAI finestre su `:0`/`WAYLAND_DISPLAY`, MAI audio reale! Usa `./tools/run-isolated.sh` e chiudi sempre con `stop`.
5. **Divieti**: niente sotto-agenti, niente `git`, niente input desktop, non toccare `vendor/`.

---

# Missione `scx86-close-build`: Fix `x86_gfx_test.c`, traduzione/fix callback on-exit `0x004FDE60`, build completo + tutti i 7 test + Exit pulito

Leggi prima:
- `reports/STATE-gap-exit.md` e `reports/REPORT-gap-exit.md`
- `reports/STATE-gap-gfx.md` e `reports/REPORT-gap-gfx.md`
- `reports/STATE-x86-merge.md`

## Obiettivi e ordine di esecuzione

1. **Fix `tests/x86_gfx_test.c` (`stdcall` COM casts) e verifica `com_ddraw.c` (`blit_clipped`) + `gdi_elf.c` (`SetDIBits`)**:
   - Correggi i cast dei puntatori a funzione COM in `tests/x86_gfx_test.c` usando `__attribute__((stdcall))` (coerente con `com_ddraw.c`).
   - Compila ed esegui `build/qa/gap-gfx/x86_gfx_test` (usando `com_ddraw.c` e `gdi_elf.c` reali): deve stampare **PASS** verificando `SetClipList`/`GetClipList`/`IsClipListChanged`, `SetClipper`/`GetClipper`, `Blt` clippato (pixel dentro aggiornati, fuori invariati) e `SetDIBits` `8↔24/32 bpp`.
   - Se emerge un bug in `blit_clipped` di `com_ddraw.c`, correggilo e riprova `x86_gfx_test`!

2. **Risolvi l'on-exit callback `0x004FDE60` (`0x004FDE60..0x004FDE6B`)**:
   - Controlla con `rg -n '4FDE60|4fde60|4FDE70|4fde70' build/lift-full/StarCraft.llasm build/lift-full/code_entry_points.sci build/lift-full/funcs-exe.txt build/lift-full/entry-x86/StarCraft.entry.ll`!
   - Verifica perché `0x004FDE70` È nella tabella di dispatch, mentre `0x004FDE60` (`push 0x006D62B8; call [0x004FE204]; ret`, che subito precede `0x004FDE70`!) non c'è:
     - Se `loc_4FDE60` è già una label/funzione in `StarCraft.llasm` / `StarCraft.ll` o se `build/lift-full/bin/SRW-llasm` + `llasm` (vedi `tools/lift-full.sh`!) usano `code_entry_points.sci`, verifica in `tools/lift-full.sh` / `tools/build-entry-x86.sh` come aggiungere `0x004FDE60` (es. aggiungendo `0x004FDE60` a `code_entry_points.sci` e ri-liftando/rigenerando `StarCraft.ll` se veloce, OPPURE controllando se `@loc_4FDE60` / `@sc_loc_4FDE60_impl` o il blocco `0x004FDE60` può essere estratto/aggiunto in `StarCraft.ll` senza rompere nulla!).
     - Controlla quanto impiega `tools/lift-full.sh` (leggi `tools/lift-full.sh` prima!): se `0x004FDE60` è già in `StarCraft.ll` come basic block interno o è liftabile in pochi secondi con `SRW-llasm` / `llasm` o con una funzione IR equivalente al blocco `0x004FDE60`, scegli la via più pulita e verificabile!

3. **Build completo unificato (`./tools/build.sh --arch x86`) e verifica di TUTTI i test**:
   - Esegui `./tools/build.sh --arch x86` (se hai rigenerato `StarCraft.ll`, con entry rebuild; altrimenti `--skip-entry` se `entry-x86` è già aggiornato) -> deve uscire con `0`.
   - Verifica:
     - `./tests/x86_fastcc_check/check` -> PASS
     - `./tests/x86_runtime_check/check.sh` -> PASS
     - `LC_ALL=C readelf -h build/starcraft-x86` -> `ELF32 / EXEC / Intel 80386`
     - `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` -> `0` collisioni
     - Tutti i 5 test unitari compilati ed eseguiti con PASS:
       - `x86_win32_shim_test`
       - `x86_saveload_test`
       - `x86_snp_lan_test`
       - `x86_dsound_test`
       - `x86_gfx_test`

4. **Test di chiusura pulita `Menu -> Exit` (`--id close-exit`)**:
   - Avvia `./tools/run-isolated.sh start --id close-exit --skip-intro 1 --smack 1`.
   - Cattura screenshot, verifica coordinata `Exit` (~`(510, 328)` o `(220, 405)` — controlla nello screenshot!), clicca `Exit`!
   - Controlla se il processo esce pulitamente con `exit 0` (oppure se incontra un secondo callback on-exit successivo a `0x004FDE60`, diagnosticalo subito e risolvilo allo stesso modo fino a **`exit 0`** pulito!).
   - Chiudi sempre `./tools/run-isolated.sh stop --id close-exit`.

5. **Report, STATE e `qmem_store`**:
   - Scrivi `reports/REPORT-close-build.md` e `reports/STATE-close-build.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
