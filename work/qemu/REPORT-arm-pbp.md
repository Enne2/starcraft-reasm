# Indagine tecnica: StarCraft ARMv6 sul Pinebook Pro

**Esito aggiornato al candidato menu3:** ARM32 nativo e loader armhf funzionanti. Il precedente `starcraft-armv6-menu` si è fermato a `0x2017008C`; `starcraft-armv6-menu3` con `SC_SMACK=0` è rimasto vivo per 45 s senza STOP/SIGSEGV, ma è arrivato solo al logo/loading StarCraft, non al menu. Prova pulita senza recorder: FPS medio 3,66 (41 campioni), CPU media 104,68%, RSS medio 71.500 KiB. Screenshot X11 acquisiti; video non avviato perché il menu non è apparso. Nessun pacchetto installato e nessun input simulato.

## Perimetro e metodo

- Indagine iniziale 20 novembre 2026, 21:52; deploy e prova runtime successivi 22:51–23:00 locale.
- SSH a `enne2@10.0.1.126` (host `pinebook-pro`). Nessun `apt`, nessun input simulato, nessuna modifica fuori da `~/starcraft-armv6-test/` remoto. Sul Pinebook sono stati creati directory/file del test e symlink richiesti; tutti sotto tale directory.
- File locali scritti: `work/qemu/REPORT-arm-pbp.md` e artefatti acquisiti sotto `work/qemu/pbp/`.
- La sessione grafica è stata usata con `DISPLAY=:0`, `XAUTHORITY=/run/user/1001/.mutter-Xwaylandauth.L687Y3`, `XDG_RUNTIME_DIR=/run/user/1001`, `PULSE_SERVER=unix:/run/user/1001/pulse/native`; nessun click/tasto è stato inviato.

## 1. Sistema, CPU e compatibilità binaria

| Voce | Output misurato |
|---|---|
| Host | `pinebook-pro`; indirizzo usato `10.0.1.126` |
| OS | Ubuntu 22.04.2 LTS (Jammy) |
| Kernel | `Linux 6.1.11-rockchip64 #23.02.2 SMP PREEMPT ... aarch64` |
| Architettura | kernel/userland aarch64; `lscpu` dice `CPU op-mode(s): 32-bit, 64-bit` |
| SoC/CPU | RK3399 big.LITTLE: 4 Cortex-A53 (part `0xd03`) + 2 Cortex-A72 (part `0xd08`), 6 CPU online |
| Compatibilità 32 bit | `/proc/config.gz`/config kernel riporta `CONFIG_COMPAT=y` e `CONFIG_COMPAT_BINFMT_ELF=y` |
| Hard-float/ARMv6 | CPU aarch64 supporta esecuzione AArch32; il target ARM EABI5 hard-float/ARMv6 è compatibile dal punto di vista CPU/ABI (ARMv6/VFPv2 è un sottoinsieme delle capacità AArch32 necessarie). Nessun test di esecuzione del binario applicativo effettuato. |

### Binario locale esaminato

`file build/starcraft-armv6-blt` lo identifica come ELF 32-bit LSB ARM EABI5, dinamico, interpreter `/lib/ld-linux-armhf.so.3`. `readelf -d` mostra queste dipendenze dirette:

- `libc.so.6`
- `libm.so.6`
- `libgcc_s.so.1`
- `libSDL2-2.0.so.0`

Questa lista non include `libsmacker-1.2.0.so`: il runtime la carica dinamicamente. La libreria locale `build/arm-libs/libsmacker-1.2.0.so` è stata distribuita in `~/starcraft-armv6-test/arm-libs/` e selezionata con `SC_SMACKER_SO` nell’esecuzione valida; il primo tentativo senza override ha provato un path assoluto del workstation non esistente sul Pinebook e ha mostrato dialogo `smk\\blizzard.smk / unknown error 0x00000002`.

## 2. Multiarch armhf e dipendenze

Il Pinebook Pro ha armhf multiarch già presente: `ldconfig -p` riporta SDL2 sia `libc6,hard-float` in `/lib/arm-linux-gnueabihf` sia AArch64 in `/lib/aarch64-linux-gnu`; `dpkg -l` conferma `libsdl2-2.0-0:armhf` versione `2.0.20+dfsg-2ubuntu1.22.04.1`, oltre alla variante arm64.

