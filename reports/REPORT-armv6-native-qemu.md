# Report — build ARMv6 nativa e prova su QEMU `raspi0` senza X11

Data: 23 novembre 2026 · host: `frigate` (Fedora 44, Zen 4) · obiettivo: ricostruire la build ARMv6
del port, eseguirla nel guest `raspi0` già preparato, **senza X11**, con l'immagine visibile sullo
schermo di QEMU.

## 1. Esito in una riga

Build ARMv6 **fatta** (`build/qa/armv6-native/starcraft-armv6`, ELF32 ARMv6/VFPv2, 21,9 MB) ed
**eseguita nel guest raspi0 (ARM1176 emulato, senza Xorg)**: il frame arriva sullo schermo di QEMU
tramite il nuovo presenter `SC_PRESENT=fb` su `/dev/fb0`, catturabile con QMP `screendump`
(evidenza: `build/qa/armv6-native/shot-fb-1.png`, `seq-8.png`).

## 2. Perché serviva un presenter nuovo (la risposta alla domanda "si vede senza X?")

Misure nel guest `raspi0` (`-machine raspi0`, kernel Pi OS `6.12.75+rpt-rpi-v6`):

| Fatto misurato | Valore |
|---|---|
| CPU | `ARMv6-compatible processor rev 7 (v6l)`, `CPU part 0xb76`, features `vfp edsp java tls` — **niente NEON** |
| `/proc/fb` | `0 BCM2708 FB` → **`/dev/fb0` presente**, 32 bpp, 800x480, stride 3200 |
| `/dev/dri` | **assente** → KMS/DRM non disponibile (vc4 non istanziato: il DT viene da `bcm2708-rpi-zero.dtb`, nessun overlay firmware) |
| `libSDL2` Pi OS 2.26.5 | driver compilati: `x11`, `wayland`, `dummy`, `offscreen`. **Niente `fbcon`, niente `kmsdrm`** |
| QMP `screendump` | cattura il framebuffer: verificato (screenshot del desktop del guest prima di disattivare X) |

Conclusione: **senza X11 e senza DRM, SDL2 non ha alcun percorso per disegnare**. L'unico modo di
mostrare il frame è scrivere direttamente in `/dev/fb0` — che è anche ciò che accade su Pi Zero /
Pi 1 reali quando non c'è KMS. Da qui `SC_PRESENT=fb`.

## 3. Modifica al codice (unico file toccato)

`runtime/shim-port/elf/host_elf.c`, **+179/−7**, nessun altro file del repo modificato da questa
attività:

- `fb_present_requested()` — attivo solo con `SC_PRESENT=fb` (default invariato: `auto`→gl/gpu/cpu).
- `fb_open()` — `open("/dev/fb0")` + `FBIOGET_FSCREENINFO`/`FBIOGET_VSCREENINFO` (header
  `<linux/fb.h>` dal sysroot) + `mmap`; formati supportati 32 bpp (XRGB) e 16 bpp (RGB565);
  area 4:3 centrata nel pannello (letterbox nero); override device con `SC_FB_DEV`.
- `fb_build_maps()`/`fb_draw()` — palette `g_pal` (0x00RRGGBB) → pixel, scala nearest-neighbour con
  due mappe 16.16 precalcolate (una divisione per colonna/riga, non per pixel).
- Hook nel dispatcher del present: se `SC_PRESENT=fb` e il framebuffer apre, disegna e ritorna;
  altrimenti logga e ricade sul percorso SDL.
- `host_elf_init()`: con `SC_PRESENT=fb` forza `SDL_VIDEODRIVER=dummy` **prima** di `SDL_Init`
  (serve una window/surface "fantasma" per input, mouse e pump: la finestra non viene mai mostrata).
- Vincoli di stile rispettati: il file non include header libc (dichiarazioni a mano), quindi
  `ioctl`/`setenv` sono dichiarati e `O_RDWR`/`PROT_*`/`MAP_SHARED` sono costanti locali;
  il blocco è `#if defined(__arm__)` per non toccare la build i386.

## 4. Build: com'è fatta, e i tre difetti del build system trovati

Sysroot: `~/dev/starcraft-armv6/build/sysroot` (Debian GLIBC 2.36 `+rpt` = **Raspberry Pi OS
Bookworm armhf**, baseline ARMv6, con `libSDL2.so` di sviluppo). Toolchain host: clang/llc 22.1.8,
`ld.lld` 23.1.1 (rustup).

