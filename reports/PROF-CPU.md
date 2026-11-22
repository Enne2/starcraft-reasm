# Profiling CPU StarCraft x86 — Boot, Menu, Gameplay

## Sintesi

Profiling in ambiente isolato Xvfb/audio dummy, profiler ELF32 con timer per-thread `CLOCK_THREAD_CPUTIME_ID` + `SIGEV_THREAD_ID`, periodo 1 ms. I campioni contengono EIP, ESP, TID, fase e monotonic timestamp; scrittura syscall diretta su file per non perdere i dati quando il runner termina con SIGKILL. Per la fase Gameplay il segnale `SIGUSR1` commuta la marcatura da fase 2 a 3. Nessuna sorgente, script o binario del progetto è stato modificato.

| Fase | Campioni validi | CPU processo, % di un core | CPU principale | Thread renderer llvmpipe | FPS osservati |
|---|---:|---:|---:|---:|---|
| Boot | 1.223 campioni (stat CPU su 2,5 s) | 45,2% | 17,7% + worker StarCraft 23,9% | ~3,6% nel breve campione | 6–10 durante startup; poi 282–320 |
| Menu animato | 3.634 campioni (stat CPU su ~5,5 s) | 108,7% | 36,8% + worker StarCraft 9,0% | ~62% sommati sui worker | 252–300 |
| Gameplay Boot Camp | 2.724 campioni fase 3 (10 s marcati) | 220,0% | 30,5% + worker 0,6% | ~188% sommati sui worker | 380–610 |

CPU% per thread è ricavata da delta `utime+stime` in `/proc/<pid>/task/*/stat` (jiffy a 100 Hz); ogni riga è % di un core, quindi il totale può superare 100%. Include i thread llvmpipe appartenenti al processo, non il processo Xvfb esterno. Il renderer ha 16 worker `llvmpipe-*`; la loro somma cresce nettamente nella scena gameplay. FPS sono il log `SC_FPS_LOG=1`, non un benchmark sincronizzato e non un limite di refresh reale in Xvfb. Le percentuali CPU Boot/Menu sono finestre di osservazione iniziate dopo che `run-isolated.sh start` ha atteso la creazione della finestra; i campioni del profiler, invece, partono dal costruttore del processo. Menu include anche l'interazione breve conclusiva. Le finestre non sono quindi intervalli wall-clock identici al millisecondo alle etichette di fase.

## Metodo e validità

- `build/qa/prof-cpu/prof_cpu.c` compilato con `clang --target=i686-unknown-linux-gnu -m32 -shared -fPIC -O2 -pthread ... -ldl`; `file` conferma `ELF 32-bit Intel 80386`.
- Il wrapper `build/qa/prof-cpu/starcraft-prof-cpu.sh` imposta `LD_PRELOAD` esclusivamente sul gioco 32-bit; non è stato esportato nell'ambiente del runner/Xvfb 64-bit.
- Il costruttore attiva il timer sul thread principale; `pthread_create` intercettata attiva il timer sui thread creati successivamente. Handler `SA_SIGINFO|SA_RESTART`. Il profiler è stato caricato senza crash; ogni file campioni ha dimensione multipla di 24 byte e migliaia di record validi (per il campione Boot iniziale aggiuntivo: 5.575 record).
- `llvm-nm -nS build/starcraft-x86` ha simbolizzato i simboli nativi e `loc_*`. Le mappe `proc/<pid>/maps` sono salvate in `build/qa/prof-cpu/*maps`; offset di librerie attribuiti per modulo. I frame sono campioni PC senza call stack, quindi la tabella mostra il punto di esecuzione, non l'albero dei chiamanti.
- Le verifiche sui guest caldi sono state svolte in IDA sui byte-identical originali copiati in `build/qa/prof-cpu/` (SHA-256 Storm `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`, StarCraft `ad6b58b27b8948845ccfa69bcfcc1b10d6aa7a27a371ee3e61453925288c6a46`), evitando di toccare sessioni IDA preesistenti.

## Breakdown per thread