Percorsi armhf effettivamente trovati:

- `/lib/ld-linux-armhf.so.3` → `/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3` (ELF 32-bit ARM EABI5)
- `/lib/arm-linux-gnueabihf/libc.so.6`, `libm.so.6`, `libgcc_s.so.1`
- `/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0` → SDL2 armhf 2.0.20
- `/usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0`

Sono inoltre installati i pacchetti armhf `libc6`, `libgcc-s1`, `libasound2`, `libpulse0`, SDL2 image/mixer/ttf e Mesa GL. Le librerie armhf di X11, Wayland, PulseAudio, ALSA, DRM e GBM compaiono nell’output di `ldconfig -p`. Dunque le principali dipendenze runtime SDL citate non sono limitate ad AArch64.

**Limite di verifica:** non è stata eseguita un’ispezione completa con un loader armhf del binario sul target. Invocare `ldd` aarch64 su un oggetto armhf non è una verifica valida: il tentativo fatto su `libSDL2` ha restituito `not a dynamic executable`, risultato atteso da tool/ABI non corrispondenti, non prova di una dipendenza mancante.

### Sysroot locale

`build/sysroot` esiste sull’host e occupa circa 1,1 GiB; contiene `ld-linux-armhf.so.3`, `libc.so.6`, `libm.so.6`, `libgcc_s.so.1` e SDL2 2.26.5 armhf. Il suo SDL2 dichiara NEEDED verso ALSA, Pulse, X11, DRM/GBM, Wayland e ulteriori librerie. **Non serve usarlo come prima scelta sul Pinebook**, che ha già loader e SDL2 armhf di sistema. Un sysroot usato tramite `LD_LIBRARY_PATH` da solo non garantisce completezza: andrebbero portate/risolte anche le dipendenze NEEDED della SDL e le eventuali librerie private del programma; non va sovrapposto alla libc del sistema senza una strategia coerente.

## 3. Desktop, grafica, display e audio

### Sessione

- `loginctl`: sessione utente `enne2`, seat0, tty2; `who` riporta `enne2 tty2`.
- Processi rilevati: GNOME/GDM, `gnome-shell`, `Xwayland`, PipeWire, `pipewire-media-session` e PulseAudio.
- La sessione è Wayland (`XDG_SESSION_TYPE=wayland`), display Wayland `wayland-0`, runtime dir `/run/user/1001`; i processi utente hanno `DISPLAY=:0` e `WAYLAND_DISPLAY=wayland-0` (l’ambiente SSH stesso non esporta `DISPLAY`/`WAYLAND_DISPLAY`).
- Socket `wayland-0` e socket X11 `X0` esistono. L’applicazione lanciata via SSH dovrà ricevere le variabili di sessione e poter accedere all’autenticazione/socket della sessione; l’SSH non le eredita automaticamente.

### GPU

- DRM: `/dev/dri/card0`, `card1`, `renderD128`; card0 driver `rockchip-drm`, card1 driver `panfrost`.
- Modulo `panfrost` caricato.
- `DISPLAY=:0 glxinfo -B`: direct rendering Yes; renderer **Mali-T860 (Panfrost)**; Mesa 22.2.5; accelerated Yes; OpenGL 3.1 e GLES 3.1.
- `glxinfo` senza un display impostato fallisce con `unable to open display`, coerente con l’ambiente SSH headless.
- Non è stato trovato/eseguito `vulkaninfo`; nessuna conclusione su Vulkan.

### Audio

- `pactl info`: PulseAudio server 15.99.1 su `/run/user/1001/pulse/native`, default sink `alsa_output.platform-es8316-sound.stereo-fallback`, default source ES8316.
- `aplay -l`: card 0 Rockchip ES8316 HiFi presente.
- Le librerie Pulse/ALSA armhf sono presenti. Per riprodurre audio in una sessione avviata via SSH servirà l’ambiente utente corretto (almeno `XDG_RUNTIME_DIR`, `PULSE_SERVER` o `SDL_AUDIODRIVER` configurato, DBus se necessario); non è stato provato.

## 4. Cattura video e screenshot

`command -v` sui nomi richiesti ha trovato:

