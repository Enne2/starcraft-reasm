# Profiling baseline — misura riproducibile e colli di bottiglia

Data: 2026-11-26. Macchina: `frigate` (Fedora 44, AMD Ryzen 7 H 255, Radeon 780M, 16 CPU logiche).
Binario: `build/starcraft-x86` (i686), sandbox `tools/run-isolated.sh` (Xvfb + audio dummy).
Vincolo rispettato: **nessuna modifica a personaggi, asset o dati di gioco**; solo strumentazione del
runtime, script di misura e (in prospettiva) codice/build/memoria/I/O/presentazione.

## 1. Metodo

### Strumentazione aggiunta (solo misura)

| Strumento | Cosa produce |
|---|---|
| `SC_FRAME_LOG=<file>` (in `host_elf.c`) | un delta in microsecondi **per frame visibile** (commit del canvas), buffer in memoria e flush a blocchi: costo per frame = un `clock_gettime` + uno store, nessun I/O nel percorso caldo |
| `tools/prof/count-alloc.c` | `LD_PRELOAD` i686 che conta `malloc/free/calloc/realloc/mmap/munmap`, byte richiesti e istogramma per dimensione, una riga al secondo |
| `tools/prof/prof-run.py` | esegue uno scenario per N secondi, raccoglie frame/RSS/alloc/`perf`, calcola p50/p95/p99/p99.9, jitter, stutter |
| `tools/prof/latency.py` | latenza **input → primo frame visibile** su una singola connessione `sc_ctl` (niente processi per campione) |
| `sc_ctl hash` (nuovo) | hash FNV del canvas + timestamp dell'ultimo present, per misure di latenza senza PNG |

### Scenari (deterministici, senza input esterno)

| Scenario | Definizione |
|---|---|
| `menu` | menu principale animato, `SC_SKIP_INTRO=1`, nessun input |
| `gameplay` | menu → Single Player → Original → profilo → Terran → *Rebel Yell* → *Boot Camp* → Start, poi 45 s di partita ferma (unità non comandate) |
| `intro` | i due filmati introduttivi (`SC_SKIP_INTRO=0`) |

Parametri comuni: `SC_PRESENT=cpu`, `SC_PRESENT_MIN_MS=0` (nessuna coalescenza artificiale dei
present), `SC_FPS_CAP=60` nei run di gameplay, warmup 3 s scartato, 2 run per scenario.

### Metriche e definizioni

- **frame time**: intervallo fra due commit visibili del canvas;
- **p50/p95/p99/p99.9**: percentili sulla distribuzione completa dei frame (non su medie);
- **jitter**: deviazione standard dei frame time e rapporto `p99/p50`;
- **stutter**: frame con tempo > 2× p50;
- **latenza input**: da `keydown` inviato al primo `state` che riporta un frame presentato in più.

## 2. Baseline

### 2.1 Gameplay (45 s × 2 run, cap 60)

| Metrica | run 1 | run 2 | mediana |
|---|---:|---:|---:|
| frame p50 | 17.13 ms | 17.12 ms | **17.12 ms (58.4 fps)** |
| p95 | 17.90 ms | 17.86 ms | 17.88 ms |
| p99 | 18.48 ms | 18.34 ms | **18.41 ms** |
| max | 661 ms | 664 ms | 663 ms (transizione briefing→mappa) |
| fps 1% low | 54.1 | 54.5 | 54.3 |
| stutter (>2× p50) | 4 | 5 | 4.5 in 45 s |
| RSS mediana / picco | 140.5 / 146.7 MiB | 140.0 / 146.4 MiB | **140.3 / 146.6 MiB** |

Lettura: il loop di gioco è **ben paced** (p99 − p50 = **1.3 ms**); l'unico evento fuori scala è il
caricamento iniziale della mappa (già escluso dal warmup, resta la transizione al primo frame).

### 2.2 Menu (45 s × 2 run, present non coalescente)

| Metrica | run 1 | run 2 |
|---|---:|---:|
| p50 (commit del canvas) | 2.48 ms | 2.40 ms |
| p99 | 17.44 ms | 17.53 ms |
| frame al secondo (commit) | ~403 | ~416 |

