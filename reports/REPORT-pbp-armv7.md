# Pinebook Pro (RK3399, aarch64 + multiarch armhf): build ARMv7 e prova reale

Data: 2026-11-22 · Host di build: frigate · Target: `enne2@10.0.1.126` (`pinebook-pro`, Ubuntu 22.04, kernel 6.1 aarch64)

## Esito sintetico
- **Build ARMv7 nativa: PASS.** `build/starcraft-armv7`: ELF32 ARM EABI5 hard-float, interpreter
  `/lib/ld-linux-armhf.so.3`, NEEDED solo `libc/libm/libgcc_s/libSDL2`, `--dump-map` 102.392 voci, 0 collisioni.
- **Esecuzione sul Pinebook: si avvia e arriva all'attivita' di menu**, ma **la finestra resta nera**.
  Vive >60 s senza STOP/SIGSEGV; FPS 23-24 a 640x480, poi 14-20 a 1280x960; CPU ~74% di un core; RSS 160-222 MB.
- **Non giocabile** finche' il presenter/palette ARM non disegna (vedi §4). Il blocco non e' piu' il build.

## 1. Cosa e' stato costruito (nuovo, nel repo)
- `tools/build-entry-arm.sh`: percorso ARM allineato a quello x86 (llc `--data-sections`, `gen_game_layout.py`,
  `gen_pe_image_bytes.py --arch arm`, `gen_storm_ordinals.py`, `gen_bridge_stubs.py --arch arm`),
  con `--sysroot`/`--ld-path` verso il sysroot armhf.
- `tools/build-runtime-armv7.sh`: runtime + shim **dai sorgenti del repo** (`runtime/shim-port/elf`,
  `runtime/arch/arm/*.S`), non piu' dai patch su `~/dev/sc-reasm`.
- `build/sysroot`: sysroot armhf Jammy minimale ricreato da `ports.ubuntu.com` (libc6, libc6-dev,
  linux-libc-dev, libgcc-s1, libsdl2-dev, libsdl2-2.0-0) senza root; piu' `build/armv7-libs/libsmacker-1.2.0.so`.

## 2. Bug critico trovato e corretto (era la causa del crash 0x1505FC20)
Sintomo: sul Pinebook il gioco moriva subito con `SIGSEGV fault address 0x1505FC20`, `guest eip 0`.

Causa: nel link ARM il farm di symlink `build/runtime-arm/game/link-entry/` passa al linker
`link-entry/storm.entry.o`, mentre `guest_data.ld` nominava il path **assoluto originale**
`build/lift-full/entry/storm.entry.o`. `ld.lld` confronta i pattern del linker script con il path
**come passato sulla riga di comando**: le due righe
`.guest_storm_rdata 0x150453C4` e `.guest_storm_data 0x15059000` non matchavano, quindi i blob di
`storm.entry.o` finivano nel `.rodata`/`.data` del port (0x18b502cc / 0x18d71a10) e i VA guest
0x150453C4 e 0x15059000 restavano **non mappati** → fault a 0x1505FC20 (dentro `.guest_storm_data`).

Fix (in `tools/build-runtime-armv7.sh`): il `.ld` di link viene riscritto anche nei path
(`sed "s|$ENTRY/|$LE/|g"`), oltre a `INSERT BEFORE .text`. Verifica: le sezioni guest passano da 13 a **15**
e compaiono `.guest_storm_rdata @0x150453C4 (0x116C4)` e `.guest_storm_data @0x15059000 (0x7FD8)`.
Diagnosi ottenuta con `-Map=/tmp/armv7.map` (non con ipotesi).

## 3. Misure sul Pinebook (run di 60 s, `SC_FPS_CAP=25`, `SC_FPS_LOG=1`)
- Avvio: MPQ caricati, `DllMain` storm OK, filmati menu aperti (`SmackOpen` x molti), `[storm-scode] fast-path C 1x LUT attivo`.
- `[fps] GL 23-24 fps, viewport 640x480` → `[host] fullscreen ON (desktop, 4:3 letterbox)` → `GL 14-20 fps, 1280x960`.
- CPU: ~3341 tick in 45 s = **~74 % di un core**. RSS: **222 MB** in menu, **160-176 MB** dopo.
- Nessuno STOP del runtime, nessun SIGSEGV per 60 s (rispetto alla build del 20/11 che si fermava al logo).

## 4. Cosa resta aperto
- **Finestra nera** su Pinebook sia con `SC_PRESENT=gl` (GL hardware/panfrost via Xwayland) sia con
  `SC_PRESENT=gpu` sia `cpu`: gli FPS avanzano e il log mostra disegno SCode, ma il contenuto non arriva a schermo.
  Da indagare: upload della palette/canvas nel percorso ARM, `SDL_UpdateWindowSurface`/swap su Xwayland,
  e l'interazione con `fullscreen bypass compositore`.
- Screenshot disponibili in `build/qa/pbp-armv7/{menu,cpu,gpu}.png` (desktop PBP con finestra gioco nera).
- Nota architetturale: il Pinebook e' **aarch64**; qui si usa il percorso **ARMv7 32-bit** (multiarch armhf
  gia' presente sul sistema). Un port **aarch64 nativo** sarebbe il target "pulito" ma richiede
  trampolini/dispatch AArch64 e un nuovo backend nel runtime: lavoro separato.

## Come riprodurre
```
# build (frigate)
SYSROOT=$PWD/build/sysroot tools/build-entry-arm.sh
SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh
# deploy + run
scp build/starcraft-armv7 enne2@10.0.1.126:~/starcraft-armv7-test/
ssh enne2@10.0.1.126 'cd ~/starcraft-armv7-test && DISPLAY=:0 XAUTHORITY=$(ls /run/user/1001/.mutter-Xwaylandauth.*|head -1) \
  XDG_RUNTIME_DIR=/run/user/1001 SDL_VIDEODRIVER=x11 SC_SMACKER_SO=$PWD/libsmacker-1.2.0.so SC_FPS_LOG=1 ./starcraft-armv7'
```
