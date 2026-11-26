# Piano — ridurre il carico CPU del rendering (e secondariamente la RAM)

Data: 2026-11-26 · Macchina: `frigate` (Ryzen 7 H 255 + Radeon 780M) · Binario `build/starcraft-x86`
Baseline di riferimento: `reports/PROF-BASELINE-2026-11-26.md`
**Nessuna modifica applicata**: questo documento è l'analisi e il piano da approvare.

## 1. I layer in gioco (chiarezza sui nomi)

Nel codice non esiste un simbolo "SPL": quello che il progetto chiama *layer sprite/pixel del gioco*
è composto da tre strati distinti, e vale la pena tenerli separati perché hanno costi diversi:

| Strato | Dove | Cosa fa |
|---|---|---|
| **A. Blitter del gioco (SPL/SCode)** | `runtime/common/storm_sdraw.c` (`storm_313_arm_body`, `storm_334_arm_body`, kernel 1×/2× LUT) | esegue i kernel SCode generati da Storm: compone sprite, testo, effetti nel **canvas 8bpp** |
| **B. Superfici DirectDraw** | `runtime/shim-port/elf/com_ddraw.c` (`s_Unlock`, `s_Blt`, `s_Flip`) | decide *quando* presentare; oggi presenta su Unlock/Blt/Flip con coalescenza 12 ms |
| **C. Presentazione host** | `runtime/shim-port/elf/host_elf.c` | tre percorsi alternativi: `present_gl` (texture `GL_R8` 8bpp + shader palette), `present_gpu` (conversione CPU→ARGB8888 + `SDL_Renderer`), `present_scaled`/`fb_draw` (conversione CPU + superficie SDL o framebuffer) |

Selettore attuale (`SC_PRESENT`): `fb` = framebuffer, prefisso `c` (es. `cpu`) = **solo percorso CPU**
(SDL software surface, niente GL/GPU), prefisso `gp` = salta GL, default = **GL → GPU → CPU**.

## 2. Dove sta il costo CPU del rendering (misurato)

### 2.1 Profilo a livello di istruzioni (callgrind, scena menu, 30,8 G istruzioni)

| % istruzioni | Chi | Lettura |
|---:|---|---|
| **58,6%** | regione eseguibile anonima `0x057a5a20` | **codice JIT di llvmpipe** (rasterizzatore software di Mesa) |
| 8,4% | `mesa/main/format_utils.c:convert_ub` | conversione di formato nel percorso GL (upload RGBA) |
| 2,7% + 2,0% + 0,7% | `gallium/drivers/llvmpipe/lp_rast*.c` | rasterizzazione software |
| **5,3%** | `storm_sdraw.c:storm_313_arm_body` | **blitter del gioco (strato A)** |
| **4,2%** | `host_elf.c:host_present_frame` | **presentazione (strato C)** |
| 5,0% | `storm_scomp.c` (`storm_scomp_decompress` 3,4 + `huff_decode` 1,0 + `adpcm_feed` 0,6) | decompressione asset durante il caricamento |
| 0,5% + 0,2% | `memcmp`, `memcpy` | confronti/copie |
| 0,35% + 0,31% + 0,27% + 0,21% | `arm_va_is_mapped`, `proc_range`, `arm_shadow_check_range`, `reasm_indirect_resolve` | validazione memoria e dispatch |

**Avvertenza onesta**: quella esecuzione ha usato il **percorso GL su llvmpipe**, quindi ~70% delle
istruzioni è rasterizzazione *software* di Mesa — un artefatto della sandbox Xvfb, non del gioco. Su
GPU reale (radeonsi) quella quota sparisce e il profilo si sposta su: SCode ~5%, presentazione ~4%,
decompressione ~5%, validazione/dispatch ~1%.

### 2.2 Quanti present per frame (baseline menu, 6.784 commit)

- frame logico (cluster lento) = **16,11 ms** → 62 fps;
- commit del canvas per frame logico = **1,6**.

Quindi si presenta **1,6 volte per frame**: c'è margine per scendere a 1,0, ma non è il fattore 3-6
che una prima lettura dell'istogramma suggeriva.

### 2.3 Costo per commit e copie

- percorso CPU (`present_scaled`): conversione 8bpp→32bpp di **307.200 pixel** + copia nella superficie
  finestra + `SDL_UpdateWindowSurface` (SHM X11);
- percorso GPU (`present_gpu`): **1,2 MB di buffer ARGB** riempito in CPU e caricato come texture
  (uno dei due buffer da 1,23 MB visti da massif);
- percorso GL (`present_gl`): upload **8bpp = 300 KB** e palette applicata nello shader → 4× meno banda
  degli altri due;
- **snapshot del canvas** (`present_snapshot_commit`): `memcpy` di **307.200 byte a ogni present**
  (serve solo a `sc_ctl` e alla ri-presentazione) → con 1,6 present/frame sono ~0,5 MB/frame di copia
  inutile, ed è il primo sospetto del 11,9% "libc" visto con `SC_PROF`.

### 2.4 RAM (da baseline §2.5)

heap 9,24 MB (2,5 MB sono due buffer di presentazione, 0,97 MB audio), RSS 98 MiB mediana / 135 picco,
46 allocazioni per frame, 76% ≤ 64 B.

## 3. Risposta alla domanda "SPL può disegnare direttamente in OpenGL?"

**Tecnicamente sì, ma non è la prima ottimizzazione, e non è quella con il rapporto migliore.**

Per far disegnare lo strato A direttamente in GL servirebbe un **canvas residente in GPU**:
una texture `GL_R8` (indici) + shader palette, e ogni primitiva SCode (i kernel 1×/2× con ROP,
color-key, translucenza, clipping) tradotta in una draw call. I vincoli che rendono l'operazione
rischiosa sono nel gioco stesso:

