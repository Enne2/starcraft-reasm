# Piano di massima — Pi Zero con Circle (bare metal)

Stato: **analisi/piano; nessun codice modificato, nessun build o avvio eseguito**. Target: Raspberry Pi Zero/Zero W originale (BCM2835, ARM1176JZF-S ARMv6, VFPv2, 512 MiB, VideoCore IV; non Zero 2 W). Circle dichiara il Pi Zero testato. Dati e prestazioni QEMU riportati qui sono quelli già misurati nei report interni, non nuove misure.

## Raccomandazione

Tenere **Linux+framebuffer come percorso primario e verificare prima su hardware**. Circle è una possibile seconda piattaforma solo se si dimostra che Linux non raggiunge il gate prestazionale o se il requisito diventa eliminare OS/SDL. Il QEMU raspi0 non è un benchmark: presenta 43,4 present/s nell’animazione, 13,9 nel menu; il present fb v2 costa circa 1 ms dei 23 ms/frame nell’animazione. Il residuo è CPU gioco (blit SCode/dispatch/clear), non trasferimento video. Il guest QEMU è inoltre privo di VideoCore e QEMU documenta PWM come device non implementato. Dunque Circle non elimina il principale collo di bottiglia già noto.

## 1. Inventario delle dipendenze OS dal codice

Riferimenti relativi alla root del repo; numeri di riga verificati sul tree letto per questo piano.

