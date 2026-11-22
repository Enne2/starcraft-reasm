# Missione: build ARM del gioco allo stato attuale e prova reale sul Pinebook Pro via SSH

## Contesto
- Target: **Pinebook Pro** (RK3399: 4×A53 + 2×A72), Ubuntu 22.04 aarch64 con **multiarch armhf gia' installato**
  (loader `/lib/ld-linux-armhf.so.3`, `libc6:armhf`, `libSDL2-2.0.so.0:armhf` 2.0.20).
  SSH funzionante: `enne2@10.0.1.126` (BatchMode, senza password).
- Sul target esiste gia' `~/starcraft-armv6-test/` con gli **asset MPQ** (Stardat/patch_rt/BroodWar/BrooDat),
  `import` e `convert` disponibili, sessione grafica utente su `DISPLAY=:0` (Xwayland di GNOME/mutter).
- Sul target esiste ancora il binario vecchio `starcraft-armv6-menu3` (build del 2026-11-20) che si fermava
  al logo/loading. Da allora il progetto ha ricevuto fix che contano anche su ARM (polarita' `llasm_cmps`,
  fast-path SCode, pacing, Huffman #552, ecc.).
- Report di riferimento con tutti i dettagli del target: **`work/qemu/REPORT-arm-pbp.md`** (leggilo prima).
- Build locale: `tools/build.sh --arch armv6|armv7` (triple `armv6-unknown-linux-gnueabihf` /
  `armv7a-unknown-linux-gnueabihf`, hard-float), `tools/build-runtime-arm.sh`, `tools/link-arm.sh`,
  `tools/compile-arm.sh`, `tools/build-entry-obj.sh`. **Non esiste una cross toolchain GCC armhf locale**
  e **non esiste piu' `build/sysroot`**: va ricreato.

## Obiettivi (in ordine)
1. **Ricreare il sysroot armhf locale** senza root, scaricando i .deb da `http://ports.ubuntu.com/ubuntu-ports/`
   (jammy) e scompattandoli in `build/sysroot/` (libc6, libc6-dev, linux-libc-dev, libgcc-s1, e SDL2:
   `libsdl2-dev` + `libsdl2-2.0-0` armhf per header e stub di link). Documenta le URL esatte e le versioni.
   Se un pacchetto non serve, non scaricarlo: il sysroot deve restare minimo.
2. **Costruire il gioco allo stato attuale del repo** per **armv7** (preferito: NEON/VFPv4 su A53/A72);
   fallback `armv6` se armv7 non linka. Produrre un ELF **32-bit ARM EABI5 hard-float** con
   interpreter `/lib/ld-linux-armhf.so.3`, e la `libsmacker` ARM (`tools/build-libsmacker.sh`).
   `--dump-map` deve restare coerente (nessuna collisione host/guest) e i gate esistenti devono passare.
3. **Deploy** su Pinebook via `scp` in una directory **nuova** `~/starcraft-armv7-test/` (non sovrascrivere
   `starcraft-armv6-test/`): binario + `libsmacker` ARM + symlink agli asset gia' presenti.
4. **Esecuzione reale** via SSH nella sessione grafica dell'utente, con l'ambiente corretto
   (`DISPLAY=:0`, `XAUTHORITY=/run/user/1001/.mutter-Xwaylandauth.*`, `XDG_RUNTIME_DIR=/run/user/1001`,
   `PULSE_SERVER=unix:/run/user/1001/pulse/native`), log su file, **nessun input simulato**
   (niente click/tasti: solo osservazione e screenshot con `import -window root`).
   Obiettivo: vedere **fin dove arriva** (logo, intro/menu, Select Game Type, Boot Camp) e misurare
   FPS, CPU% e RSS del processo (`/proc`), durata, eventuali STOP.
5. Confrontare con la build precedente (logo-only, 3,66 FPS, 104,7 % CPU, 71,5 MB RSS) e dire
   esplicitamente cosa e' migliorato per effetto dei fix recenti e cosa resta bloccato.

## Vincoli
- **Nessun `apt`/`apt-get`/`dpkg -i`/`sudo` sul target**, nessuna modifica fuori da `~/starcraft-armv7-test/`
  (e, se serve, dentro `build/` e `reports/`/`tasks/` in locale). Nessuna installazione di pacchetti.
- Nessun input simulato sul desktop dell'utente (no `xdotool`/`ydotool`/`wtype`), nessuna chiusura forzata
  di applicazioni dell'utente: termina solo i processi che hai avviato tu (per PID) e lascia il sistema pulito.
- Non modificare i sorgenti del gioco per "far apparire" il menu: se qualcosa non arriva, **documenta**.
  Se trovi un bug ARM reale, puoi proporre la patch in `reports/` ma **non** applicarla senza dichiararla.
- Non toccare le sessioni IDA di altri lavori; non fare `revert`/`stash` sul working tree.
- Se un passaggio e' impossibile, dichiaralo con l'errore esatto e proponi l'alternativa (es. aarch64 nativo).

## Deliverable
- `reports/REPORT-pbp-armv7.md`: sysroot usato (URL/versioni/hash), comandi di build esatti, `readelf -h`,
  `--dump-map`, deploy (hash locale/remoto), esecuzione (env, durata), log chiave, screenshot,
  tabella FPS/CPU/RSS, confronto con la build del 20/11, limiti e prossimi passi.
- `reports/STATE-pbp-armv7.md`: stato sintetico.
- Se la build armv7 produce un binario utilizzabile, allega i comandi per riprodurla in una riga.