Ricetta riproducibile (script in `build/qa/armv6-native/`, **fuori** da `tools/` per non modificare
il repo senza autorizzazione):

```
SYSROOT=~/dev/starcraft-armv6/build/sysroot SC_REASM=~/dev/sc-reasm LDFLAGS_EXTRA=--no-relax \
  build/qa/armv6-native/build-entry-armv6.sh      # entry objects ARMv6 -> build/lift-full/entry-armv6
SYSROOT=... SC_REASM=... LDFLAGS_EXTRA=--no-relax \
  build/qa/armv6-native/build-runtime-armv6.sh    # runtime + shim + link -> build/qa/armv6-native/starcraft-armv6
```

Difetti trovati (workaround nelle copie QA, **repo non toccato**):

1. **`tools/build.sh --arch armv6` usa il percorso legacy `build-entry-obj.sh`**, che cerca la
   sezione `.rodata..L_text` nel linker script. Il lift attuale non la contiene (`grep -c L_text`
   su `StarCraft.ll`/`storm.ll`/`*.llasm` = 0) e `gen_game_layout.py` gestisce correttamente la sua
   assenza: la build ARMv6 si ferma con `sezione .rodata..L_text assente`. La ricetta moderna
   (`build-entry-arm.sh`) è usata solo da armv7. → `build.sh` dovrebbe chiamare `build-entry-arm.sh`
   parametrico per entrambe le architetture.
2. **`tools/link-arm.sh` legge gli shim da `build/runtime-arm/shim-port/obj` in modo hardcoded.**
   In un albero multi-arch quel percorso conteneva gli shim ARMv7/ARMv8 di un'altra build:
   il primo binario ARMv6 conteneva `movw/movt` + NEON e moriva con `SIGILL` sotto
   `qemu-arm -cpu arm1176`. Diagnosi: `llvm-objdump` su `win32_shim_init` mostrava `e30912b0`
   (movw) mentre l'oggetto ARMv6 aveva `ldr r2,[pc,#0x44]`; `readelf -A` dell'oggetto in
   `build/runtime-arm/shim-port/obj` = `Tag_CPU_arch: v8`, `VFPv4`. → serve un override
   (`SHIM_OBJ_DIR`) come per `RUNTIME_OBJ_DIR`.
3. **`tools/build-libsmacker.sh --arch armv6` non passa `--sysroot`**: compila con gli header host
   Fedora e fallisce su `__TC__`/`__float128`. Inoltre il `.so` già presente in
   `build/armv6-libs/` era **in realtà ARMv7** (`Tag_CPU_arch: v7`, `VFPv3-D16`): è stato
   ricostruito e ora è `v6`/`VFPv2`.

Nota: `--no-relax` è passato al linker per prudenza (LLD 23 sa rilassare `ldr`→`movw/movt`);
con gli oggetti corretti il rilassamento non si presenta comunque.

## 5. Prova nel guest (senza X11)

Ambiente: QEMU 9.2.2 `-M raspi0 -m 512M`, kernel+DTB dall'immagine Pi OS, disco = overlay qcow2 su
`build/qemu/runtime/guest-lite.raw` (immagine già provisionata), cmdline con
`systemd.unit=multi-user.target systemd.mask=lightdm.service systemd.mask=minimal-x.service`.
Il guest non ha `/lib/modules` per il kernel in uso → **niente rete** (nessun modulo RNDIS):
il deploy del binario è avvenuto **offline**, con `debugfs` (+ `e2fsck` per il journal) sulla
partizione 2 dell'immagine raw, SHA-256 host/guest verificato.

Risultati:

| Prova | Comando (nel guest) | Esito |
|---|---|---|
| Correttezza ARMv6, nessun display | `SC_PRESENT` assente, `SDL_VIDEODRIVER=dummy` | menu/animazioni a **19–47 FPS** (log `[fps] GPU`), nessun `SIGILL`; si ferma su asset mancante (vedi §6) |
| Output sul framebuffer, **senza X** | `SC_PRESENT=fb SC_SMACK=0 SC_SKIP_INTRO=1` | `[fb] present su /dev/fb0 800x480x32 stride=3200 area=640x480 offset=80,0`; frame visibile, `screendump` cattura (57 KB PNG); 1406 present registrati via `sc_ctl state` |
| Costo | `ps` durante il logo | RSS ≈ 30 MB, CPU ≈ 18% di un core (con pacing FPS) |
| RAM disponibile senza X | `free -m` | 344 MB liberi su 427 (con X attivo: 306 MB) |

