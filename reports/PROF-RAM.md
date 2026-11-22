# Profiling RAM — StarCraft x86

## Sintesi

Profiling svolto su `build/starcraft-x86` (ELF32/i386, build con debug info), in Xvfb privato via `./tools/run-isolated.sh`, audio dummy, senza toccare sorgenti/scripts di progetto. L'istanza è stata portata dal menu principale alla schermata di selezione campagna e fino al Boot Camp Terran; lo snapshot Gameplay mostra 8 unità selezionate. Screenshot: `build/qa/prof-mem/captures/{boot,menu-select-type,campaign-select,gameplay-selected}.png`.

Il footprint residente cambia da **130,6 MiB RSS / 129,8 MiB PSS** al menu a **133,7 MiB RSS / 132,9 MiB PSS** nel gameplay selezionato. Il dato virtuale è invece circa **1,7 GiB**, in gran parte prenotato ma non residente: include una riserva anonima `0x21959000–0x60000000` di circa **998 MiB**, oltre a regioni guest/runtime. Non confondere VmSize con RAM consumata.

## Snapshot `/proc` (kB; 1 MiB = 1024 kB)

| Fase | VmSize | RSS | PSS | Private_Dirty | Private_Clean | Shared_Clean | Anonymous | File-backed appross. | Swap |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Boot/menu animato (dopo init/MPQ) | 1,764,552 | 130,640 | 129,809 | 47,604 | 81,584 | 252 | 47,604 | 83,036* | 0 |
| Menu / scelta profilo e campagna | 1,764,552 | 130,052 | 129,221 | 47,764 | 80,836 | 252 | 47,764 | 82,288* | 0 |
| Gameplay Boot Camp | 1,743,980 | 133,556 | 132,725 | 55,400 | 76,704 | 252 | 78,156* | 0 |
| Gameplay, unità selezionate | 1,743,980 | 133,704 | 132,873 | 55,400 | 76,852 | 252 | 78,304* | 0 |

`VmSize` da `ps` del processo; RSS/PSS e categorie da `smaps_rollup`. File-backed approssimato come `RSS − Anonymous` (può includere shmem; non è una somma perfetta delle classi). In gameplay finale `Pss_Anon=55,400`, `Pss_File=76,873`, `Pss_Shmem=600`, confermando la distinzione. File grezzi in `build/qa/prof-mem/captures/` (`*.smaps`, `*.smaps_rollup`, `*.pmap`, `*.ps`).

## ELF statico

`stat`: **17,196,196 byte** (16,4 MiB; presentazione `ls -lh`: 17M). `readelf -SW/-lW`:

| Sezione / blocco | Dimensione | Osservazione |
|---|---:|---|
| `.text` | `0x8aff55` = 8.69 MiB | codice tradotto/runtime e dati read-only in una sola sezione eseguibile |
| `.rodata` | `0x0d6794` = 0.84 MiB | dati read-only |
| `.data` | `0x4f0` = 1.2 KiB | dati inizializzati |
| `.bss` | `0x2ebcd8` = 2.92 MiB | NOBITS: cresce la memoria virtuale RW ma RSS solo per le pagine toccate |
| `.guest_exe_*` | circa 3.0 MiB complessivi | immagine guest PE (codice, dati, rdata, risorse); i segmenti ELF sono separati per permessi |
| `.guest_storm_*` | circa 0.55 MiB | immagine guest Storm, incluse risorse/reloc |
| `.symtab` | `0x19bbc0` = 1.61 MiB | 103,432 simboli |
| `.strtab` | `0x12c117` = 1.17 MiB | stringhe simboli |
| `.debug_*` | 707,790 B = 0.69 MiB | dati DWARF, non mappati nel processo |
| `.comment`/note, dynsym e altre sezioni | resto | metadati e runtime ELF |