| Dipendenza/call-site | Uso attuale | Sostituzione/porting Circle |
|---|---|---|
| `runtime/common/game_memory.c:21,90-93`; `runtime/shim-port/elf/host_elf.c:44-46,491,526,547,541-542`; `runtime/shim-port/elf/main_elf.c:45,142,190`; `runtime/shim-port/elf/win32_shim.c:65-89,662-668,767-797,906-907,944-977,1641-1651,2258-2266` | mmap per arena guest a VA originale, canvas e stack; mprotect per protezioni/guard/watch; mmap file view e stack pthread | Circle non ha POSIX `mmap/mprotect`. Usare allocazione statica/allocator Circle e arena mappata 1:1; implementare commit/protezione software e, se serve realmente, page tables/MMU. L’identità VA guest↔host e i range di protezione devono essere provati prima: **MMU identity 1:1 è una scelta d’architettura/ipotesi, non una capacità qui verificata**. `CMMU`/translation mapping da verificare nella versione Circle selezionata; non simulare `mprotect` ignorandolo. |
| `runtime/shim-port/elf/win32_shim.c:87-89,882-918,925-1002,1257-1262,1355-1496,1609-1651,1749-1776`; `runtime/shim-port/elf/smack_elf.c:231-233,334-430,437-445,496-510`; `runtime/shim-port/elf/sc_ctl.c:221-222`; `runtime/shim-port/elf/snp_elf.c:36-37,817-822`; `runtime/common/guest_call.c:147-198`; `runtime/common/storm_sdraw.c:13-17,107-162` | Win32 mutex/event/thread emulation; lock del cache file; Smacker async decoder; sc_ctl server; SNP event polling; CreateThread; map SCode; | Cooperative main-loop pump + callback/timer Circle per eventi; lock a no-op solo dopo dimostrare single-thread e non rientranza; altrimenti sezioni critiche/interrupt-safe. Smacker in decoder sincrono inizialmente (misurare intro e latenze), poi callback a blocchi. `CUSBKeyboardDevice` per keyboard. Non promettere equivalenza POSIX pthread. |
| `runtime/shim-port/elf/smack_elf.c:60-63,90-130` | `dlopen`/`dlsym` risolve dinamicamente le 13 API di libsmacker; fallback di ricerca file | Linking statico: compilare e linkare le funzioni Smacker come oggetti statici e sostituire la risoluzione runtime con binding diretto. Rimuovere il percorso `.so`; verificare licenza e symbol set esistente. |
| `runtime/shim-port/elf/host_elf.c:40,1323,1403-1422,1743-1770,1945-1959`; `runtime/shim-port/elf/win32_shim.c:66,159-230,308-321,841-850,1760-1819,2258`; `runtime/shim-port/elf/dlg_elf.c:27,306`; `runtime/shim-port/elf/win32_ui.c:44`; `runtime/common/storm_scomp.c:14,747-748`; `runtime/common/storm_sdraw.c:13-14,107-109` | open di log/framebuffer/asset e `/proc`; `pread64` per file cache; `fstat` per distinguere file regolari; `/proc/self/maps` per query memoria e watchdog; read/write standard | `FatFs` (addon Circle basato su ChaN) per MPQ e file di gioco, accesso sequenziale/random via seek/read. Togliere `/proc`/Linux ABI e generare lo stato memoria da arena/allocator Circle. `pread64` diventa seek + read sotto lock o cache dedicata. CRC/file-size e letture random devono uguagliare le API Win32 attese. Il filesystem FatFs preciso dipende da supporto media/driver della release Circle scelta. |
| `runtime/common/bridge.c:32,212`; `runtime/shim-port/elf/host_elf.c:75,1297,213,1579,1608,1694-1695`; `runtime/shim-port/elf/win32_shim.c:80,435-436,1262,1485,1666`; `runtime/shim-port/elf/snp_elf.c:34,208,898`; `runtime/shim-port/elf/sc_ctl.c:144-145,174-175` | tempo monotono, tick Win32, pacing present, timeout e profiling | timer hardware/kernel Circle (`CTimer`/timer handler) e contatore monotono a risoluzione dichiarata. Non chiamare funzioni non reentrant da ISR: il callback setta flag/accumula tick, il main loop effettua present/mix. |
| `runtime/shim-port/elf/snp_elf.c:20,292-314,599-615`; `runtime/shim-port/elf/sc_ctl.c:196-222` | UDP gioco e discovery; socket di controllo Unix/TCP per test | Socket Circle sono opzionali e dipendono da stack/driver ethernet; il Pi Zero originale non ha Ethernet integrata. Per il gioco singolo nessun socket; eliminare `sc_ctl` server sul bare metal o rimpiazzare con seriale/USB debug. Multiplayer è fuori dal primo port e richiede NIC USB + stack Circle e verifica hardware. |
| `runtime/shim-port/elf/host_elf.c:58-110,469-473,581-612,657-665,1699-1728,2206-2280` | SDL2 video/window, eventi, mouse, tastiera e timer; dummy window ancora usata dal presenter framebuffer | Scartare SDL. `CFrameBuffer` per HDMI/framebuffer e `CUSBKeyboardDevice` per input; mouse USB HID solo se aggiunto/verificato. Implementare finestra/viewport 4:3 e trasformazione coordinate nel presenter Circle; non c’è più window/event queue SDL. |
| `runtime/shim-port/elf/host_elf.c:923-1093,1120-1133` (GL/GLES palette); `1155-1188` (SDL renderer); `1359-1422,1480-1528,1617-1639` (fb) | present CPU/SDL o shader GL: canvas 640×480 8bpp, palette in shader; framebuffer mmap Linux | Fase iniziale: `CFrameBuffer` e conversione 8bpp→formato framebuffer con LUT. Fase opzionale: EGL/GLES2 via `addon/vc4`, sostituendo SDL/EGL window e importando gli entry point Circle/VC4. La pipeline VC4 è sperimentale e va provata solo su Pi Zero reale. |
| `runtime/shim-port/elf/host_elf.c:2300-2323,2349-2437,2439-2485`; `runtime/shim-port/elf/com_dsound.c:275,309,556-581` | SDL audio callback S16LE, mixer software e DSound/Smacker voice | `CSoundBaseDevice` + device Circle PWM/HDMI/I2S. Un producer riempie buffer DMA/queue; callback/device consuma, senza SDL audio thread. Adattare la coda/mixer e il formato/clock. |

L’inventario è rappresentativo dei call-site; le righe dei blocchi indicano tutte le occorrenze rilevanti individuate con ricerca dei simboli, non solo la prima dichiarazione.

## 2. Fasi e criteri di uscita

**F0 — freeze delle dipendenze (1–2 sessioni).** Elencare simboli Linux rimasti con `nm`/link map e riferimenti testabili. Uscita: nessuna dipendenza POSIX non dichiarata; tabella di mapping API/semantica approvata internamente. Non portare tutto il sistema prima di avere un binario minimo.