1. **il gioco legge il proprio canvas**: diverse operazioni sono read-modify-write (ombre SRCAND,
   dissolvenze, dithering, maschere) e la logica di gioco legge pixel (hit-test del cursore, minimappa,
   screenshot). Con il canvas in GPU ogni lettura diventa **readback** — l'operazione più costosa in
   GL (flush + stall) e proprio quella che la documentazione sconsiglia nel frame loop;
2. **fedeltà pixel-per-pixel**: oggi abbiamo una parità verificata (`SC_GL_VERIFY`: 307.200/307.200
   pixel identici fra shader palette e canvas CPU). Spostare i ROP in GPU significa riprodurre
   esattamente le stesse semantiche (comprese le tabelle di translucenza 256×256) o accettare
   differenze visibili;
3. **il guadagno massimo è limitato dalla quota di SCode**: 5,3% delle istruzioni su questa scena,
   e solo nelle scene molto dense (molte unità, effetti) sale. Spostarlo in GPU rende al massimo
   ~5% oggi, con un rischio di regressione grafica alto.

**Verdetto**: la strada GPU-residente va tenuta come *fase avanzata*, da riaprire **dopo** aver tolto
le inefficienze del percorso C (che valgono ~4% + 0,5 MB/frame di memcpy + 2,5 MB di heap) e **solo**
dopo aver misurato su GPU reale una scena pesante, dove la quota SCode cresce. Nel frattempo la mossa
"GL" giusta e a basso rischio è un'altra: **usare `present_gl` (upload 8bpp + shader palette) come
percorso predefinito quando la GPU è reale**, con upload **dirty-rect** invece del frame intero.

## 4. Piano incrementale

Ogni fase si misura con lo stesso comando, sulla stessa scena, con la macchina nelle stesse
condizioni (vedi §5).

| Fase | Intervento | Dove | Guadagno atteso | Rischio |
|---|---|---|---|---|
| **M0** | harness A/B dei tre percorsi (`SC_PRESENT=cpu|gp|gl`) + contatore byte/frame + correzione della propagazione di `SC_PRESENT` nel profiler | `tools/prof/` | nessuno (misura) | nullo |
| **P1** | riuso dei due buffer 8bpp/ARGB e **snapshot solo su richiesta** (`sc_ctl`, ri-presentazione) | `host_elf.c` | −0,5 MB/frame di `memcpy`, −2,5 MB heap | basso |
| **P2** | **un present per frame logico** (dedup sul frame budget) + **upload dirty-rect** su `present_gl` | `com_ddraw.c`, `host_elf.c` | −30…−40% del costo present; upload da 300 KB a pochi KB nei frame senza scroll | medio (verificare latenza input, che peggiora se si accumula troppo) |
| **P3** | pool per allocazioni ≤ 64 B nel percorso guest | `game_memory.c` / allocatore | −3…−6% CPU, alloc/frame 46 → <5 | basso |
| **P4** | validazione memoria: tabella shadow a pagina O(1) (già scritta nel fork `starcraft-reasm-smacker`) | `game_memory.c` | −1,5…−2,5% CPU | basso |
| **P5** | `reasm_extern` per il loop di decodifica caldo del gioco | `storm_crt_unlifted.c` / sostituzione | −2…−4% CPU | medio-alto (equivalenza comportamentale) |
| **P6** | **canvas GPU-residente + shader palette** (l'idea "SPL in GL"), solo per le primitive senza readback, con fallback CPU | nuovo `present_gl` esteso | −3…−5% oggi, di più nelle scene dense | **alto** |

Ordine consigliato: **M0 → P1 → P2 → P3 → P4**, poi rivalutare P5/P6 con le misure in mano.

## 5. Criteri di misura a parità di condizioni

1. **Stato della macchina**: prima di ogni serie verificare RAM e swap (`free -m`, `swapon --show`).
   Le misure del 2026-11-26 sono state prese con 22,2/23,8 GiB di RAM occupati e 25,5 GiB di swap in
   uso: in quelle condizioni il p99 sale da 18,4 a ~21 ms e parte del processo finisce in swap. Se le
   condizioni non sono comparabili, la serie va rifatta, non interpretata.
2. **Stessa scena e stesso comando** (per ogni fase, 3 run, si confrontano le mediane):

```sh
python3 tools/prof/prof-run.py --scenario gameplay --seconds 45 --repeat 3 --tag <fase> \
  --present <cpu|gp|gl> --alloc --env SC_PRESENT_MIN_MS 0 --env SC_FPS_CAP 60
python3 tools/prof/latency.py <socket> 25
```

3. **Gate obbligatori** (tutti): suite `tests/*/check.sh` 9/9 · `SC_GL_VERIFY PASS: 307200` ·
   gameplay **p50 entro ±2%** e **p99 ≤ 18,41 ms** · latenza **p99 ≤ 9,85 ms** · **RSS picco ≤ 147 MiB**
   (P1: −2 MiB) · alloc/frame non peggiore · build ARM `rc=0` · hash dei 3 screenshot di riferimento
   invariato (parità visiva).
4. **Contro la deriva**: ogni fase misura anche un **controllo invariato** (scena menu) per distinguere
   l'effetto della modifica dal rumore di sistema.

## 6. Buco da chiudere prima di P6

Serve una misura su **GPU reale**: in sandbox il percorso GL cade su llvmpipe e falsa ogni confronto.
Prima opzione `gamescope --backend headless` (presente sulla macchina), altrimenti una sessione con
display reale e audio dummy. Senza quella misura, P6 non va deciso.