- Presenti: `/usr/bin/import` (ImageMagick), `/usr/bin/xwd`.
- Non trovati: `ffmpeg`, `wf-recorder`, `grim`, `slurp`, `spectacle`, `scrot`, `vncserver`, `x11vnc`.
- In controllo successivo è stato trovato `/usr/bin/gnome-screenshot`; pacchetto `gnome-screenshot` 41.0-2 arm64 installato.
- Sono installate librerie FFmpeg 4.4.2 arm64 (`libav*`), ma non l’eseguibile `ffmpeg` nelle ricerche PATH: le librerie da sole non forniscono un recorder CLI.

**Verifica effettiva:** GStreamer ha i plugin `ximagesrc`, `videoconvert`, `vp8enc`, `webmmux`, `jpegenc`, `pngenc` e `filesink`; `pipewiresrc` e `theoraenc` sono presenti; `x264enc` non è presente. La pipeline X11→VP8→WebM è stata preflightata con 30 frame e poi usata durante l’esecuzione. `gnome-screenshot` è stato eseguito senza input simulato. Il video registrato è 1920×1080, VP8, 15 fps, durata 62,33 s, 189.917 byte; GStreamer ha chiuso con EOS (exit 0). Il video/screenshot sono file utili all’ispezione ma il desktop si è oscurato durante il test: allo screenshot dei 60 s appare il lock screen GNOME, e i frame a 10/30 s risultano neri. Non si è tentato di sbloccare la sessione.

## 5. Risorse

- Root `/dev/mmcblk2p1`: 57 GiB totali, 35 GiB usati, **22 GiB liberi (63%)**.
- RAM: 3,8 GiB totali, 687 MiB usati, 1,8 GiB liberi, 2,9 GiB disponibili; swap zram 1,9 GiB, 0 usato.
- `/tmp` è tmpfs da 1,9 GiB, quasi vuoto al momento del rilievo.

## 6. Deploy e verifica hash

Il filesystem remoto ha riportato 22 GiB disponibili su `/`; asset complessivi trasferiti in `~/starcraft-armv6-test/` (directory totale circa 1,3 GiB). Il sorgente degli asset in `/home/enne2/dev/final-starcraft/Starcraft/` è stato solo letto. Il binario è stato reso eseguibile con `chmod +x`; symlink creati nella directory remota: `local.dll -> Local.dll`, `Broodat.mpq -> BrooDat.mpq`, `Stardat.mpq -> StarDat.mpq`.

SHA-256 host e Pinebook coincidono per tutti i file:

| File | SHA-256 |
|---|---|
| `starcraft-armv6-blt` | `32b141c9b94913f942b4f5668615f64a8c67d94701fb4fd0bc5aa9eb817a48c0` |
| `arm-libs/libsmacker-1.2.0.so` | `41523b9f24d395f092cd3b7a13d1cef180647eafaf2fcd18daa0f9d74bde3705` |
| `Starcraft.mpq` | `17fca7b2746af43e161c8acc3970115f655cb0e239bdee5544eed81f3cecb10b` |
| `StarDat.mpq` | `0cf90e52b5bb65a6086d25d4c5c5bbd070adf04d12fb19be035af2db39737cd2` |
| `patch_rt.mpq` | `f692f7e4ddcd10255ac866bffd56be72f7dd717b67eb35af7442deca394501ca` |
| `BrooDat.mpq` | `8c2c8ad5b32d4caf05d9e9d6727f46872ce6fc54f2e69ad6d6364cd1ea58c3c4` |
| `BroodWar.mpq` | `180425f53094c71b208b1244dab3cb2f63492a9120d96e3a9362f4c5ff7404b2` |
| `Local.dll` | `597e78f855a2853602e3dde0876a92849c3a5b6679e4003a46eba4f1d1dada43` |
| `EditLocal.dll` | `e3e91cffef9e181356dedb03a2156f567ad092aa38a0bf64e7fa9f318e7537ef` |

I symlink minuscoli `local.dll`, `Broodat.mpq`, `Stardat.mpq` non esistono nel sorgente originale come file fisici, perciò sono stati creati solo nel target di test, come richiesto.