Le tabelle `reasm_va_va` e `reasm_va_fn` sono entrambe `0x63fe0` byte (409,984 B ciascuna; **819,968 B complessivi ≈ 0.78 MiB**) in `.text`, simboli a `0x188b3b14` e `0x18917af4` rispettivamente. La sezione `.text` non ha sottosezioni ELF né linker map per attribuire *esattamente* ogni byte a codice EXE tradotto, Storm, runtime/shim o LLASM; sottraendo le tabelle restano circa **7.91 MiB** da attribuire. Le immagini guest raw non vanno sommate al codice host `.text` come se fossero la stessa categoria.

### Simboli `.bss` principali

Da `llvm-nm -S --size-sort`, primi 20 (ordinati per dimensione):

| Simbolo | Size |
|---|---:|
| `g_shadow_pages` | 1,048,576 B (1 MiB) |
| `g_win` | 460,032 B |
| `g_vm_regions` | 458,752 B |
| `g_buf` | 147,456 B |
| `region_info.maps` | 131,072 B |
| `g_map_cache` | 81,920 B |
| `g_sc_prof` | 81,920 B |
| `g_ring` | 65,536 B |
| `g_vm_pool` | 32,768 B |
| `g_input` | 28,672 B |
| `g_q` | 22,528 B |
| `g_cs` | 14,336 B |
| `g_res` | 10,240 B |
| `g_xmap` | 8,192 B |
| `g_ymap` | 8,192 B |
| `g_mods` | 5,504 B |
| `g_crypt` | 5,120 B |
| `dialogs` | 6,720 B |
| `g_com` | 2,048 B |
| `g_mpq` | 2,048 B |

Nota: la lista .bss è ordinata per size (es. `dialogs=0x1a40`, `g_com/g_mpq=0x800`). I top 20 simboli `.data` sono: `arm_com_thunk_table` 1,024 B; `g_caps` 36 B; `g_trace`, `scode_trace_on.enabled`, `ds_log_on.v`, `g_err`, `g_arate`, `g_ach`, `g_aerr`, `g_adrv`, `g_rp_n`, `input_test_tick.ctrl_pk`, due `active.v`, `g_socket_fd`, `snp_trace_on.cached`, `next_search`, `g_timer_next_id`, `g_tls_n`, `g_ev_next` (4 B ciascuno). Nessun singolo grande pool initialized in `.data`.

## Mapping runtime (snapshot gameplay selezionato)

Le dimensioni seguenti sono quelle delle singole VMA superiori a 256 KiB; **virtuale / RSS / dirty**, KiB. Intervalli anonimi contigui con lo stesso ruolo vengono descritti insieme, non enumerati tutti.

- **Immagine StarCraft**: `0x00401000` text guest 1,012/860/0; `0x0050c000` dati guest 1,864/1,824/1,720; `0x15001000` Storm guest text 272/64/0; `0x18000000` `.text` 8,896/8,156/0; `0x188b0000` rodata/unwind 948/896/0; `.bss` anonima `0x189a1000` 2,992/1,152/1,152. La `.bss` virtuale è 2.92 MiB ma residenti ne risultano circa 1.13 MiB.
- **Guest arena**: `0x0e000000–0x0f000000` = 16 MiB virtuali, appena 12 KiB residenti al momento della cattura. Regioni anonime `0x02000000` 2 MiB (48 KiB RSS), `0x02400000` e `0x02800000` da 512 KiB ciascuna (4 KiB RSS ciascuna). Le mappe a `0x02000000`/`0x20000000` non sono equivalenti all'arena 0x0E000000.
- **Guest/runtime eseguibile**: varie VMA `rwx` fra `0x202f0000–0x21cbb000`, ciascuna da 260 KiB a 2.5 MiB; somma delle regioni RWX visibili elencate nel pmap circa 11 MiB virtuali e circa 10 MiB RSS. Probabili blocchi host eseguibili/dati virtual allocator; non sono una singola arena identificabile dal nome in `/proc`.
- **Riserva virtuale non residente**: `0x21959000–0x60000000` circa 1,017,780 KiB `---p`, RSS 0: prenotazione, non RAM. Numerose altre guard/reserve `---p` per circa 1 MiB ciascuna hanno RSS nullo.
- **Heap**: `[heap]` a `0x1a19a000` 42,036 KiB virtuali, 26,640 RSS e private dirty. È una delle maggiori voci RAM residente.
- **Rendering Mesa/LLVM llvmpipe**: `libLLVM.so.22.1` circa 145 MiB virtuali aggregati; mapping RX/R da 74 MiB RSS + 11 MiB RSS + 1.3 MiB RSS, più anonimi associati. `libgallium-26.2.6.so` circa 56 MiB virtuali, circa 10 MiB RSS (codice/rodata file-backed). L'allocazione è software GL su Xvfb; non attribuire a GPU dedicata.
- **SDL/X11/libc**: `libSDL3` circa 3.4 MiB virtuali, ~1.7 MiB RSS; `libX11` 1.4 MiB virtuali, ~0.9 MiB RSS; libc ~2 MiB virtuali, ~1.6 MiB RSS; altre librerie (libxml2, libstdc++, SPIRV Tools, zstd, locale) minori.
- **Shared memory**: SysV `/SYSV...` 1,280 KiB, RSS 1,200 KiB, PSS 600 KiB; Mesa shader cache `index` 2,052 KiB virtuali ma non residente. Non c'è una VMA Wayland (runner usa X11 e Xvfb).
- **Shadow table**: `g_shadow_pages` è 1 MiB statico in `.bss` (BSS RW anonima dentro il mapping `0x189a1000`), non un mapping indipendente. Riduzione della RSS non è automaticamente 1 MiB: al menu la BSS mappata ha già circa 1.1 MiB RSS nel complesso.