Verifica di *fedeltà* del presenter: con `SC_PRESENT_DBG=1` durante la riproduzione SMK il log
riporta `[present-black] nz=0x00000000 why=blt` — il **canvas è davvero nero** in quella fase
(i filmati vanno in un buffer Smack separato, non nel canvas), quindi il nero a schermo non è un
difetto del presenter. Geometria e palette sono corrette: il frame compare nell'area 640x480
centrata, con i colori del logo StarCraft.

## 6. Problemi aperti (non introdotti da questa attività)

1. **`Could not load "glue\mainmenu\Exit.smk"`** — il gioco si ferma lì, **identicamente su i686**
   (log del binario x86 con lo stesso asset set) e su ARMv6: è un limite dell'insieme di asset
   attuale, non dell'architettura.
2. **Frame "a mosaico"** nella fase logo: lo schermo mostra il logo "Z" ripetuto in griglia
   (≈6x4). Il presenter è fedele (vedi §5), quindi il contenuto viene dal canvas. **Attribuzione
   non completata**: manca il confronto con la stessa fase su x86 (il runner isolato x86 non è
   partito: `no free Xvfb display in 100..299`). Prossimo passo: stesso frame su `starcraft-x86`
   con `SC_SMACK=0` e confronto delle due immagini; se x86 è corretto, è un difetto del percorso
   di blit ARM (candidati: `runtime/common/storm_sdraw.c` interprete SCode, `host_blit8`).

## 7. Attenzione: lavoro concorrente nel repo

Durante la sessione è comparso in `main` il commit `6621246 present: niente present a ogni Blt
(flicker + audio affamato), tick a cadenza` (23 nov 20:50) **non prodotto da questa attività**, e
restano modifiche non committate non mie (`runtime/common/storm_sdraw.c`,
`runtime/shim-port/elf/com_ddraw.c`, `runtime/shim-port/elf/smack_elf.c`, `reports/REPORT-race-select.md`
non tracciato). La build ARMv6 descritta qui è stata compilata **dall'albero di lavoro in quello
stato**: i binari ne riflettono il contenuto. Questa attività ha modificato solo `host_elf.c`
(non committato).

## 8. Come ripetere

```sh
# 1) build
SYSROOT=~/dev/starcraft-armv6/build/sysroot SC_REASM=~/dev/sc-reasm LDFLAGS_EXTRA=--no-relax \
  build/qa/armv6-native/build-entry-armv6.sh
SYSROOT=~/dev/starcraft-armv6/build/sysroot SC_REASM=~/dev/sc-reasm LDFLAGS_EXTRA=--no-relax \
  build/qa/armv6-native/build-runtime-armv6.sh

# 2) deploy offline (VM spenta): e2fsck -> debugfs write -> dd della partizione 2 sull'immagine
#    (i comandi esatti sono in §5; base: build/qemu/runtime/guest-lite.raw, offset 1064960 settori)

# 3) avvio guest senza X
IMAGE=$PWD/build/qemu/lite.img DISK=$PWD/build/qemu/runtime/guest-lite.rc.qcow2 \
KERNEL=$PWD/build/qemu/kernel.img DTB=$PWD/build/qemu/bcm2708-rpi-zero.dtb QEMU_DISPLAY=none \
KERNEL_APPEND="systemd.unit=multi-user.target systemd.mask=lightdm.service systemd.mask=minimal-x.service" \
./tools/qemu/run.sh

# 4) console seriale e screenshot
python3 build/qa/armv6-native/gcon.py login
python3 build/qa/armv6-native/gcon.py 'cd ~/sc && SC_PRESENT=fb SC_SMACK=0 SC_SKIP_INTRO=1 SC_SMACKER_SO=$PWD/libsmacker-armv6.so ./sc-armv6-qa'
python3 tools/qemu/qa-shot.py build/qa/armv6-native/shot.png
```

Nota: per guardare dal vivo si può avviare QEMU con `QEMU_DISPLAY=sdl` (finestra sul desktop host):
il guest resta senza X11, l'immagine la disegna QEMU.

## 9. Artefatti

