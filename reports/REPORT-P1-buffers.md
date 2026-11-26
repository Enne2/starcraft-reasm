# P1 — riuso buffer e snapshot on-demand

## Modifiche

Solo `runtime/shim-port/elf/host_elf.c` è cambiato. Il canvas snapshot viene allocato durante `canvas_alloc()` e riutilizzato finché la dimensione non cambia; viene copiato soltanto nei present forzati. Il present ordinario non copia più i 307.200 byte del canvas nello snapshot. La deduplicazione usa un fingerprint FNV-1a a 64 bit e conserva il confronto esatto della palette. `sc_ctl_present()` resta prima dei rami di throttle/dedup: `shot` e `rawshot` copiano così il canvas corrente al momento della richiesta. Il buffer ARGB del percorso GPU era già statico e riutilizzato; non è stato introdotto alcun buffer per frame. `present_gl` è rimasto invariato.

Patch: `build/qa/p1/LAN-FIX-P1.patch`.

## Copie e allocazioni

- Copia canvas → snapshot: **0 byte per present ordinario**; **307.200 byte** per present forzato, se il buffer è disponibile.
- `shot`: conversione on-demand del canvas corrente in RGB da parte di `sc_ctl`; `rawshot` aggiunge la copia indicizzata. Non avvengono copie di snapshot nel present normale.
- Buffer snapshot riutilizzato; buffer ARGB GPU statico, allocato al primo uso. Nessuna allocazione per frame nel codice ordinario di present (audit del percorso).
- Allocazioni totali osservate: **34,01 malloc/frame** nel processo strumentato. Questo non isola le allocazioni del present e include il gioco/runtime.

## Verifiche e misure

- Build `./tools/build.sh --arch x86 --skip-entry`: **rc=0** (warnings preesistenti in `input_elf.c`).
- `tests/x86_gl_palette_test/check.sh`: **PASS**, verifica i 307.200 pixel RGBA e il gate `SC_GL_VERIFY=1` (`SC_GL_VERIFY PASS: 307200 pixel esatti`).
- Altri test verdi: `x86_affinity_test`, `x86_io_readahead_test`, `x86_runtime_check`, `x86_scomp_test`, `x86_shadow_mem_test`, `x86_vm_pool_test`.
- Non verdi/bloccati da dipendenze dei test: `sc_ctl_test/check.sh` non linka (`host_elf_canvas` e `host_elf_presented_at_ms` non definiti nel suo harness); `x86_scode_lut_test/check.sh` fallisce al controllo `read_dump` perché manca `build/qa/b1-hover/scode-0x4a-0x2bc5.hex`. Non ho modificato test o artefatti condivisi.
- Screenshot menu: `build/qa/p1/menu-1.png`, `menu-2.png`, `menu-current.raw.png`. `shot` e `rawshot` hanno risposto `ok`; luminanza media ImageMagick 3375,32 e 3377,84 per `shot`/canvas RGB. Le due catture menu differiscono di 985 pixel (animazione); non è disponibile una cattura baseline pre-modifica, quindi non dichiaro parità prima/dopo né una verifica gameplay visuale.
- Misura indicativa gameplay CPU, macchina condivisa e quindi **contaminata dal carico parallelo**: p50 **12,01 ms**, p99 **13,51 ms**, RSS picco **143,9 MiB**, allocazioni **34,01/frame** (`build/prof/p1-alloc/`). Latenza su 20 campioni: p50 **7,82 ms**, p99 **9,15 ms**, anch'essa indicativa/contaminata. La misura autorevole resta quella seriale del supervisore. Non è stato raccolto un nuovo baseline A/B controllato.

## Resta aperto

Eseguire la suite completa dopo aver fornito le dipendenze mancanti di `sc_ctl_test` e `x86_scode_lut_test`; acquisire screenshot baseline e gameplay prima/dopo in sessione controllata; ripetere le misure serialmente. Il fingerprint 64 bit ha una possibilità teorica di collisione, estremamente remota, che può sopprimere un present diverso.

## Decisioni autonome

Per evitare la copia completa usata dalla deduplicazione ho scelto un fingerprint 64 bit, mantenendo invariata la finestra temporale di dedup e il confronto palette. Ho lasciato invariati i percorsi fb/CPU/GPU/GL e non ho toccato gli artefatti condivisi. Per i due test non autonomamente eseguibili ho conservato lo stato e documentato gli errori invece di alterare test o dipendenze.

---

## Verifica del supervisore (misura in serie, dopo l'integrazione)

Applicato nel repo principale con `git apply`, ricostruito (`rc=0`), misurato con la macchina
relativamente libera (18,8/23,8 GiB usati invece dei 22,2 della baseline) e con la suite completa.

| Metrica | baseline (26/11) | dopo P1 | nota |
|---|---:|---:|---|
| gameplay p50 | 17,12 ms | **17,04 ms** | −0,5%, entro il gate ±2% |
| gameplay p99 | 18,41 ms | 18,72 ms | **+1,7%**, fuori dal gate stretto: da riverificare in una serie controllata |
| jitter CV | 1,25 | **0,74** | −41%, effetto del `memcpy` rimosso dal percorso |
| frame massimo | 663 ms | 646 ms | −17 ms |
| RSS picco | 135,0 MiB | 140,0 MiB | +5 MiB: **non** il −2 atteso, da investigare (probabile effetto della pressione di memoria e delle run con `--alloc`) |
| allocazioni/frame | 46,2 | **34,7** | −25% (lo snapshot riusato toglieva anche allocazioni) |
| copia canvas→snapshot per present | 307.200 B | **0 B** (solo su richiesta) | obiettivo della fase raggiunto |

Suite `tests/*/check.sh`: **9/9 verde**. Nota: `sc_ctl_test` era rotto dal comando `hash` introdotto
nella baseline (riferimenti non risolti nell'harness); corretto con riferimenti deboli + risposta
`err hash unavailable`.