Il comando richiesto `/lib/ld-linux-armhf.so.3 --list ./starcraft-armv6-blt` ha risolto i NEEDED diretti, tutti da `/lib/arm-linux-gnueabihf/`: libc, libm, libgcc_s, SDL2; le dipendenze SDL mostrate (ALSA, Pulse, X11, Wayland, DRM/GBM ecc.) hanno anch’esse risoluzione armhf nel loader output. Il caricamento Smacker è `dlopen`, fuori dalla lista NEEDED e verificato poi dal log del gioco.

## 7. Esecuzione reale, output e misura

Due tentativi senza input sintetico:

1. **Primo avvio, senza `SC_SMACKER_SO`:** ambiente `DISPLAY=:0`, SDL X11 e PulseAudio; il runtime cercava `/home/enne2/dev/starcraft-armv6/build/arm-libs/libsmacker-1.2.0.so`, path host inesistente sul PBP. Ha aperto finestra e audio, ma è entrato nel modal loop `smk\\blizzard.smk / unknown error 0x00000002`. Timeout 95 s ha chiuso il wrapper (exit 137); non è rimasto processo di gioco.
2. **Secondo avvio, con `SC_SMACKER_SO=$PWD/arm-libs/libsmacker-1.2.0.so`:** loader e libreria Smacker hanno funzionato; log `filmato pronto` per il primo SMK (320×308, 171 frame), completamento del primo filmato e apertura del successivo SMK2 (320×308, 3428 frame). Audio Pulse è stato aperto a 22050 Hz stereo e i chunk sono stati elaborati. Il gioco ha poi terminato autonomamente con exit 71: `[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto`, `target = 0x00418ED9`. Non ha raggiunto il menu. Nessun PID è rimasto attivo.

Con il recorder attivo `ps` sul processo `starcraft-armv6-blt` ha misurato `%CPU 99.7`, RSS `224792` KiB (= circa 219,5 MiB) dopo 72 s: CPU pari a un core. Il recorder VP8 era anch’esso CPU-heavy (circa 94% in un campione dopo 43 s), quindi FPS osservati durante questa acquisizione non sono un benchmark del gioco senza recorder. Log FPS: 82 righe nel secondo tentativo; dopo avvio Smacker, tipicamente **12–13 fps** (con singoli valori 8–13; il log mostra anche un iniziale 2 fps durante startup). Il primo SMK è identificato dal log con 171 frame e il log segnala la fine del relativo stream audio prima dell’apertura del secondo. Il secondo header dichiara 3428 frame; il log mostra la decodifica/DoFrame iniziale (ultimo `cur=0x5A` esplicitamente registrato) ma non dimostra il completamento del filmato. Pertanto non è stata dimostrata la riproduzione completa/fluida dei due filmati: la frequenza è sotto i 24/30 fps e il processo termina prima del menu.

## 8. Artefatti locali

Tutti sotto `work/qemu/pbp/`:

- `attempt2.stdout-stderr.log` — log integrale del run con Smacker caricata, inclusi STOP, registri e trace di chiamata.
- `attempt2-controller.log`, `attempt2-gst.log`, `attempt2-capture.webm` — controller e log GStreamer; cattura VP8/WebM.
- `attempt2-screenshot-10s.png`, `attempt2-screenshot-30s.png`, `attempt2-screenshot-60s.png` — catture del display; 60 s mostra il lock screen GNOME.
- `starcraft-pbp.stdout-stderr.log`, `screenshot-*.png`, `starcraft-pbp-capture.webm` — primo tentativo (errore path Smacker).
- `capture-preflight.png`, `capture-preflight.webm`, `gst-capture.log` — test preventivo della pipeline.

`ffprobe` locale ha misurato per `attempt2-capture.webm`: codec VP8, 1920×1080, 15/1 fps, durata 62,329530 s, 189917 byte. GStreamer ha chiuso il file con EOS. La cattura si è interrotta prima del timeout del gioco per non continuare un recording CPU-costoso.

## Raccomandazione aggiornata

**Il binario ARM32 è eseguibile nativamente e i NEEDED di sistema si risolvono; non serve ricompilare aarch64 per incompatibilità ABI.** Lo STOP a `0x00418ED9` era della build `blt`; il candidato `menu` si è poi fermato al diverso target `0x2017008C`. `menu3` non ha prodotto STOP nei 45 s ma non è arrivato oltre il logo/loading: FPS puliti medi 3,66, CPU ~un core pieno. Quindi il menu non è ancora verificato o utilizzabile. I 12,51 FPS della build `menu` con encoder non sono direttamente comparabili: build e workload sono diversi.