| Fase | TID principale StarCraft | Worker StarCraft | Renderer llvmpipe | Altri |
|---|---:|---:|---:|---:|
| Boot, 2,5 s | 17,7% | 23,9% | circa 3,6% | residuo trascurabile |
| Menu, ~5,5 s | 36,8% | 9,0% | circa 62% totali (16 thread, dal 7,5% al 2,0% fra i più attivi) | SDLAudioP15 0,4% |
| Gameplay, 10 s | 30,5% | 0,6% | circa 188% totali (16 thread; i primi sei 21,6%, 20,9%, 19,0%, 17,0%, 15,4%, 13,7%) | SDLAudioP15 0,4% |

Il totale di ciascuna riga è una somma arrotondata. Il processo può usare simultaneamente più core. In Gameplay lo screenshot `build/qa/prof-cpu/gameplay-after.png` conferma una partita Boot Camp attiva con unità e base; la selezione ha evidenziato un Marine. Non ho verificato visivamente che l'SCV abbia effettivamente raccolto risorse: il task input di movimento/raccolta è stato tentato, ma il profilo non può essere attribuito a una raccolta riuscita.

## Top 25 campioni per fase

Percentuale sul totale dei campioni della rispettiva fase. `loc_*` indica codice guest tradotto, mentre `storm_*_arm_body`, `proc_range` e simili sono funzioni host/runtime. Per simboli esterni senza debug sono mostrati modulo e offset ELF.

### Boot

| % | Funzione / modulo |
|---:|---|
| 10,96 | libc `__syscall_cancel_arch_end` (+0x7f2ab) |
| 7,69 | runtime `reasm_indirect` |
| 5,89 | Storm guest `loc_15004CC0` |
| 5,48 | Storm guest `loc_15004CE2` |
| 5,07 | runtime `storm_334_arm_body` |
| 4,74 | `[vdso]` +0x240c |
| 3,52 | Storm guest `loc_1501FDDF` |
| 3,52 | runtime `x86_rep_movsd` |
| 2,86 | Storm guest `loc_150049AC` |
| 1,88 | libc `_int_malloc` (+0x86ce6) |
| 1,88 | Storm guest `loc_150049FF` |
| 1,55 | libc `memset` (+0x8ccb7) |
| 1,55 | libc `_int_malloc` (+0x86e0f) |
| 1,23 | Storm guest `loc_150049C5` |
| 1,14 | Storm guest `loc_150049C1` |
| 0,98 | Storm guest `loc_15004C72` |
| 0,90 | libc `memcpy` (+0x8bfa6) |
| 0,90 | runtime `EnterCriticalSection__c` |
| 0,82 | Storm guest `loc_1501C290` |
| 0,74 | Storm guest `loc_15004B54` |
| 0,65 | Storm guest `loc_15004AA9` |
| 0,65 | runtime `proc_range` |
| 0,57 | `ld-linux.so.2` +0x23dd3 |
| 0,57 | `ld-linux.so.2` +0x24ff7 |
| 0,57 | `ld-linux.so.2` +0x23f19 |

### Menu animato

| % | Funzione / modulo |
|---:|---|
| 34,89 | runtime `storm_334_arm_body` |
| 6,82 | libc `__syscall_cancel_arch_end` (+0x7f2ab) |
| 6,66 | runtime `proc_range` |
| 4,16 | `[vdso]` +0x240c |
| 3,80 | runtime `arm_shadow_check_range` |
| 3,11 | libc `memcpy` (+0x8bfa6) |
| 2,86 | runtime `storm_313_arm_body` |
| 2,56 | runtime `reasm_indirect` |
| 2,39 | Storm guest `loc_15004CC0` |
| 2,34 | runtime `scode_rd32` |
| 2,17 | Storm guest `loc_15004CE2` |
| 1,98 | runtime `scode_rd8` |
| 0,88 | Storm guest `loc_150049AC` |
| 0,77 | runtime `x86_rep_movsd` |
| 0,52 | Storm guest `loc_150049FF` |
| 0,52 | runtime `scode_wr32` |
| 0,44 | Storm guest `loc_150049C5` |
| 0,41 | Storm guest `loc_150049C1` |
| 0,41 | runtime `reasm_indirect_resolve` |
| 0,41 | StarCraft guest `loc_41035F` |
| 0,39 | runtime `EnterCriticalSection__c` |
| 0,36 | `[vdso]` +0xc64 |
| 0,30 | libc `memset` (+0x8ccb7) |
| 0,30 | libc `getenv` (+0x1ddfc) |
| 0,25 | `ld-linux.so.2` +0x24ff7 |