**F1 — boot, video, storage (2–4 sessioni).** Boot seriale su Pi Zero reale, `CFrameBuffer` con pattern colore/gradienti e FAT da SD; leggere l’MPQ scelto a offset casuali e in sequenza. Uscita misurabile: 100 letture ripetibili, CRC32 dell’intero MPQ identico al riferimento Linux e pattern stabile su HDMI per 10 minuti. QEMU è utile per boot/seriale/FAT solo se il dispositivo è emulato; non valida GPU o prestazioni.

**F2 — memoria guest (3–6 sessioni).** Arena riservata ai VA usati dal gioco, mappe statiche/linker e MMU/aliasing; sostituire `mmap/mprotect` e stack. Selftest di reserve/commit/free, accesso inizio/fine pagina, `VirtualQuery`, check range e mappa del PE. Uscita: test unitari ARMv6 invariati e zero VA/CRC/hash divergenze per letture note; fault deliberato terminato in modo controllato. Se si adotta MMU 1:1, provare identity map e cacheability su hardware; Circle specifica e MMU mapping sono da confermare leggendo API/release, non assunti.

**F3 — runtime fino al menu, grafica CPU (4–8 sessioni).** Static link di runtime C, IR liftato, `storm_sdraw.c`, `storm_scomp.c`, bridge e Smacker sincrono; presenter 8bpp→CFrameBuffer. Uscita: avvio da SD, menu corretto e hash/shot ripetibile; almeno **15 present/s menu** misurati su 60 s, senza reset/crash e con nessuno stallo >500 ms. FPS misurati su hardware; non trasferire valori QEMU.

**F4 — input e audio (3–6 sessioni, hardware obbligatorio per audio).** Keyboard USB HID via `CUSBKeyboardDevice`; test 1000 press/release senza eventi persi; audio minimo PCM tone e Smacker/DSound. Uscita: input→primo frame p95 ≤100 ms, test 10 min senza deadlock/overrun, audio continuo senza underrun udibile per 10 min e PCM checksum del mixer in test host. Il PWM non è validabile su `raspi0` QEMU.

**F5 — EGL/GLES2 VC4 (3–8 sessioni, solo hardware).** Primo demo EGL clear/triangle, quindi palette shader sul canvas 8bpp, fallback CPU obbligatorio. Circle `addon/vc4` dichiara EGL 1.4/GLES 1.1/2.0, solo AARCH=32, supporto sperimentale; il supporto GL non è su Pi 4/5 e va verificato per Zero. Uscita: immagine GLSL pixel-identica al CPU su tutti i pixel del test palette, nessun errore GL, almeno +10% present throughput sul presenter CPU in scena identica (altrimenti non mantenere complessità GLES).

**F6 — partita/gate (3–6 sessioni).** Entrare in Boot Camp, 60 s gameplay, misurare FPS, latenza, stalli, RAM e temperature/throttling. Gate “giocabile”: menu ≥15 present/s, gameplay ≥10 present/s, input p95 ≤100 ms, zero stalli >500 ms in 60 s, zero corruzione visiva/audio. Sono soglie prodotto proposte, non risultati acquisiti.

## 3. Riutilizzo / scarto / riscrittura

- **Riutilizzare in C**: codice runtime indipendente da POSIX (`runtime/common/`), `storm_sdraw.c`, `storm_scomp.c`, logica `com_*`, `gdi_elf`, bridge e IR liftato. Conservare test CPU/guest e confronti hash/CRC. Le stesse routine non sono automaticamente portabili: rimuovere `pthread`, `/proc`, file descriptor e assunzioni Linux a ogni confine.
- **Scartare**: SDL2 intero (window, input, renderer, GL context, timer, audio); `host_elf.c` nella parte windowing/SDL e framebuffer Linux; X11/Wayland; `dlopen` libsmacker; poll thread preemptivo SNP e server thread di `sc_ctl`; thread Smacker asincrono al primo rilascio.
- **Riscrivere/astrarre**: platform layer `host_circle` per framebuffer/input/timer/audio/asset; allocazione e memoria virtuale guest; API file Win32 su FatFs; time/tick; presenter palette; lifecycle e init; build/link statico ARMv6. Eventuali primitive concorrenti con callback/timer devono essere non bloccanti.

