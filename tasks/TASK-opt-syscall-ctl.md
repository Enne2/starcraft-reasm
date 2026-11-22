# Missione: eliminare le syscall inutili del control-file (SC_CONTROL_FILE)

## Contesto misurato (frigate, 2026-11-22)
In una run isolata (`./tools/run-isolated.sh`, che imposta `SC_CONTROL_FILE`), durante il gameplay a 25 FPS
`strace -c` misura ~2.400 `openat`/s, ~3.400 `read`/s e ~2.400 `_llseek`/s, quasi tutti su
`build/isolated/<id>/cmd.txt`. Causa: `host_elf.c` (~riga 1360) fa `open()` **ad ogni pump** e legge
**un byte per `read()`**. Questo inquina anche le misure di performance e consuma CPU inutilmente.

## Obiettivo
Riscrivere il percorso del control-file mantenendo **semantica identica** osservabile:
- **fd persistente**: `open()` una sola volta; se il file non esiste, ritentare con backoff (es. 250 ms).
- **lettura a blocchi** (es. 512–1024 B) con buffer interno, invece di 1 byte per `read()`;
  gestire correttamente righe spezzate tra due letture, `sizeof cline` (256) e `ctrl_off`.
- comportamento invariato per: `focus | move X Y | down X Y [btn] | up X Y [btn] | click X Y [btn] |
  keydown K | keyup K | key K | text STR`, righe vuote, commenti `#`, `ctrl_pending`/`ctrl_due_ms`.
- se il file viene **troncato/riscritto**, ripartire dall'inizio (rilevare `st_size < ctrl_off`);
  se viene **ruotato/sostituito**, riaprire (confronto `st_dev`/`st_ino`).
- nessun busy-wait aggiuntivo: se non ci sono byte nuovi, uscire subito (come ora).

## Vincoli
- Modificare SOLO `runtime/shim-port/elf/host_elf.c` (+ eventuale test).
- Codice **C11 portabile, CPU-agnostico, senza assembly**: usare solo `open/read/lseek/fstat`
  già dichiarati come `extern` nel file (aggiungere le dichiarazioni mancanti, es. `fstat`/`struct stat`
  se serve — meglio evitare e usare `lseek(fd,0,SEEK_END)` per la dimensione).
- Nessuna modifica a `snp_elf.c`, `win32_shim.c`, ai tool, a `build/starcraft-x86`.
- Nessun rebuild globale: verifica con compilazione del singolo TU (i686 e host x86-64, `-std=c11 -Wall -Wextra`).

## Verifiche richieste
1. Compilazione i686 + x86-64 del TU: PASS.
2. Test funzionale reale: `./tools/run-isolated.sh start --id ctl-test --skip-intro 1 --smack 1`,
   poi una sequenza `cmd` (focus, move, click, click...) e screenshot con `shot` **che dimostri
   che i click arrivano** (es. menu → Select Game Type). Poi `strace -q -c -f -p <pid> -e trace=openat,read,lseek`
   per 3 s e riporta i conteggi **prima/dopo** (prima: ~2.400 openat/s, ~3.400 read/s).
3. Verifica che senza `SC_CONTROL_FILE` non cambi nulla (nessuna nuova syscall).

## Deliverable
- Patch in `runtime/shim-port/elf/host_elf.c`.
- `reports/REPORT-opt-ctl.md` e `reports/STATE-opt-ctl.md` con numeri prima/dopo e comandi.