### Gameplay Boot Camp

| % | Funzione / modulo |
|---:|---|
| 18,65 | runtime `storm_334_arm_body` |
| 13,51 | libc `__syscall_cancel_arch_end` (+0x7f2ab) |
| 11,27 | `[vdso]` +0x240c |
| 7,64 | libc `memcpy` (+0x8bfa6) |
| 3,19 | runtime `proc_range` |
| 1,76 | runtime `arm_shadow_check_range` |
| 1,32 | runtime `scode_rd32` |
| 1,06 | runtime `storm_313_arm_body` |
| 0,99 | runtime `scode_rd8` |
| 0,95 | runtime `x86_rep_movsd` |
| 0,81 | StarCraft guest `loc_40AC9A` |
| 0,73 | `libgallium-26.2.6.so` +0xaeb17 |
| 0,70 | runtime `reasm_indirect` |
| 0,37 | `libgallium-26.2.6.so` +0x97a2fc |
| 0,33 | libc `__pthread_mutex_lock` (+0x7979d) |
| 0,29 | libc `malloc` (+0x88020) |
| 0,29 | runtime `host_elf_pump` |
| 0,29 | libc `getenv` (+0x1ddde) |
| 0,29 | libc `getenv` (+0x1ddfc) |
| 0,26 | `libgallium-26.2.6.so` +0xb6321 |
| 0,26 | Storm guest `loc_1501FDDF` |
| 0,26 | runtime `win32_shim_timers_pump` |
| 0,22 | Storm guest `loc_1500404F` |
| 0,22 | StarCraft guest `loc_40AC82` |
| 0,22 | StarCraft guest `loc_41C810` |

L'offset libc `0x7f2ab` risolve a `__syscall_cancel_arch_end` ma può rappresentare un campione dentro il percorso syscall/cancellazione, non identifica da solo la chiamata sorgente. `[vdso]` resta attribuito a modulo, senza simbolo verificato.

## Ispezione dei guest più caldi (IDA, binari originali)

- **Storm `0x15004CC0`–`0x15004D48`** (campioni `loc_15004CC0`, `loc_15004CE2`): routine di lettura da bitstream con contatore bit; quando il buffer è esaurito richiama un callback di refill, sposta i bit residui e aggiunge il byte successivo. È una funzione helper di decoding, non uno spin-loop.
- **Storm `0x15004990`–`0x15004CFD`** (campioni `loc_150049AC`, `loc_150049FF`, `loc_150049C1/C5`): loop di decodifica token. I token letterali scrivono un byte; gli altri chiamano un helper per ricavare lunghezza/distanza e copiano dalla finestra di output. Quando il buffer raggiunge 0x2000, invia 0x1000 byte al callback e compatta il residuo. La struttura è compatibile con una decodifica a dizionario, ma qui non è stato determinato il formato/caller specifico.
- **StarCraft `0x00410330`** (campione `loc_41035F`): legge byte-token, deriva campi low-bit/shift e avanza una destinazione usando contatori e stride globali; la funzione termina al contatore esaurito. Semantica dettagliata dei dati non risolta, quota del menu 0,41%.
- **StarCraft `0x0040ABBE`** (campione interno `loc_40AC9A`): decoder byte per righe con tabella di trasformazione, run di pixel e salti/spazi; campione 0,81% in Gameplay. Non è un hot spot dominante.

I nomi nel database IDA QA sono stati annotati e salvati come `storm_bitstream_read_bits` (Storm 0x15004CC0), `storm_decode_token_block` (Storm 0x15004990), `sc_decode_byte_tokens` (StarCraft 0x00410330) e `sc_decode_palette_runs` (StarCraft 0x0040ABBE), con commenti sulle inferenze/limiti. Il principale blocco guest hot sopra l'helper di decoding è `storm_334_arm_body`: in `runtime/common/storm_sdraw_arm.c` implementa la validazione dei descriptor/surface e il patch/dispatch di un kernel SCode per blit. I campioni runtime `proc_range`, `arm_shadow_check_range` e `scode_rd*` sono coerenti con costi di validazione/accesso alla memoria e dispatch, ma il campionamento PC non separa il costo dei controlli dal kernel chiamato. `storm_334_arm_body` è 34,89% nel Menu e 18,65% in Gameplay: evidenza di hotspot ripetibile, non prova che tutti i cicli siano eliminabili senza indebolire i controlli di sicurezza.

