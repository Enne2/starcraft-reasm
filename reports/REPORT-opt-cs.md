# Report — ottimizzazione CriticalSection Win32

**Data:** 22 novembre 2026 — host `frigate` (Fedora Linux 44, x86-64)  
**Esito:** implementazione e verifiche principali PASS.

## Modifica

In `runtime/shim-port/elf/win32_shim.c` il lookup passa da spinlock globale + scansione del pool a una hash table open-addressing da 512 slot, con pool di 256 descrittori. Le letture degli slot sono `__atomic` relaxed; registrazione e rimozione usano CAS. I tombstone permettono il riuso dei bucket. Per un CS già registrato Enter/Leave fanno probing hash, senza lock globale e senza scansione del pool. Il lock del caso realmente conteso resta `pthread_mutex_lock`.

Owner/count sono ora letti e aggiornati atomicamente; la ricorsività resta owner + conteggio. Inizializzazione, Delete/re-init e saturazione del pool conservano la semantica prevista (oltre 256 CS, lookup/create restituisce NULL e Enter non blocca). Non sono stati aggiunti header architetturali o syscall.

Il file conteneva già modifiche non correlate nella gestione delle regioni VM (allocazione a chunk). Sono state preservate: il diff di questa attività è limitato al blocco CriticalSection. Non sono stati modificati `host_elf.c` o `snp_elf.c`.

## Verifiche

- `build/qa/opt-cs/x86_win32_shim_test`: **PASS**. Aggiunti quattro thread POSIX con 100.000 incrementi ciascuno sotto CS, ingresso/uscita ricorsivi, Delete/re-init, limite di 256 voci e riuso dopo Delete.
- Compilazione i686 C11 con `-Wall -Wextra`: **PASS**. Restano warning preesistenti non relativi a questa modifica (cast di funzione, variabile inutilizzata, formattazione).
- Compilazione x86-64 C11 con `-Wall -Wextra`: **PASS** usando il target host x86-64 e `-D__i386__` per superare il guard architetturale già presente in `win32_game.h`. La compilazione host senza quel define è intenzionalmente rifiutata dal guard («requires the ELF32 i386 runtime (or the ARMv6 runtime)»); il define non cambia il target generato.
- `./tools/build.sh --arch x86 --skip-entry`: **PASS**, exit 0; linker ha creato `build/starcraft-x86`.
- `git diff --check`: **PASS**.

## Microbench

Misura su frigate, compilazione i686 `-O2`, 10 milioni di iterazioni per thread. Ogni iterazione esegue un Enter e un Leave; ns/op è il tempo per singola chiamata. Due thread: il test a un CS contende lo stesso lock; quello a otto CS distribuisce i thread su indirizzi distinti. Tre run per configurazione; tabella con mediana. Il benchmark “prima” è stato costruito in `/tmp` sostituendo temporaneamente il blocco CS con la sua implementazione precedente; il sorgente del repository non è stato riportato indietro.

| Thread | CS | Prima (ns/Enter o Leave) | Dopo | Variazione |
|---:|---:|---:|---:|---:|
| 1 | 1 | 8,03 | 8,90 | +10,8% |
| 1 | 8 | 9,69 | 8,72 | −10,0% |
| 2 | 1 | 27,53 | 27,68 | +0,5% |
| 2 | 8 | 50,66 | 10,39 | −79,5% |

Il caso con un solo lock conteso è dominato dal mutex e sostanzialmente invariato; la riduzione maggiore è nel caso a due thread su CS distinti, dove sparisce la contesa sul precedente spinlock globale. Le misure sono microbenchmark di processo e non sostituiscono una nuova acquisizione del profilo gameplay/futex.

## Decisioni autonome

- Ho mantenuto intatte tutte le modifiche preesistenti del working tree, incluse quelle concorrenti fuori dal perimetro.
- Ho usato 4 thread (il limite superiore richiesto) e mediana di 3 run per contenere l’effetto della variabilità temporale.
- Per la verifica host ho rispettato il guard architetturale del progetto tramite `-D__i386__`, mantenendo il target effettivo x86-64 e segnalando la limitazione.

## File

- Modificato: `runtime/shim-port/elf/win32_shim.c` (blocco CriticalSection; modifiche preesistenti VM conservate).
- Modificato: `tests/x86_win32_shim_test.c`.
- Aggiunti: `reports/REPORT-opt-cs.md`, `reports/STATE-opt-cs.md`.
- Artefatti di prova: `build/qa/opt-cs/`.