- `build/qa/armv6-native/starcraft-armv6` — binario ARMv6 (`sha256 5e90660e…`, 21 958 484 B)
- `build/qa/armv6-native/shot-fb-1.png`, `seq-8.png` — frame sul framebuffer del guest via QMP
- `build/qa/armv6-native/shot-console.png` — desktop del guest (fase con X attivo, per confronto)
- `build/qa/armv6-native/{build-entry-armv6.sh,build-runtime-armv6.sh,link-armv6.sh,gcon.py}` — ricetta QA
- `build/armv6-libs/libsmacker-1.2.0.so` — ricostruita correttamente ARMv6/VFPv2
- `build/qemu/` — QEMU raspi0: `kernel.img`, `bcm2708-rpi-zero.dtb`, `lite.img`, `runtime/guest-lite.raw`

---

# Round 2 — verifica dei fix x86/ARMv7, rebuild ARMv6, misure FPS e valutazione del present

Data: 23 novembre 2026, 23:05–23:30. Contesto: un altro agente ha chiuso i bug grafici (commit
`705a300 grafica: ritratti campagna (limiti SmackToBuffer), filmato ARM nero, flicker intro`), che
include anche la **prima versione** del mio presenter fb (era già nel working tree).

## 10. Verifica delle modifiche dell'altro agente (4 fix, tutti arch-neutri)

| File | Modifica | Rilevanza per ARMv6 |
|---|---|---|
| `runtime/shim-port/elf/smack_elf.c` | `buffer_range_find`/`smack_elf_buffer_owner()`; `copy_frame_data` valida l'**ultimo byte scritto** (`(top+rows-1)*pitch+left+w`) invece di `pitch*dh`; `host_elf_movie_end` su `SmackBufferClose` | sì: i film con destinazione "con padding" non vengono più scartati |
| `runtime/common/storm_sdraw.c` | dopo il composito full-screen (`run_2x_lut`) chiama `host_elf_movie_present(movie)` (weak symbol) | sì: è la correzione "intro nera ARM" (Bug 3 del loro report) |
| `runtime/shim-port/elf/com_ddraw.c` | `present(s)` → `present_tag(s,"unlock")` (diagnostica) | neutro |
| `runtime/shim-port/elf/host_elf.c` | `host_present_frame(int movie)`, `g_movie_buffer`, `host_elf_movie_present/end`: i frame di filmato non passano da dedupe/cap/pump e i present non-movie vengono sospesi finché un film è attivo | sì (coesiste con il presenter fb) |

Nessuno dei fix è specifico dell'architettura: la build ARMv6 li assorbe senza modifiche.
Nessun conflitto con il presenter fb: il filmato viene **composto nel canvas** da Storm, quindi
`fb_draw()` che legge `g_canvas` è già corretto (non serve leggere `g_movie_buffer`).

## 11. Rebuild ARMv6 e prova

Ricetta invariata (§4). Nuovo binario: `build/qa/armv6-native/starcraft-armv6`,
**sha256 `70b01f12cc759c91…`, 21 961 900 B** (il precedente, con i soli fix e presenter v1, era
`c5233bd3a183da6a…`).

Prova nel guest `raspi0` senza X11 (`SC_PRESENT=fb`, `SDL_AUDIODRIVER=dummy`): **il menu principale
di StarCraft è visibile sullo schermo di QEMU** — Single Player / Multiplayer / Campaign Editor /
Exit / View Intro / Show Credits, nave e pianeta, colore corretto (`build/qa/armv6-native/mv-3.png`,
130 KB). Il mosaico della §6 era la fase in cui il composito del filmato non arrivava al canvas:
con i fix non si presenta più.

Diagnostica coerente: `[present-seq] … why=movie nz=0x338 h=0xD45FC4` (canvas con contenuto e hash
che cambia frame per frame) e 65 `[present-black]` solo nelle prime fasi; RSS ≈ 37 MB.

## 12. Misure FPS (present/s = frame distinti mostrati)

Metodo: `sc_ctl state` (contatore `presented`) campionato due volte a 10 s di distanza durante la
stessa fase, ambiente identico, nessun X, audio dummy. Fase "logo/animazione" = 14–24 s dall'avvio;
fase "menu" = dopo 30 s (menu statico con elementi animati: i present coincidono con i frame
*visivamente distinti*, quindi il numero è la resa reale a schermo).

