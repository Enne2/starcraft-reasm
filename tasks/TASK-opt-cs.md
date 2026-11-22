# Missione: eliminare spin globale e scan lineare nelle CriticalSection Win32

## Contesto misurato (frigate, 2026-11-22)
Profilo a 25 FPS: `EnterCriticalSection__c` 1,95 % + `LeaveCriticalSection__c` 1,51 % dei campioni;
`strace` mostra ~2.960 `futex`/s (93 % del tempo syscall) in gameplay.
In `runtime/shim-port/elf/win32_shim.c` (~righe 1230–1275) **ogni** Enter/Leave fa:
1. `cs_lock()` = spin su `__sync_lock_test_and_set(&g_cs_lock,1)` (busy-wait globale),
2. **scansione lineare** di `g_cs[256]`,
3. `pthread_mutex_lock` (futex quando conteso).

## Obiettivo
Riscrivere il lookup delle CriticalSection in modo **lock-free nel caso comune (già registrata)**,
mantenendo **semantica Windows identica**:
- ricorsività (owner + count), `DeleteCriticalSection` (slot liberato e riutilizzabile),
  `InitializeCriticalSectionAndSpinCount` invariata nell'effetto, capacità 256 invariata
  (se piena: comportamento attuale, cioè `cs_of` restituisce NULL e Enter non blocca).
- **nessun** lock globale e **nessuna** scansione lineare nel percorso Enter/Leave di un CS esistente:
  usa una tabella hash open-addressing a dimensione potenza di 2, letture `atomic` relaxed e
  inserimento con **CAS** (slot vuoto → CAS del puntatore). Riutilizzo degli slot cancellati:
  tombstone o contatore `used`. Usa solo builtin portabili (`__atomic_*` o `__sync_*`), niente assembly.
- nessuna nuova syscall, nessun busy-wait: il caso "conteso" resta `pthread_mutex_lock` come oggi.

## Vincoli
- Modificare SOLO `runtime/shim-port/elf/win32_shim.c` (+ test in `tests/`).
- Codice C11 portabile, CPU-agnostico, nessun header specifico di architettura.
- Nessuna modifica a `host_elf.c`, `snp_elf.c`, tool, `build/starcraft-x86`.
- Non alterare altre funzioni del file.

## Verifiche richieste
1. `tests/x86_win32_shim_test` (già esistente) deve restare **PASS**; aggiungi al test un caso
   **multithread** (2–4 thread POSIX, contatore protetto, 100k incrementi) che verifichi la mutua esclusione,
   la ricorsività (`Enter` due volte / `Leave` due volte) e `Delete` + re-init.
2. Compilazione i686 **e** host x86-64 del TU: PASS (`-std=c11 -Wall -Wextra`).
3. Microbench: 10M Enter/Leave su 1 CS e su 8 CS, **prima/dopo**, thread singolo e 2 thread:
   riporta i tempi (ns/op) nel report.
4. `./tools/build.sh --arch x86 --skip-entry` deve continuare a PASSARE (puoi eseguirlo: è il build integrato).

## Deliverable
- Patch in `runtime/shim-port/elf/win32_shim.c`.
- `reports/REPORT-opt-cs.md` e `reports/STATE-opt-cs.md` con microbench prima/dopo e verifica multithread.