## Allocazioni intercettate (profiler LD_PRELOAD)

Il profiler i386 intercetta `malloc/calloc/realloc/free/mmap/mmap64/munmap`, usa un lock statico e tabelle statiche, e salva snapshot binari su `SIGUSR1` in `build/isolated/prof-mem/profiler.bin`. Valori cumulativi nel dump finale: **700,531 allocazioni malloc-family / 635,035 free**, **6,549,021,555 B richiesti cumulativamente / 6,502,014,041 B liberati**; per mmap wrappers: 59 chiamate / 1,109,086,216 B, 19 munmap / 77,824 B. Questi sono flussi cumulativi, non il live heap né RSS; mmap grandi spesso prenotano indirizzi e possono essere `PROT_NONE`.

Principali call-site per byte cumulativi nel dump:

| Call site | allocati | liberati | interpretazione |
|---|---:|---:|---|
| `0x188a0dec` | 1 GiB | 0 | `vm_init_locked` (via `addr2line`), prenotazione virtuale iniziale |
| `0xf345874c` | 3,133,658,640 B | 3,133,597,096 B | caller in Mesa/LLVM, churn elevato; live tracking non affidabile (limite sotto) |
| `0xf34587f2` | 3,134,182,992 B | 3,134,117,448 B | callsite vicino del medesimo percorso allocator |
| `0xf30a41c2` | 55,545,272 B | 25,341,160 B | allocazioni persistenti/temporanee da libreria non risolta simbolicamente |
| `0x18873bb3` | 20,455,424 B | 0 | `arm_map_at`, mappatura/codice guest |
| `0xf33ec1e9` | 10 MiB | 0 | blocco residente attribuito a libreria Mesa |
| `0x1889cc9d` | 63,106,822 B | 63,106,822 B | `receive_packet` (SNP), churn durante gameplay |
| `0x1889afc2` | 7,311,136 B | 4,535,476 B | `read_input`/Smacker, letture e buffering |
| `0x188a5ff1` | 5,767,168 B | 4,653,056 B | `ReadFile__c`, file/asset input |

I call-site interni sono risolti con DWARF; i puntatori `0xf...` sono in librerie native (principalmente Gallium/LLVM), non in `starcraft-x86`, e non sono simbolizzati perché le librerie distro non sono incluse in questo dump. La tabella alloca 4,096 siti e 65,536 record di puntatori; overhead statico circa 1.5 MiB, presente nelle misure.