Il primo tentativo evidenzia anche che questo candidato cerca Smacker a un path assoluto del workstation; nei test successivi è necessario `SC_SMACKER_SO` esplicito o aggiornare la ricerca relativa nel runtime. Per ora libreria distribuita in `arm-libs/` e variabile d’ambiente hanno consentito il load. Non sono stati installati pacchetti. Il lock screen GNOME ha coperto lo screenshot al secondo 60: senza autorizzazione a modificare il comportamento idle o a interagire con la sessione, non è stato sbloccato.

## 9. Candidato intermedio `starcraft-armv6-menu`

- SHA-256 verificato sul Pinebook: `29b53a52b78b90e40b288fdfddbfc3aa34867452128d5b29e87fe5bafcefb387`.
- Prova pulita senza encoder, con Smacker e audio Pulse: ha creato la finestra, poi è terminato con exit 71 per `[arm-dispatch] STOP` al target `0x2017008C` dopo circa 30 s; menu non raggiunto.
- XID figlio visibile nell’albero X11: `0x800016`, classe `starcraft-armv6-menu`. Il controller iniziale non aveva matchato la classe minuscola, quindi niente screenshot per quella build. Log, campioni e tree: `menu-clean.stdout-stderr.log`, `menu-clean-perf.csv`, `menu-window-tree.txt` in `work/qemu/pbp/`.

## 10. Candidato `starcraft-armv6-menu3` — prova pulita

- Hash locale/remoto: `bd54eb50ef5bedaa4a6944441a47121ec4e4a43914aa6db4559ea77043860ce9`; remoto `~/starcraft-armv6-test/starcraft-armv6-menu3`, `chmod +x`. Il loader armhf ha risolto libc, libm, libgcc_s, SDL2 e dipendenze.
- Avvio con `DISPLAY=:0`, XAUTHORITY della sessione Xwayland, `XDG_RUNTIME_DIR=/run/user/1001`, PulseAudio, `SC_SMACKER_SO=$PWD/arm-libs/libsmacker-1.2.0.so`, `SDL_VIDEODRIVER=x11`, `SC_SMACK=0`, `SC_FPS_LOG=1`. **Nessun GStreamer/encoder concorrente.**
- Il processo è rimasto vivo per tutti i 45 s, poi il controller ha inviato TERM e, dopo 2 s, KILL; exit 137 per timeout del test, non crash. Log privo di STOP, API non implementate, SIGSEGV o SIGBUS; nessun processo è rimasto.
- `xwininfo` ha trovato finestra X11 XID `0x800016`, classe `starcraft-armv6-menu3`; `import -window <XID>` ha prodotto screenshot della sola finestra (non screensaver). A 5 s: schermata “Loading”; a 15 e 30 s: logo StarCraft/Brood War e copyright. **Nessun menu principale né voci Single Player / Multiplayer / Campaign Editor / Exit.**
- FPS: 41 campioni, min 1, max 5, media **3,66 fps**. CPU: 45 campioni, media **104,68%**, min 69%, max 109% (circa un core). RSS: media **71.500 KiB**, min 68.764, max 72.804 KiB.
- Il video non è stato avviato: era richiesto solo se il menu appariva, ma la build restava al logo/loading.
- Artefatti: `menu3-clean.stdout-stderr.log`, `menu3-clean-controller.log`, `menu3-clean-perf.csv`, `menu3-window-tree.txt`, `menu3-screenshot.log`, `menu3-5.png`, `menu3-15.png`, `menu3-30.png` in `work/qemu/pbp/`.

### Confronto delle misure

| Build e condizione | Risultato | FPS | CPU / RSS | Video |
|---|---|---:|---|---|
| `menu`, Smacker attiva + VP8 recorder concorrente | STOP `0x2017008C`; niente menu | 12,51 media (82 righe log) | 99,7% / 224.792 KiB in un campione | WebM 62,33 s |
| `menu3`, `SC_SMACK=0`, senza recorder | Vivo fino al limite, solo logo/loading | 3,66 media (41 righe log) | 104,68% / RSS medio 71.500 KiB | Non prodotto |