Lettura: il menu **committa il canvas 3-6 volte per frame logico** (2.4 ms ciascuno) e il confine di
frame vero è ~17 ms. Il costo del singolo commit CPU (640×480, LUT palette) è quindi **~2.4 ms**, ed è
la metrica utile per ottimizzare la presentazione.

### 2.3 Allocazioni (25 s di gameplay)

| Metrica | Valore |
|---|---|
| allocazioni/s (`malloc+calloc+realloc`) | **3.982** |
| `free`/s | 3.440 |
| byte richiesti | **248 MiB/s** (8,2 GiB in 33 s) |
| distribuzione dimensioni | **76,2% ≤ 64 B**, 9,3% ≤ 256 B, 4,5% ≤ 4 KiB, 2,9% ≤ 256 KiB, 2 allocazioni > 1 MiB |

Lettura: **~68 allocazioni per frame visibile**, tre quarti delle quali sono oggetti minuscoli. È
churn classico: costo di allocatore, non di dati.

### 2.4 Latenza input → frame visibile (25 campioni per scenario)

| Scenario | p50 | p95 | p99 | max | jitter CV |
|---|---:|---:|---:|---:|---:|
| menu | **1.94 ms** | 4.70 | 5.04 | 6.78 | 0.73 |
| gameplay | **5.67 ms** | 8.35 | 9.85 | 11.56 | 0.35 |

**Trappola metodologica trovata e corretta**: il comando `sc_ctl key` contiene un
`nanosleep(50 ms)` fra keydown e keyup (per emulare una pressione), quindi la prima misura dava
50.2 ms con CV 0.001 — era lo strumento, non il gioco. La misura corretta usa `keydown`/`keyup`
separati (`tools/prof/latency.py`). Con la pressione reale la latenza è **sotto un terzo di frame**.

## 3. Profilo CPU (hotspot)

`SC_PROF=1` (campionatore in-process a 1 kHz, 335 campioni validi, uscita pulita via `sc_ctl quit`):

| % | Dove | Note |
|---:|---|---|
| **11,9%** | PC in **libc 32-bit** (`0xF7C0B2AB`) | non risolto con `addr2line` (libreria di sistema). Primo sospetto, per lettura di codice: **`memcpy` di `present_snapshot_commit`**, che copia l'intero canvas (307.200 B) a **ogni** present, e i present sono 3-6 per frame ⇒ **1-2 MB/frame** copiati |
| **5,9%** | seconda PC in libc (`0xF7F68C64`) | coerente con `malloc/free` interni, dato il churn di §2.3 |
| **4,9%** | guest `loc_40AC9A` (`0x0040AC9A`) | decodifica byte-per-riga con tabella: codice *di gioco*, è il costo più alto fra le funzioni del gioco |
| 2,7% | `arm_shadow_check_range` | validazione shadow a ogni accesso guest |
| 0,9% | `proc_range` | parsing/cache della mappa di memoria |
| 0,9% | `reasm_indirect_*` | risoluzione dei salti indiretti |
| resto | frammentato | — |

Limite dichiarato: su questa macchina **non sono installati `perf`, `valgrind` né `sysprof-cli`** e
`perf_event_paranoid=2`; quindi niente `perf stat` (IPC, cache/branch miss) né flamegraph completo.
Il campionatore interno copre il codice guest e i simboli del nostro binario, non le librerie di
sistema (per quelle servirebbe un run con `/proc/pid/maps` catturato alla stessa apertura, oppure
installare `perf`).

## 4. Colli di bottiglia, in ordine di guadagno atteso

1. **`memcpy` del canvas per ogni present** (candidato al 11,9%): la copia serve solo a `sc_ctl` e alla
   ri-presentazione, ma viene fatta sempre. Riducibile a "solo quando un consumatore la chiede" o a
   copia incrementale per righe dirty.
2. **Churn di allocazioni** (5,9% + 248 MiB/s): 76% delle allocazioni ≤ 64 B. Un pool per piccoli
   oggetti nel percorso dell'allocatore guest elimina la maggior parte delle chiamate a libc.