## 4. Rischi e esperimenti che li chiariscono

1. **Prestazioni CPU** — l’output QEMU mostra che il presenter non è il collo di bottiglia; Circle non rende più veloce il codice gioco. Esperimento: F3/F6, misure stesse scene; fermarsi se gameplay resta sotto 10 fps dopo le ottimizzazioni CPU a basso rischio.
2. **Audio** — Circle elenca PWM, I2S e HDMI sound; il Pi Zero ha HDMI e GPIO, ma il PWM QEMU non è implementato. Per PWM servono pin alternate function/PWM clock e `CPWMSoundDevice`/DMA, con uscita elettrica/amplificazione esterna (il Zero non ha jack analogico integrato); HDMI richiede HDMI sink/clock ed è la via da provare prima per uscita senza hardware aggiuntivo; I2S richiede codec/DAC esterno cablato. Adattare PCM a `CSoundBaseDevice`, i rate/canali e la callback di mixing; verificare su Pi con tone, underrun, 10 min audio Smacker e sincronismo video. Quale device offre qualità/latency accettabile è **da misurare su hardware**.
3. **GLES2 / VideoCore IV** — GLES2 core non garantisce `GL_R8` (formato sized GLES3); il presenter attuale usa texture indice 8-bit e palette shader (host_elf.c: shader/present GL); probe `glGetString(GL_EXTENSIONS)` e upload texture in F5. Ipotesi di port: su GLES2 usare `GL_LUMINANCE` per l’indice (sample `.r`) se accettato, palette in texture RGBA, lookup nearest e no mipmap; `glTexSubImage2D` è GLES2, ma `GL_R8` nativo/texture red dipende da versione/extension. Texture 640×480×1 = 307.200 B più palette; framebuffer/GPU memory share non è documentata qui come budget d’app. Testare limiti dimensioni, alignment/pitch, precisione fragment shader (`highp` availability; altrimenti test dei 256 indici), `glReadPixels`/hash vs CPU. Costo upload per frame e memoria GPU: **da misurare su hardware**.
4. **Scheduler cooperativo** — rimuovere thread poll e Smacker, gestire input e decode tra tick/frame. Un loop occupato può ritardare audio/input; callback ISR non deve chiamare decoder o rendering. Esperimento: log timestamp input, callback audio/underrun, p50/p95/p99 latenza e stall; implementare ring buffer breve e budget massimo per lavoro/frame. L’impatto su pacing non noto.
5. **Static linking/immagine** — ora libsmacker è dinamica. Il binario ARMv6 QEMU interno era ~21,96 MB prima di eventuali payload MPQ/firmware; è un ordine di grandezza, non limite Circle. Esperimento: link map + `size`, dimensione kernel/app e boot da SD, misurare RAM statica e regioni di link; assets restano su FAT, non inglobarli nell’immagine.
6. **Cache/MMU/determinismo** — DMA/VC4 e CPU cache coerenti, layout MMU e memoria condivisa possono causare frame/audio corrotti. Esperimento: selftest pattern/cache clean-invalidate, CRC dopo DMA, confronto byte-for-byte e 30 boot cold; niente `mprotect` semanticamente vuoto. Identity map è ipotesi da dimostrare con linker map e page table.
7. **VC4 API Circle** — documentazione addon definisce supporto sperimentale; non c’è evidenza che la versione di addon e la toolchain del repo siano compatibili direttamente. F5 include un demo standalone prima del runtime.

## 5. Gate di misura Pi Zero reale

Non usare QEMU per concludere su fps, audio, cache, GPU o latency reale. Stesse risoluzione e scena, OS/clock/raffreddamento dichiarati; 3 run da 60 s e mediana/p95. Avvio diagnostico (adattare solo percorso dell’eseguibile/asset):