| Fase | Presenter | present/s | frame time |
|---|---|---:|---:|
| logo/animazione | fb **v1** (mappe + AND per pixel) | 33,4 | 29,9 ms |
| logo/animazione | fb **v2** (LUT 32-bit + fast path 1:1 + unroll 4) | **43,4** | **23,0 ms** |
| logo/animazione | SDL CPU su finestra dummy (nessun write su fb) | 45,5 | 22,0 ms |
| menu | fb **v1** | 10,2 | 98 ms |
| menu | fb **v2** | **13,9** | 72 ms |
| menu | SDL CPU su finestra dummy | 11,0 | 91 ms |
| logo/animazione | fb **v2** con framebuffer **16 bpp** (`fbset -depth 16`) | 39,3 | 25,4 ms |

Lettura dei dati:

- L'ottimizzazione del presenter vale **+30 % (logo)** e **+36 % (menu)**; il costo del present fb
  scende da **~7,9 ms a ~1 ms** per frame (differenza v1→v2 a carico di gioco invariato).
- **16 bpp è peggiore** (39,3 vs 43,4): dimezza i byte ma non il numero di store, che è il vero
  limite su ARM1176 (`strh` costa quanto `str`). Il percorso 32 bpp + LUT resta il migliore.
- Sul menu il costo del present v2 (13,9 present/s) è **inferiore** al presenter SDL-CPU (11,0):
  il fast path 1:1 batte `present_scaled` (che rifà mappe e scala).

## 13. Valutazione delle ottimizzazioni residue (percorso rendering/presentazione, senza OpenGL)

1. **Fatto (v2)**: LUT `rgb32[256]` ricostruita solo quando `g_pal_dirty` (toglie una AND e una
   lettura a 4 byte per pixel); fast path **1:1** quando l'area di destinazione coincide col canvas
   (caso reale 640x480 → 640x480: elimina due mappe per pixel); ciclo srotolato a 4.
2. **Scartato dopo misura**: framebuffer 16 bpp. Se un target reale avesse fb nativo 16 bpp,
   l'unica ottimizzazione sensata è **impaccare 2 pixel per store a 32 bit** (`pal16[a] | pal16[b]<<16`),
   perché il collo di bottiglia sono gli store, non la banda.
3. **Dirty-rect present** (priorità bassa): il present ora costa ~1 ms su ~23; il guadagno massimo è
   quindi ~4 % anche nei menu dove solo una parte del canvas cambia, e in partita il full-screen
   annulla il vantaggio. Costo: tracciamento della dirty box in `storm_sdraw.c`/`gdi_elf.c` +
   coordinamento con `host_elf`. Non conveniente ora.
4. **Coalescing** (`SC_PRESENT_MIN_MS`, default 12 ms ≈ 83 fps): non è il limite a 43 o 14 present/s.
5. **Il costo residuo (22–23 ms/frame al logo, 72 ms al menu) è del gioco**, non del present:
   blit SCode/x86 interpretati, `reasm_indirect` sugli archi indiretti, clear del canvas a 8 bpp.
   Il prossimo lavoro va lì (fast-path assembly della cache di dispatch, come già indicato in
   `PLAN-PERF-OPT.md` §2 Fase A/D), non sul presenter.
6. **Su hardware reale** (Pi Zero/Pi 1): `bcm2708_fb` mappa il framebuffer in write-combining, quindi
   il costo per store è più basso che in QEMU TCG; il rapporto conversion/gioco migliorerà ancora a
   favore della conversione. Utile verificare `fb_line` reale e la profondità nativa con `fbset`.
7. **Profiling**: `SC_PROF=1` non ha prodotto il report nel guest (`[SC_PROF] samples` assente dopo
   70 s, 0 campioni): le quote qui sopra sono ricavate per **differenza A/B fra configuratori**,
   non da campionamento. Se serve un profilo vero, il profiler va verificato su target nativo.

## 14. Stato del repository dopo il round 2

- HEAD = `705a300`, che contiene i 4 fix **e** la prima versione del presenter fb.
- Working tree: **solo** `runtime/shim-port/elf/host_elf.c` modificato (versione v2 del presenter:
  LUT + fast path 1:1 + unroll + log `[fps] FB`), **non committato**. Nessun altro file toccato.
- Artefatti nuovi: `build/qa/armv6-native/{mv-1..4.png,dlg.png}`, `guest/{ctl.py,fps.py}`,
  `build/qa/armv6-native/starcraft-armv6` (70b01f12…).
