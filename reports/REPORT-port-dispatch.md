# Report — port dispatch A1/A2/D1/C

## Esito
Portati i blocchi dispatch/cache per-CPU, shadow page table, #552 SCompDecompress, affinity opzionale e fix regex `ALIAS_RE`. Preservate le regole locali di `dispatch-ir.py` per `RE_TABLE_BASE`, `RE_TEXT_ALIAS`, `RE_PTRTOINT_TEXT` e i fix di SCode/video in `storm_sdraw_arm.c`.

## Modifiche
- `runtime/common/cpu_llasm.{c,h}`: cache indiretta privata per CPU a offset 144 e rilascio in destroy.
- `runtime/common/indirect_dispatch.c`, `runtime/arch/{x86/indirect_dispatch_x86.S,arm/indirect_dispatch.S}`, fixture x86: fast path assembly per-CPU, cache-first e stub `storm_552_arm`.
- `runtime/common/game_memory_arm.{c,h}` e `storm_sdraw_arm.c`: shadow map da 1 MiB; `proc_range` esportata per il test e usa la shadow map; restano `storm_video_skip_enabled`, accessi SCode 8A/8B, kernel non-LUT e `SC_SCODE_DUMP`.
- `runtime/common/storm_scomp.c`, `guest_call_arm.c`, `tools/dispatch-ir.py`: fallback/invocazione lifted per #552 e wrapper `storm_552_arm`; mantenuti i tre fix per tabelle e alias `_text`.
- `runtime/common/game_main_arm.c`, `guest_call_arm.c`, nuovo `thread_affinity.h`: supporto `SC_AFFINITY` opzionale.
- `runtime/common/gen_game_layout.py`: regex alias estesa a `%__[a-z]+*` senza cambiare il resto del layout.
- Copiati `tests/x86_shadow_mem_test.c`, `tests/x86_shadow_mem_test/`, `tests/x86_scomp_test/`, `tests/x86_perf_bench/`.

## Verifiche
- `./tools/build-entry-x86.sh`: PASS; genera gli entry IR/O i386. Output registra 12 fix `RE_TABLE_BASE` e 612 `@_text` (`545` exe + `67` storm). `%_cpu` termina con `i8*`; `@loc_4FDE60` presente. Hook #552 presente e l'originale conservato come `@orig_storm_552`.
- `./tests/x86_shadow_mem_test/check.sh`: PASS.
- `./tests/x86_scomp_test/check.sh`: PASS; 4.292 settori, 3.798 identici, 0 differenze, 494 fallback; benchmark mediano PKWARE 132,26 MiB/s.
- Compilazione i686 in `/tmp/disp-check/`: `storm_sdraw_arm.c`, `indirect_dispatch.c`, `cpu_llasm.c`, `storm_scomp.c`, `indirect_dispatch_x86.S`, `game_main_arm.c`, `guest_call_arm.c`: PASS.
- `ALIAS_RE` validata sugli IR exe/storm: recuperati `_text`, `_rdata`, `_data`; test regex su alias `__data`, `__text`, `__rdata`: PASS.

## Limiti
`gen_game_layout.py` non espone `--arch`; il tentativo completo con gli entry-x86 termina perché l'oggetto non contiene `.rodata..L_text` (ha `.text`, `.rodata`, `.data..L_data`, `.rodata..L_rdata`). Non ho aggiunto opzioni o modificato il comportamento x86 per mascherare questa incompatibilità. Non eseguiti smoke test del gioco o benchmark dispatch.

## Decisioni autonome
Ho mantenuto l'interfaccia esistente di `gen_game_layout.py` e verificato la regex direttamente sugli IR, invece di introdurre opzioni architetturali non presenti. Nessun commit creato.