I valori non sono un confronto controllato della stessa scena: candidato/workload differenti; la prima misura include inoltre costo dell’encoder.

## 11. Diagnostica breve del blocco “Loading…” — `SC_PRESENT=cpu` e trace

### Present CPU senza encoder (25 s)

Run di `starcraft-armv6-menu3` con `SC_SMACK=0`, `SC_PRESENT=cpu`, `SC_FPS_LOG=1`, X11 e PulseAudio, senza recorder. **Nessun miglioramento rispetto ai 3–5 FPS precedenti:** 20 campioni `[fps]`, media **3,70 fps**, range 1–5. `present medio` media **0,761 ms**, range 0,40–3,50 ms. A 20 s lo screenshot diretto XID `0x800016` (`menu3-cpu-present-20.png`) mostra ancora la schermata StarCraft/Brood War; nessun menu. Questo esclude il present GPU/X11 come costo dominante: una chiamata di present dura meno di 1 ms, mentre il tempo medio per frame a 3,7 fps è circa 270 ms.

Campionamento `ps` ogni secondo: 25 campioni, CPU media 97,17% includendo i campioni iniziali di avvio; dal secondo 6 al 25, CPU media **102,80%** (98–106%), RSS media **62.417 KiB** (61.740–63.424 KiB). A 12 s `ps -L` mostra il thread principale a 97,9%, un worker a 7,7%, thread audio SDL e altri due thread. Processi `perf` e `gdb` non installati/disponibili. `/proc/<pid>/syscall` risponde `Operation not permitted`, `/proc/<pid>/stack` `Permission denied`, `wchan` restituisce `0`; quindi non è disponibile uno stack campionato. Nessun input è stato inviato.

### `REASM_TRACE=1` (10 s)

Il log completo ha **5.234 righe / 522.111 byte**. Nel trace: **934 `ReadFile`**, **1.642 `VirtualAlloc`**, **606 `VirtualFree`**, **203 `Wait`**; 28 wait riportano timeout `0x102` (250 ms), e gli altri sono perlopiù completamenti/eventi. L’ordine `file-open` conferma aperture riuscite di `Stardat.mpq`, `patch_rt.mpq`, `BroodWar.mpq`, `Broodat.mpq`; i tentativi col prefisso `./` falliscono con `errno=0x14`, poi la risoluzione senza `./` riesce. Il trace mostra letture ripetute da posizioni elevate dell’archivio e allocazioni/deallocazioni mentre il processo è nello splash.

Contesto misurato sul file: `BroodWar.mpq` inizia con firma `MZ`; contiene un header `MPQ\x1a` a offset `0x48000`, e la dimensione dichiarata dall’header (`0x22113408`) coincide con la dimensione del file meno `0x48000`. Subito dopo l’apertura di `BroodWar.mpq`, il trace mostra scansione con seek/read da 32 byte a offset `0x00000000`, `0x00000200`, `0x00000400`…; questo è compatibile con la ricerca dell’archivio MPQ embedded, seguita da letture/allocazioni dei dati. È la spiegazione più concreta emersa per lo startup lungo, ma il trace da solo non prova che la scansione sia l’unico costo né identifica quale asset mantiene la UI su “Loading”.

Il thread principale è quasi interamente CPU-bound (circa un core), con letture e allocazioni frequenti; non sembra semplicemente addormentato in attesa di un timer. Le attese da 250 ms sono presenti ma 3,7 FPS con present sotto 1 ms indicano che la GPU/presentazione non è il collo di bottiglia. **Conclusione prudente:** il tempo è speso soprattutto nella fase di inizializzazione/caricamento/lettura e trattamento dei dati MPQ (con overhead CPU del runtime), non nel present GPU; manca però uno stack profiler per attribuire precisamente il costo a decompressione, parsing o routine del gioco.

Estratto delle ultime righe del trace: il processo continua a leggere e allocare da dati dell’archivio, poi attende e registra FPS, senza una transizione verso il menu:

```text
[trace] ReadFile pos=0x16c9dedc n=0x000005fc ...
[evt] Wait handle=0x5E000003 ms=0x000000FA result=0x00000102
[vm] VirtualAlloc ...
[trace] ReadFile pos=0x16c9f6d0 n=0x00000600 ...
[fps] GPU 3 fps, viewport 640x480, finestra 640x480
```

