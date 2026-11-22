# Stato — port dispatch

- A1/D1 cache indiretta per-CPU (offset `_ind_cache` 144): integrata.
- A2 shadow page table 1 MiB e percorso `proc_range`: integrati; fix SCode/video originali preservati.
- C `#552 SCompDecompress`: `storm_scomp.c`, guest-call helper e hook IR/stub assembly integrati.
- D3 `SC_AFFINITY`: file header presente; applicazione opzionale in main/thread guest.
- `dispatch-ir.py`: RE_TABLE_BASE, RE_TEXT_ALIAS, RE_PTRTOINT_TEXT mantenute; build ha corretto 612 ptrtoint alias `_text`.
- `gen_game_layout.py`: ALIAS_RE estesa; parse dei blob exe/storm verificato. Nessun parametro `--arch` nell'interfaccia corrente.
- Entry x86 ricostruiti: PASS; `@loc_4FDE60`, `%_cpu` allungato e wrapper/originale #552 verificati.
- Test shadow: PASS. Test SComp: PASS (3.798/3.798 decode nativi identici; zero differenze).
- Compilazione componenti in `/tmp/disp-check/`: PASS.
- Limite: invocazione completa di gen_game_layout con entry-x86 fallisce per `.rodata..L_text` assente; non alterato il linker/layout per aggirarlo.
- Nessun commit. Report dettagliato: `reports/REPORT-port-dispatch.md`.
