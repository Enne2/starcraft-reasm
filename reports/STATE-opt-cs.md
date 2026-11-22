# Stato — TASK-opt-cs

**Completato — 22/11/2026, frigate.**

- [x] Lookup hash open-addressing CAS; letture atomiche relaxed; tombstone e riuso slot.
- [x] Pool/capacità 256 invariata; Enter su pool pieno non blocca; ricorsività e Delete/re-init.
- [x] Test multithread: 4 thread × 100.000 incrementi protetti, ricorsività, Delete/re-init, full-capacity/riuso — PASS.
- [x] Compilazione C11 i686 e host x86-64 — PASS (host con `-D__i386__` necessario al guard di `win32_game.h`).
- [x] `./tools/build.sh --arch x86 --skip-entry` — PASS.
- [x] Microbench prima/dopo riportato in `reports/REPORT-opt-cs.md`.

## Microbench — mediana ns per Enter/Leave

| Thread | CS | Prima | Dopo |
|---:|---:|---:|---:|
| 1 | 1 | 8,03 | 8,90 |
| 1 | 8 | 9,69 | 8,72 |
| 2 | 1 | 27,53 | 27,68 |
| 2 | 8 | 50,66 | 10,39 |

## Perimetro e note

`win32_shim.c` era già modificato all’inizio del task per un’ottimizzazione VM concorrente; tale modifica non è stata toccata. `host_elf.c` e `snp_elf.c` non sono stati modificati in questa attività. Nessun revert o stash effettuato. I warning delle compilazioni sono preesistenti e non bloccanti; dettagli e metodo benchmark sono nel report.