3. **`arm_shadow_check_range` + `proc_range`** (3,6% insieme): tabella shadow a pagina con lookup O(1)
   (implementazione già scritta e misurata nel fork `starcraft-reasm-smacker`: +61%/+91% sul
   microbench).
4. **Present multipli per frame** (menu: 3-6 commit a 2,4 ms): non è solo CPU, è anche banda di memoria;
   un commit per frame logico con dirty-rect è il taglio strutturale.
5. **`reasm_indirect`** (0,9%): cache-first prima di ogni validazione (anche questo già nel fork).
6. **Codice caldo del gioco** `loc_40AC9A` (4,9%): sostituibile con una versione C equivalente via
   `reasm_extern` (come già fatto per SComp #552), senza toccare asset né logica di gioco.

## 5. Piano di ottimizzazione incrementale (con criteri di verifica)

Ogni fase: **misura → modifica → rimisura con lo stesso comando → gate di non regressione**.

Comando di riferimento (identico per ogni fase):

```sh
python3 tools/prof/prof-run.py --scenario gameplay --seconds 45 --repeat 2 \
  --tag after-<fase> --present cpu --alloc --env SC_PRESENT_MIN_MS 0 --env SC_FPS_CAP 60
python3 tools/prof/latency.py <socket> 25          # latenza input→frame
for t in tests/*/check.sh; do bash "$t"; done       # 9/9 obbligatorio
```

**Gate di non regressione (tutti obbligatori)**: suite 9/9 · `SC_GL_VERIFY PASS: 307200` · gameplay
p99 ≤ **18.41 ms** · latenza p99 ≤ **9.85 ms** · RSS picco ≤ **147 MiB** · allocazioni/frame non
peggiori · build ARM `rc=0`.

| Fase | Intervento | Guadagno atteso | Verifica specifica |
|---|---|---|---|
| **P1** | snapshot del canvas solo su richiesta (o per righe dirty) | −5…−10% CPU, meno banda | CPU processo, p99 invariato, `sc_ctl shot` ancora corretto |
| **P2** | pool per allocazioni ≤ 64 B nel percorso guest | −3…−6% CPU, < 500 alloc/s | conteggio `count-alloc`, RSS invariato |
| **P3** | tabella shadow a pagina O(1) (da fork) | −2…−3% CPU | microbench `proc_range`/`arm_va_is_mapped` |
| **P4** | un commit visibile per frame logico + dirty rect | −20…−40% costo present | p50 menu ~17 ms con 1 commit, latenza invariata |
| **P5** | cache-first nel dispatch indiretto (da fork) | −0,5…−1% | microbench dispatch |
| **P6** | read-ahead 64 KiB su MPQ (avvio/caricamenti) | −30…−50% syscall I/O | `strace -c`, tempo di caricamento mappa |
| **P7** | `reasm_extern` per `loc_40AC9A` | −3…−5% CPU | confronto frame-hash del gioco (stesso comportamento) |

Ordine consigliato: **P1, P2** (misurabili in un ciclo corto, nessun rischio funzionale), poi **P3,
P5, P6** (import dal fork già collaudato), poi **P4** (tocca la presentazione: va misurata anche la
latenza), infine **P7** (sostituzione di codice di gioco: richiede equivalenza verificata).

## 6. Limiti di questa baseline

- **GPU non misurata in modo utile**: nella sandbox Xvfb il percorso GL cade su **llvmpipe** (software),
  quindi `gpu_busy_percent` e i tempi GPU non sarebbero rappresentativi. I numeri GPU reali
  (radeonsi, Radeon 780M) vanno presi su una sessione con GPU nativa (`gamescope --backend headless` o
  display reale) e restano da fare.
- **Nessun `perf`/flamegraph**: hotspot del codice guest e del nostro binario sì, librerie di sistema
  solo per inferenza (i due PC in libc).
- I run `menu` hanno p50 dominato dai commit multipli: per confronti "prima/dopo" sul menu va usato il
  p99 (confine di frame), non il p50.
- Un solo worker di gioco, nessun carico concorrente: misure ripetibili, non un test di carico.
