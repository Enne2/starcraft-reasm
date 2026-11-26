# Report P3 — allocazioni piccole (esito parziale)

## Esito
**Implementazione non applicata.** Il call-site attribution richiesto non è stato ottenuto in modo affidabile: il tentativo Massif non ha prodotto il file di profilo prima che il runner terminasse forzatamente il processo. Il profiler ordinario non ha potuto misurare malloc/s perché `build/prof/count-alloc.so` non era presente. Senza questa attribuzione, intervenire su `HeapAlloc` o costruire un interposer globale sarebbe speculativo e potrebbe cambiare la semantica/ownership di allocazioni estranee.

## Chi alloca — evidenza disponibile
- Il dato fornito nel brief attribuisce 3.982 allocazioni/s e 46,2 allocazioni/frame al processo, con il 76,2% di taglia ≤64 B; non contiene call-site.
- L'ispezione del codice mostra che `runtime/shim-port/elf/win32_shim.c:HeapAlloc` chiama `malloc` e azzera il buffer, mentre `HeapFree` chiama `free`. `HeapReAlloc` e `HeapSize` usano rispettivamente `realloc` e `malloc_usable_size`.
- `runtime/common/game_memory.c` è l'allocatore di arena bassa specifico ARM (`arm_alloc_low`), non l'allocatore libc del percorso x86; modificarlo non raggiungerebbe il churn x86 misurato.
- L'uso di `malloc/free` è distribuito anche in altri shim/runtime. I dati raccolti non consentono di distinguere allocazioni guest via HeapAlloc da allocazioni interne al runtime.

## Misure e tentativi
- Build: `./tools/build.sh --arch x86 --skip-entry` — **rc 0**. Ha usato gli artefatti condivisi in sola lettura tramite i symlink indicati nel brief.
- Massif: avviato tramite `SC_BIN` wrapper in scenario gameplay. L'app è stata troppo rallentata dal profiler; il runner ha terminato il processo e non è stato scritto `/tmp/lan-p3-massif.out`. Nessuna conclusione sui call-site.
- Profiler normale, `python3 tools/prof/prof-run.py --scenario gameplay --seconds 30 --repeat 1 --tag p3-baseline --alloc`: 1.954 frame campionati; p50 12,21 ms, p99 24,99 ms; RSS mediano 137,5 MiB, picco 144,1 MiB, USS mediano 66,8 MiB. **Allocazioni/frame e malloc/s non disponibili**: il target `build/prof/count-alloc.so` mancava e il run non ha prodotto `alloc.txt`. Numeri contaminati/non confrontabili con la baseline dichiarata nel brief; nessuna misura post-fix perché nessun fix è stato applicato.
- Test `tests/*/check.sh`, screenshot menu/gameplay e completamento missione: **non eseguiti**; non c'è una modifica da validare e il tentativo Massif non è arrivato al gameplay osservabile.
- ARM: non verificata; non è stato modificato codice condiviso e `game_memory.c` ARM non è stato toccato.

## Implementazione, perdite e thread-safety
Nessun pool aggiunto: nessun contatore d'uso, tetto d'arena o fallback introdotti. Thread-safety e blocchi in uso a fine partita non sono quindi applicabili/non misurati.

## Decisioni autonome
Scelgo di non aggiungere un pool finché non è noto il call-site, evitando di intercettare soltanto `HeapAlloc` (potenzialmente non responsabile) o di cambiare globalmente `malloc/free` (rischio di alterare anche allocazioni del runtime, `realloc`, e ownership). Prossimo passo necessario: ottenere un profilo con stack/call-site affidabili o strumentare il contatore esistente per registrare gli stack, quindi selezionare il punto d'intervento e ripetere build, test e misure.

## Artefatti
- `build/qa/p3/LAN-FIX-P3.patch`: patch vuota (nessuna sorgente modificata).
- Nessuna modifica a immagine guest, dati di gioco, `host_elf.c` o `com_ddraw.c`.