```sh
cd /home/pi/sc
SC_PRESENT=fb SC_FB_DEV=/dev/fb0 SC_FPS_LOG=1 SC_CTL_SOCK=/tmp/sc-ctl.sock \
SC_SMACKER_SO=./libsmacker-armv6.so ./starcraft-armv6 2>&1 | tee /tmp/sc-pizero.log
```

- FPS: usare log `[fps]` (`tools/qemu/qa-perf.sh` parse solo se log conforme) e `sc_ctl state` in due campioni a 60 s; present/s = delta `presented`/60. Non contare il rate del processo o i present SDL.
- Correttezza: `sc_ctl hash` prima/dopo, `sc_ctl shot /tmp/menu.png`, `tools/shot-stat.sh /tmp/menu.png`; luminanza e hash confrontati con immagine attesa solo come controllo di resa, non come oracolo del comportamento.
- Input: `python3 tools/prof/latency.py /tmp/sc-ctl.sock 30`; si basa su socket Unix/sc_ctl, che sul Circle non c’è. Sul Circle occorre portare un transport seriale o strumento equivalente prima di usare lo script; non riportare latenza Linux come latenza Circle. In Linux, lo script è applicabile.
- RAM Linux: `ps -o pid,rss,vsz,stat,comm -p $(pgrep -n starcraft-armv6)` e `/usr/bin/time -v`; Circle: high-water allocator, stack e regioni linker (strumentazione da implementare).
- Stalli: timestamp del contatore ogni 100 ms per 60 s; accettare nessun intervallo >500 ms. Audio: misurare underrun/callback e ascolto reale.

## 6. Stima e stop rule

Stima grossolana di pianificazione (sessione = blocco tecnico; hardware/periferiche e debug possono allungare): F0 1–2, F1 2–4, F2 3–6, F3 4–8, F4 3–6, F5 3–8, F6 3–6 sessioni; **19–40 sessioni**, circa **10–25 giornate-persona**. Incertezza alta soprattutto su memoria guest, FatFs ABI/port, audio, VC4 e debug di runtime. Non è una previsione di calendario.

**Fermarsi e restare su Linux** se (a) F2 richiede cambiare layout/semantica guest non isolabile; (b) F3 non arriva al menu stabile in 8 sessioni; (c) F6 <10 gameplay fps o input p95 >100 ms dopo due iterazioni misurate; (d) audio non affidabile dopo una prova PWM + una HDMI su hardware; oppure (e) GLES non guadagna ≥10% e il presenter CPU già soddisfa i gate. Linux+fb riusa il binario ARMv6 e il presenter esistente, quindi è baseline meno rischiosa; il runtime Linux ha già un report QEMU di avvio/menu senza X, ma il Pi reale resta da misurare.

## Fonti

- Circle (repo/feature list, support status, audio, timer, framebuffer): https://github.com/rsta2/circle/blob/master/README.md
- Circle class reference: https://github.com/rsta2/circle/blob/master/doc/classes.txt
- Circle addon index (FatFs, VC4, Linux emulation): https://github.com/rsta2/circle/blob/master/addon/README
- Circle VC4 API/build note (EGL 1.4, GLES 1.1/2.0, sperimentale, solo AARCH=32): https://github.com/rsta2/circle/tree/master/addon/vc4/interface
- Circle PWM API: https://circle-rpi.readthedocs.io/en/latest/basic-system-services/gpio-access.html
- QEMU raspi0 devices/mancanti (PWM): https://qemu.readthedocs.io/en/master/system/arm/raspi.html
- Raspberry Pi Zero specifiche ufficiali: https://www.raspberrypi.com/documentation/computers/zero.html
- Khronos OpenGL ES specifications: https://registry.khronos.org/OpenGL/index_es.php
- Dati performance interni: `reports/REPORT-armv6-native-qemu.md` §§2, 10–12; `reports/PROF-BASELINE-2026-11-26.md`; `reports/PLAN-CPU-RENDER-2026-11-26.md`; `reports/REPORT-x86-vs-arm.md`; `PLAN-MULTIARCH.md` §7.2.

## Decisioni autonome

Scelta conservativa: prima validare Linux+fb sul Pi Zero reale e trattare Circle come esperimento a gate; non assumere né prestazioni GPU né audio funzionante sulla base di QEMU o delle sole API documentate.