**Limite importante di accuratezza:** il tracciatore usa open-addressing per i puntatori ma il `free` non conserva tombstone. Dopo rimozioni in bucket con collisioni, alcune entry possono diventare irraggiungibili; quindi contatori globali e byte cumulativi sono utili come stima di churn, ma per-call-site live/peak e conteggi `free` non sono rigorosi. Il dump non espone stack traces 2–3 frame. Non usare questo risultato per dichiarare live bytes esatti o per allocare un budget di memoria. Anche mmap interni a libc che non passano dai simboli interponibili non sono necessariamente visibili. `smaps`/`pmap` sono l'oracolo per footprint residente.

## Opportunità e risparmio quantificabile

- **Virtual reservation lazy**: il mapping di ~998 MiB `---p` è il principale VmSize, ma RSS 0; rimuoverlo ridurrebbe VmSize di ~998 MiB, **non** RSS. Valutare un layout dinamico solo se la prenotazione crea problemi di spazio indirizzi a host 32-bit.
- **Arena guest lazy**: 16 MiB a `0x0e000000` con 12 KiB RSS osservati. Il vantaggio attuale massimo in RSS di una sostituzione sarebbe vicino ai soli 12 KiB nella fase misurata, salvo accessi successivi; riduzione VmSize 16 MiB.
- **BSS**: `g_shadow_pages` 1 MiB è il massimo candidato isolato; `g_win` 0.44 MiB, `g_vm_regions` 0.44 MiB. Risparmio virtuale potenziale pari alle dimensioni dei simboli solo dopo prova che le regioni non servono o possono essere ridotte; RSS massima osservata inferiore e già mescolata con il resto della BSS. Non si può sommare il loro totale come risparmio RAM.
- **Debug/symbols**: strip/split può rimuovere dal file circa 1.61 MiB `.symtab` + 1.17 MiB `.strtab` + 0.69 MiB DWARF (circa **3.47 MiB** lordi, non mappati), preservando debug separato. L'effetto RSS del gioco è sostanzialmente nullo; non è una riduzione runtime.
- **Tabelle dispatch**: `reasm_va_va`+`reasm_va_fn` occupano 0.78 MiB di `.text`; riduzione massima file/virtuale 0.78 MiB, ma non tutte le pagine sono residenti e il costo di traduzione/runtime va verificato.
- **Mesa/llvmpipe**: oltre 40 MiB RSS di codice/rodata LLVM/Gallium nel snapshot; non sono RAM del guest. Cambiare renderer/backend può influenzare questa cifra, ma non è stato misurato A/B.
- Nessuna riduzione del pool `VirtualAlloc` o dei buffer Smacker viene raccomandata come fix: le misure non dimostrano che tali pool siano in eccesso.

## Riproducibilità e artefatti

- ELF: `readelf -SW`, `readelf -lW`, `llvm-nm -S --size-sort` sul binario corrente.
- Profiler: `build/qa/prof-mem/prof_mem.c`, `build/qa/prof-mem/prof_mem.so` (ELF32/i386), wrapper `build/qa/prof-mem/starcraft-prof-mem.sh`. **Mai** esportare LD_PRELOAD nel runner: è solo nel wrapper SC_BIN.
- Snapshot, `pmap -x`, `smaps`, screenshot e source dump: `build/qa/prof-mem/captures/` e `build/isolated/prof-mem/`.
- L'istanza `prof-mem` è stata arrestata con `./tools/run-isolated.sh stop --id prof-mem`; verifica finale `list`: `alive=0`.

*Le celle File-backed approssimative nel riepilogo sono RSS−Anonymous; non sommare PSS come fosse RSS.*

## Decisioni autonome

Ho mantenuto l'attività strettamente in sola analisi: profiler, wrapper e acquisizioni sono confinati in `build/qa/prof-mem/` (con il log di runtime in `build/isolated/prof-mem/`); non ho modificato sorgenti, script o binario di progetto. Per i limiti del profiler ho scelto l'interpretazione conservativa: i contatori di churn sono indicativi, mentre `smaps`/`pmap` sono la fonte per la RAM residente; live bytes per call-site non sono presentati come esatti.