`SC_BRIDGE_TRACE` non compare nel codice runtime cercato; è stato usato `REASM_TRACE=1`. Artefatti in `work/qemu/pbp/`: `menu3-cpu-present.log`, `menu3-cpu-present-perf.csv`, `menu3-cpu-present-proc.txt`, `menu3-cpu-present-controller.log`, `menu3-cpu-present-20.png`, `menu3-trace.log`, `menu3-trace-proc.txt`, `menu3-trace-controller.log`.

## 12. Candidato `starcraft-armv6-menu8` — prova Pinebook Pro

- Hash host e Pinebook verificato: `e0688bc0cf7e508262afe0e6f7c0cc19773c6fac4df546ca2572e7b992f76fcc`; remoto `~/starcraft-armv6-test/starcraft-armv6-menu8`, eseguibile (`chmod +x`). Nessun pacchetto installato; il vecchio menu3 non è stato rimosso.
- Ambiente: `DISPLAY=:0`, `XAUTHORITY=/run/user/1001/.mutter-Xwaylandauth.L687Y3`, `XDG_RUNTIME_DIR=/run/user/1001`, PulseAudio, Smacker da `arm-libs/`, `SDL_VIDEODRIVER=x11`, `SC_SKIP_INTRO=1`, `SC_FPS_LOG=1`. Prima esecuzione limitata a 35 s; exit del wrapper `124` (timeout atteso, non exit 0). Log stdout/stderr verbatim: `work/qemu/pbp/menu8-clean.stdout-stderr.log` (copia del file remoto). Nessun input simulato.
- `xwininfo` ha identificato la finestra gioco `0x800016`. Catture dirette `import -window` salvate in `work/qemu/pbp/menu8-3.png`, `menu8-8.png`, `menu8-15.png`, `menu8-25.png`. A 3 s appare ancora la schermata Loading; da 8 s è visibile il menu principale effettivo (Single Player, Multiplayer, Campaign Editor, Exit; anche View Intro/Show Credits). Le immagini a 15 e 25 s mostrano animazione dello sfondo e dei pulsanti, menu stabile.
- FPS senza encoder: log riporta picco iniziale 62, poi valori stabili soprattutto 17–25, quindi circa 173–207 FPS in una successiva porzione del run; esempi `GPU 205 fps`, `GPU 207 fps`. Numerose righe `DoFrame` confermano attività Smacker. Nel log compare infine `[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto`; il processo non ha completato spontaneamente i 35 s, benché il menu fosse vivo alle catture. Il wrapper è terminato al proprio timeout. Il campionatore CPU/RSS previsto non ha prodotto il CSV (matching `pgrep` troppo restrittivo); CPU% e RSS sono **non disponibili**, non stimati.
- Prova video separata, 25 s, con `gst-launch-1.0 ximagesrc xid=0x800016 use-damage=0 ! videoconvert ! vp8enc deadline=1 threads=2 ! webmmux ! filesink ...`; artefatti: `menu8-capture.webm`, `menu8-video-gst.log`, `menu8-video.stdout-stderr.log`. Il gioco ha terminato con STOP durante la cattura e GStreamer ha registrato `BadDrawable` (finestra distrutta); non risulta chiusura EOS regolare. File WebM 839.222 byte; `ffprobe` locale identifica VP8, 640×480, 25 fps, ma non restituisce una durata valida. `ffprobe` non è installato sul Pinebook; verifica eseguita sull’host. Video parziale/non affidabile, non prova di 25 s completi.
- Nessun processo del gioco è rimasto sul Pinebook dopo le prove. Log e screenshot sono conservati ai percorsi sopra.

## Decisioni autonome

Non ho simulato click o tastiera. Ho eseguito il test autorizzato per 35 s; il menu è arrivato e si anima, ma lo STOP del runtime ha terminato il gioco. Ho proceduto alla cattura video come richiesto; il processo si è chiuso durante la registrazione, quindi il WebM è incompleto/solo diagnostico. Non ho lasciato processi appesi. La raccolta CPU/RSS è dichiarata mancante perché il selettore PID non ha prodotto campioni.