## Opportunità d'ottimizzazione, ordinate per impatto atteso

1. **Ridurre il costo nel percorso di SCode/blit senza rimuovere le guardie** — potenziale maggiore: `storm_334_arm_body` 34,9% Menu / 18,7% Gameplay; `proc_range` + `arm_shadow_check_range` ulteriori 10,5% Menu / 5,0% Gameplay. Misurare separatamente la validazione e l'esecuzione, quindi valutare cache di intervalli già validati o fast path solo per pattern/fingerprint esatti e superfici stabili. Una riduzione relativa del 20% del blocco `storm_334` implicherebbe al massimo ~7,0 punti campione nel Menu e ~3,7 nel Gameplay; stima aritmetica, da verificare con A/B. Non bypassare verifiche di range.
2. **Ottimizzare il percorso memoria `memcpy`/`x86_rep_movsd`** — in Gameplay memcpy 7,64%, `x86_rep_movsd` 0,95%, oltre alla parte di copia del decoder Storm; profilare dimensioni/allineamenti e confrontare un fast path equivalente. Limite teorico immediato delle sole voci nominate: circa 8,6 punti in Gameplay, senza assumere che siano tutte copie evitabili.
3. **Ridurre chiamate indirette nel dispatcher** — `reasm_indirect` 7,69% nel Boot breve, 2,56% Menu, 0,70% Gameplay. Il calo con scena stabilizzata suggerisce costo soprattutto durante startup; ottimizzare solo dopo avere attribuito call site e verificato la correttezza del cache hit.
4. **Renderer software llvmpipe** — circa 62% CPU in Menu e 188% in Gameplay su tutti i suoi worker, ma senza una singola funzione Gallium dominante (>0,73% nel campione Gameplay). In Xvfb è il renderer software: utile confronto successivo fra presenter/driver, non evidenza per modificare codice guest. Ridurre lavoro di presentazione/refresh solo se A/B conferma invariato il comportamento.

## Decisioni autonome

- Ho scelto timer per-thread `CLOCK_THREAD_CPUTIME_ID` + `SIGEV_THREAD_ID` invece di `ITIMER_PROF`, per distinguere i TID e il consumo CPU effettivo di worker gioco/renderer; il campionamento resta a 1 kHz e l'overhead non nullo è dichiarato.
- Ho omesso gli A/B opzionali `SC_PRESENT` e `SC_SMACK_ASYNC`: la priorità era ottenere un profilo ripetibile senza alterare il percorso di resa e non c'erano misure iniziali che giustificassero varianti.
- La schermata gameplay prova una partita Boot Camp attiva ma non prova la raccolta dell'SCV; non attribuisco la misura a quell'azione specifica.

## Artefatti, log e limiti

- Profiler/wrapper/simboli/maps/campioni/statistiche thread/screenshot/originali copiati: `build/qa/prof-cpu/`.
- Log runner: `build/isolated/profcpu-boot2/game.log`, `profcpu-menu/game.log`, `profcpu-gameplay/game.log`.
- Tutte le istanze `profcpu-*` avviate per questa prova sono state fermate con `./tools/run-isolated.sh stop`. Nessun processo di questa sessione resta attivo.
- Il runner `list` mostrava un'istanza indipendente `prof-mem` attiva durante le prove; non è stata terminata né modificata in questa sessione.
- Il profiler scrive un record per campione e non mantiene stack/call chain; timer e syscall di campionamento introducono overhead. I valori sono stime orientative, le percentuali di funzioni derivano dai campioni, non da hardware performance counters. Non è stato eseguito A/B `SC_PRESENT` o `SC_SMACK_ASYNC`.
- Non è stata determinata la funzione/call-site responsabile dei campioni libc/vDSO, né è stata provata la causa dei cicli dentro il kernel SCode. Le raccomandazioni sono ipotesi misurabili, non modifiche applicate.
