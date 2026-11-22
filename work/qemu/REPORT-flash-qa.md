# REPORT — QA del port ARMv6 nel guest QEMU raspi0 (fase 1: readiness)

**Data:** 2026-11-20, 15:10–15:30 (Europe/Rome).
**Brief:** `work/qemu/TASK-flash-qa.md`. **Stato di partenza:** `CHECKPOINT.md` (13:10).
**Fase:** 1 (readiness) **completata**; **fase 2a/2b (prerequisiti) COMPLETATA**: `apt-get clean`,
`libsdl2-2.0-0` installata e configurata, **tutti e tre gli MPQ trasferiti e verificati** (§14).
Nessun binario del gioco è stato eseguito (né in guest né con `qemu-arm-static`). Manca solo il
**candidato con hash** dal supervisore.
**Oracolo:** decompilato IDA del binario originale (area del collega). In questa fase la QA non ha
riscritto semantica: si limita a prerequisiti, misure e strumenti. Nessuna funzione di gameplay è
stata interpretata qui.

---

## 0. TL;DR

| Voce | Esito |
|---|---|
| Guest QEMU raspi0, SSH slirp 2222, VNC 5901 | **OK** (verificato) |
| Sessione X11 nel guest (`minimal-x.service`, 800×480) | **OK** (screenshot QMP allegato) |
| `libSDL2-2.0.so.0` nel guest | **INSTALLATA e configurata** (2.32.4; `dpkg --audit` vuoto, `ldd` 0 mancanti) → §4 |
| Spazio nel guest | **risolto**: `apt-get clean` → **948.215.808 B (904 MiB)** liberi; 643 MiB di asset ci stanno con ~254 MiB di margine |
| Asset richiesti | **tutti e tre trasferiti e verificati**: `patch_rt.mpq` + `StarDat.mpq` + `Starcraft.mpq` = 674.128.427 B, `sha256sum -c` → OK/OK/OK (§14.1) |
| Metodo trasferimento + identità | `scp` su loopback + `sha256sum` bilaterale + manifest QA (throughput da misurare) |
| Identità del binario | **nessun build-guard nel port ARM**: solo sha256/manifest lato QA |
| Percorso deterministico fino alla partita | **non esiste** per single-player: serve replay registrato o un passo manuale |
| VM | **uccisa 4 volte da un progetto esterno** (`esp95` gate, `kill` su `comm ~ ^qemu-system`), 2 delle quali in fase 2a |
| Blocchi per il supervisore | §8 (killer esterno; SDL2+sudo nel guest; accesso al gameplay; spazio) |

---

## 1. Vincoli rispettati e integrità della base

- `build/qemu/lite/lite.img` (2.785.017.856 B) **non toccata**: mtime `2026-11-20 10:42:42`
  invariato anche dopo il rilancio della VM; QEMU la apre solo come *backing file* dell'overlay.
  Si scrive solo su `build/qemu/runtime/guest-lite.qcow2`.
- `KERNEL`/`DTB` (`build/qemu/lite/boot/{kernel.img,bcm2708-rpi-zero.dtb}`) invariati
  (mtime `10:37:34`).

**Conferma esplicita (per il brief):** la base non è stata alterata — l'unica scrittura su disco del
rig è il qcow2 dell'overlay, come previsto da CHECKPOINT §11; nessun `e2fsck`/`debugfs` sulla base.
- **Nessuna modifica** a `runtime/`, `build/runtime-arm/`, `~/dev/sc-reasm`, `~/dev/m-ht-sr`,
  sorgente QEMU condiviso, `tools/link-arm.sh`.
- **Nessuna modifica** al filesystem del guest in questa fase: niente `apt install`, niente copia
  di asset, niente `apt clean`. L'overlay ha registrato solo le scritture normali di boot/journal.
- Nessun input simulato (né host né guest): l'unico accesso è stato SSH/console e `screendump` QMP.
- Rete: nessun cambio di configurazione. Slirp `hostfwd 127.0.0.1:2222→22` come da CHECKPOINT;
  `passt` non usato.
- Nessun `git`, nessun `sudo` sull'host, nessuna installazione host.

**Solo scritture fatte:** `work/qemu/REPORT-flash-qa.md`, `work/qemu/evidence/*`,
`build/qemu/launch-flash-qa.log` (+ log runtime del QEMU rilanciato).

---

## 2. VM: identità esatta e **incidente di terminazione esterna**

### 2.1 Stato attuale (dopo rilancio QA)

| cosa | valore |
|---|---|
| QEMU (attuale) | pid **361253**, rilanciato da QA alle **15:24:35** con il comando di CHECKPOINT §8 |
| QEMU (precedente) | pid 3404331 — **terminato alle 15:20:30**, non da QA |
| viewer VNC host | pid 3405541 (`TigerVNC`, `127.0.0.1::5901`) — morto con il vecchio QEMU (End of stream) |
| dischi | overlay `build/qemu/runtime/guest-lite.qcow2`, virtual size 4 GiB, allocato 1,42 GiB |
| rete | slirp `hostfwd tcp:127.0.0.1:2222-:22`, guest `10.0.2.15`, VNC `127.0.0.1:5901` |
| kernel | 6.18.50+rpt-rpi-v6 (Trixie), `/proc/cpuinfo` `CPU part 0xb76` rev 7 |
| SSH | `build/qemu/ssh/gssh` (askpass, password auth) — **funziona** |
| GUI | `minimal-x.service` **active**, Xorg `:0` su vt1, fbdev 800×480 |
| uptime verificato post-rilancio | SSH disponibile **88 s** dal lancio; `nproc`=1, RAM 427 MiB (131 usati, 295 disponibili), no swap |

Rilancio usato (identico a CHECKPOINT §8, solo il log cambiato):

```bash
IMAGE=$PWD/build/qemu/lite/lite.img KERNEL=$PWD/build/qemu/lite/boot/kernel.img \
DTB=$PWD/build/qemu/lite/boot/bcm2708-rpi-zero.dtb \
DISK=$PWD/build/qemu/runtime/guest-lite.qcow2 SSH_FWD=1 QEMU_DISPLAY=none \
  setsid nohup env QEMU=/tmp/qemu-arm-install/usr/local/bin/qemu-system-arm \
  ./tools/qemu/run.sh -vnc 127.0.0.1:1 -k it >build/qemu/launch-flash-qa.log 2>&1 &
```

### 2.2 Causa delle due terminazioni (evidenza, non ipotesi)

```
$ tail build/qemu/launch-slirp-final.log
qemu-system-arm: terminating on signal 15 from pid 331341 (bash)
$ tail build/qemu/launch-flash-qa.log            # QEMU rilanciato da QA, ucciso dopo 9 s
qemu-system-arm: terminating on signal 15 from pid 331341 (bash)
```

Il pid 331341 era `bash ./run-gate.sh MI1.lean-2.ELF lean2-pcspk pcspk` con cwd
`/home/enne2/projects/esp95/build-lean` — **un altro progetto**, che ha appena riscritto il proprio
gate proprio per cambiare il criterio di kill:

```bash
# ~/projects/esp95/build-lean/run-gate.sh (letto da /proc, non modificato da QA)
# match on comm (never on the full command line: that would match this shell)
for p in $(ps -eo pid,comm | awk '$2 ~ /^qemu-system/ {print $1}'); do kill "$p" 2>/dev/null || true; done
```

`comm ~ /^qemu-system/` **colpisce ogni emulatore QEMU system**, quindi anche `qemu-system-arm`
(`raspi0`) di questo progetto. Cadenza misurata dai file del loro gate: `last-0..3.txt` alle
15:02:23, 15:10:57, 15:17:19, 15:24:06 → **un ciclo ogni ~7 minuti**, ogni ciclo uccide la VM.
È esattamente la trappola §10.2 del CHECKPOINT (kill per pattern), in versione cross-progetto.

Rilanciato alle 15:24:35, il nuovo QEMU è stato ucciso alle **15:22:09** dal ciclo precedente del
medesimo script; il rilancio successivo (pid 361253) è vivo al momento di questo report ma è
**esposto al ciclo delle ~15:31**.

> Conseguenza operativa: finché quel gate gira con quel pattern, **ogni misura lunga nel guest è
> interrompibile a caso**. Non è un difetto del port né di QEMU. Vedi §8.1.

### 2.3 Evidenza di prontezza GUI (prima della prima uccisione)

`work/qemu/evidence/fase1-baseline-x11.png` — `screendump` QMP delle 15:19, 800×480:
desktop openbox con `lxterminal` intitolato “StarCraft ARMv6” al prompt (`pi@raspberrypi:~$`).
Nessun input inviato. Log seriale pre-uccisione conservato in
`work/qemu/evidence/fase1-serial-boot-until-kill.log` (boot completo, `lightdm` assente,
`minimal-x.service` OK, `multi-user.target` raggiunto).

`work/qemu/evidence/fase1-after-relaunch.png` — stesso desktop sulla VM rilanciata da QA
(pid 361253, cattura delle 15:28 con `tools/qemu/qa-shot.py`): il rig è di nuovo **pronto**
(X11 attivo, terminale al prompt). Questa è la prova di readiness che la fase 1 doveva produrre;
non contiene né menu né partita, e non viene spacciata per tale.

### 2.4 Fase 2a (prerequisiti) — **due uccisioni consecutive → STOP come da istruzione**

| # | ora | VM (pid) | killer | prova |
|---|---|---|---|---|
| 1 | **15:38:46** | 361253 | pid **478340** (`bash`) — `esp95/build-lean/run-gate.sh MI1.lean-3.ELF lean3` | `work/qemu/evidence/fase2-launch-kill1.log`: `qemu-system-arm: terminating on signal 15 from pid 478340 (bash)` |
| 2 | **15:40:24** | 489002 (rilancio) | **stesso pid 478340**, 98 s dopo | `work/qemu/evidence/fase2-launch-kill2.log` (65 B); serial di quella VM fermo a 2,8 s di boot (`fase2-serial-kill2.log`) |

La prima uccisione è arrivata **a metà `dpkg --unpack`** dell'installazione SDL2; la seconda
**4 secondi dopo il rilancio** (boot a 2,8 s), quindi la riparazione non era ancora possibile.
Stesso comando del §2.2, stesso `bash` che sopravvive fra i due cicli: il gate esp95 uccide
`qemu-system*` **più volte dentro la stessa esecuzione**, non solo una per ciclo.

Carico host durante le operazioni (misurato con `uptime`): **9,9 → 11,9 → 13,6** (16 thread).
Conseguenza: SSH verso il guest andato in timeout ripetutamente anche a VM viva — cioè il rig,
oltre a essere ucciso, è **inservibile per misure di performance** mentre quel gate gira.

**Azione:** fermato qui (§8.1). Evidenza salvata in `work/qemu/evidence/`.

---

## 3. Prerequisiti nel guest — misurati

| Requisito | Valore misurato | Esito |
|---|---|---|
| Distro / libc | Debian 13 (Trixie) armhf, **glibc 2.41** | OK: il binario chiede al massimo `GLIBC_2.34` |
| CPU visibile | 1 core ARM1176 (`vfp edsp java tls`, **niente NEON**) | coerente con `-march=armv6zk -mattr=+vfp2,-neon` |
| RAM | 427 MiB totali, 131 usati, **295 disponibili**, no swap | OK per il gioco (canvas 640×480 + MPQ streaming) |
| Spazio `/` | 3.4 G totali, 2.5 G usati, **759.222.272 B liberi (724 MiB)** | **stretto** (§5) |
| `/tmp` | tmpfs **214 MiB** (non usabile per gli asset) | — |
| X11 | Xorg `:0` su vt1, fbdev `/dev/fb0` 800×480 (video memory 1500 kB) | OK |
| `DISPLAY` | `:0` | OK |
| `XAUTHORITY` | `/tmp/serverauth.Kly2S31P4J` (file di `startx`, **non** `~/.Xauthority`) — il percorso cambia a ogni avvio di X | scoprire da `/proc/<Xorg>/cmdline` (`-auth …`) |
| `libSDL2-2.0.so.0` | **assente** | **blocco** → §4 |
| audio (ALSA/pulse) | `libasound2t64` presente, **nessuna scheda audio in raspi0** | il fallimento è gestito (log + `return -1`) |
| `conspy` | `/usr/bin/conspy` presente, `/dev/vcs2` e `/dev/tty2` esistono; **vt1 è di Xorg** | conspy utilizzabile su tty2, **non** su tty1 mentre X la possiede |
| strumenti utili | `sha256sum`, `scp`, `sftp`, `python3`, `wget`, `curl`, `gcc`, `make`, `htop`, `top`, `vmstat`, `xauth`, `xset`, `xrandr` | OK |
| strumenti **mancanti** | `git`, `xwd`, `import`, `xwininfo`, `xlsclients`, `xprop`, `xdotool`, `wdmctrl`, `sar`, `pidstat`, `strace` | nessuno è indispensabile: screenshot via QMP, misure via `/proc` |
| servizi attivi | `minimal-x`, `ssh`, `NetworkManager`, `cron`, `dbus`, `serial-getty@ttyAMA0` | OK |

Verifica X funzionante senza `xdpyinfo`:

```bash
DISPLAY=:0 XAUTHORITY=/tmp/serverauth.Kly2S31P4J xrandr
# Screen 0: minimum 800 x 480, current 800 x 480, maximum 800 x 480
```

---

## 4. Strategia SDL2 (blocco risolvibile in fase di deploy)

Il binario dichiara:

```
NEEDED libc.so.6, libm.so.6, libgcc_s.so.1, libSDL2-2.0.so.0     (nessun RPATH)
```

Nel guest `libSDL2-2.0.so.0` **non esiste**. Due strade misurate:

**A) `apt` nel guest (raccomandata).** `apt-get -s install libsdl2-2.0-0` (simulazione, nessuna
scrittura): **11 pacchetti nuovi, 0 rimossi**, `libsdl2-2.0-0 2.32.4+dfsg-1` da
`raspbian.raspberrypi.com trixie/main armhf` + `libpulse0, libxss1, libsndfile1, libmpg123,
libopus0, libmp3lame0, libvorbisenc2, libasyncns0, libdecor-0-0, libdecor-0-plugin-1-gtk`.
Rete slirp attiva e liste `apt` già in cache (85 MiB in `/var/lib/apt/lists`). Peso stimato
dell'ordine di pochi MiB di `.deb`. Richiede `sudo` **dentro il guest** (password del team, non
riportata qui) — vedi §8.2.

**B) copia della `.so` dal sysroot** (`build/sysroot/.../libSDL2-2.0.so.0.2600.5`, 1,5 MiB, quello
con cui il binario è stato linkato). Non usata come prima scelta: le sue dipendenze
(`libpulse.so.0`, `libdecor-0.so.0`, `libsamplerate`, `libXss`, …) non sono tutte presenti nel guest,
quindi servirebbe comunque `apt` per le dipendenze. Resta come fallback offline.

**Stato (aggiornato 15:51, fase 2b): SDL2 INSTALLATO E CONFIGURATO.**
`dpkg --configure -a` ha completato la transazione interrotta (nessun output = niente da fare),
`dpkg --audit` **vuoto**, e il pacchetto risulta:

```
ii  libsdl2-2.0-0:armhf 2.32.4+dfsg-1 armhf   Simple DirectMedia Layer
/usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0 -> libSDL2-2.0.so.0.3200.4   (1.677.260 B)
ldd ... | grep -c "not found"  ->  0
```

Quindi **tutti e quattro i `NEEDED` del binario ora risolvono nel guest**: `libc.so.6`, `libm.so.6`,
`libgcc_s.so.1` (presente), `libSDL2-2.0.so.0` (installata). Verifica fatta **senza eseguire** il
binario (controllo dei nomi + `ldd` sulla libreria, non sul gioco).

Costo misurato dell'installazione: **~99 MB** di spazio occupato con soli **4,4 MB** di `.deb`
(libero 948.215.808 → 845.242.368 B). Il delta è dovuto ai file scompattati dei 11 pacchetti
(`libpulse0`, `libsndfile1`, `libdecor`, …) — coerente con l'ordine di grandezza, ma **non**
verificato pacchetto per pacchetto: da chiudere con
`dpkg-query -W -f='${Package} ${Installed-Size}\n' libsdl2-2.0-0 libpulse0 libsndfile1 …`.

**La strada B resta non usata** (non serve più).

**Driver video.** In guest serve `SDL_VIDEODRIVER=x11` (il default `dummy` dello stage 9 host non
vale qui). `SC_PRESENT` (default `gpu`) prova `CreateRenderer(ACCELERATED)` → su ARM1176 emulato
finirebbe su **llvmpipe** (`mesa-libgallium` installato): da misurare A/B contro `SC_PRESENT=cpu`
(scalatura CPU su `XShm`). Il percorso CPU logga “present medio X ms” — è già la metrica utile.

**Audio.** raspi0 non espone scheda audio: `SDL_OpenAudioDevice` fallisce e il runtime lo gestisce
(log, `return -1`, gioco avanti). Per misure deterministiche la scelta da dichiarare è
`SDL_AUDIODRIVER=dummy` (+ eventualmente `SC_SMACK_AUDIO=0`): va scritta nel manifest della misura,
perché **cambia il comportamento osservabile** (lezione dell'incidente “schermata senza animazioni”).

---

## 5. Asset MPQ: cosa serve davvero, dimensioni, hash

Il runtime ARM apre **solo due MPQ** per il gioco originale (grep su
`build/runtime-arm/shim-port/tools/elf/win32_shim.c`: risoluzione **case-insensitive** dei path,
`"Stardat.mpq"`→`StarDat.mpq`):

| file | byte | sha256 (host, read-only) | serve? |
|---|---|---|---|
| `Starcraft.mpq` | 606.857.006 | `17fca7b2746af43e161c8acc3970115f655cb0e239bdee5544eed81f3cecb10b` | **sì** |
| `StarDat.mpq` | 66.184.568 | `0cf90e52b5bb65a6086d25d4c5c5bbd070adf04d12fb19be035af2db39737cd2` | **sì** |
| `patch_rt.mpq` | 1.086.853 | `f692f7e4ddcd10255ac866bffd56be72f7dd717b67eb35af7442deca394501ca` | sì (patch in cima) |
| `BrooDat.mpq` | 23.847.431 | `8c2c8ad5b32d4caf05d9e9d6727f46872ce6fc54f2e69ad6d6364cd1ea58c3c4` | solo Brood War |
| `BroodWar.mpq` | 571.847.688 | `180425f53094c71b208b1244dab3cb2f63492a9120d96e3a9362f4c5ff7404b2` | solo Brood War |

- **Set minimo (menu + partita originale):** 674.128.427 B = **643 MiB**.
- **Con Brood War:** +595 MiB → 1,19 GiB.
- Sorgente: `~/dev/final-starcraft/Starcraft/` (**read-only**, nessuna copia, nessun upload).

### 5.1 Aritmetica dello spazio (misurata, non stimata)

```
libbero nel guest / prima della pulizia       759.222.272 B (724 MiB)   [misura 15:30:03]
dopo `apt-get clean` (181 MiB di cache)      948.215.808 B (904 MiB)   [misura 15:33:16]
  = spazio effettivamente disponibile per il deploy
set minimo MPQ (3 file, hash al §5)          -674.128.427 B (643 MiB)
binario + manifest                              -8.100.000 B (~8 MiB)
--------------------------------------------------------------------------
margine atteso dopo il trasferimento         ~ 266.000.000 B (~254 MiB)   OK
```

Durante l'installazione SDL2 il libero è sceso a **853.905.408 B (814 MiB)** con gli 11 `.deb`
già scaricati: **~94 MB** consumati su un contenuto di `.deb` di 4,4 MB. La differenza (unpack di 11
pacchetti + journal/backup `dpkg` + ext4) è **più alta dell'atteso** e resta **non spiegata** finché
non si può interrogare il guest: da verificare alla ripresa (`du -sh /usr/lib/arm-linux-gnueabihf`,
`dpkg-query -W --showformat='${Installed-Size}\n'`, `du -sh /var/cache/apt /var/lib/dpkg`).
Non incide sul piano (il margine resta > 250 MiB), ma va chiusa come misura.

Spazio recuperabile **senza toccare l'immagine base**: `/var/cache/apt` (già azzerato, 181 MiB
liberati) e `/var/lib/apt/lists` = 85 MiB (da **tenere**, serve se si installa altro offline).

**Brood War resta fuori scope** in questo set: +571.847.688 B (545 MiB) non ci stanno senza allargare
il disco. Nessun trasferimento è stato eseguito in questa fase: gli originali in
`~/dev/final-starcraft/Starcraft/` sono **intatti** (mtime 2019, sola lettura).

### 5.2 Alternative valutate (non adottate ora)

1. **NBD read-only via slirp** (disco guest intatto): `qemu-nbd -r` sull'host su `127.0.0.1`
   + `nbd-client` nel guest. Il modulo c'è (`/lib/modules/6.18.50+rpt-rpi-v6/kernel/drivers/block/nbd.ko.xz`)
   e `qemu-nbd`/`qemu-img` esistono sull'host; `nbd-client` **manca** (apt, piccolo). Costo: una
   seconda copia degli asset sull'host (host libero: 186 GiB) e una dipendenza in più. Pronto come
   piano B se lo spazio nel guest si rivelasse insufficiente.
2. **9p/virtfs**: non applicabile — `-M raspi0` non espone un bus virtio
   (`hw/arm/raspi.c` senza virtio; `-device virtio-9p-device` richiede `virtio-bus`). Il modulo
   `9p.ko` nel guest non basta senza trasporto.
3. **Allargare il disco** (`qemu-img resize` + `block_resize` QMP + `resize2fs` online): possibile
   in teoria, ma è un cambio di stato del rig a VM viva e richiede una prova dedicata: **decisione
   del supervisore**, non l'ho fatto.
4. **HTTP loopback host→guest (10.0.2.2:…)** come *canale di trasferimento* alternativo: da
   validare; per il *runtime* non serve, il gioco vuole file su disco.

---

## 6. Trasferimento, identità dell'aggiornamento, launcher, performance

### 6.1 Trasferimento + identità (metodo **misurato**, con ripresa)

```bash
# host: hash di sorgente (già fatto, §5)
sha256sum ~/dev/final-starcraft/Starcraft/{Starcraft,StarDat,patch_rt}.mpq
# copia con la stessa auth di gssh (helper nuovo: build/qemu/ssh/gscp)
./build/qemu/ssh/gscp <asset> pi@127.0.0.1:/home/pi/sc/
# guest: verifica bilaterale
cd /home/pi/sc && sha256sum <asset>      # confronto con i valori del §5
```

**Throughput misurato su questo rig (2026-11-20, carico host 7–10 da un altro progetto):**

| prova | dati | tempo | rate |
|---|---|---|---|
| `scp` `patch_rt.mpq` (1,09 MB) | 1.086.853 B | 10,5 s | dominato dall'handshake (~8 s) |
| `scp` `StarDat.mpq` (66,2 MB) | 66.184.568 B | 286,4 s | **0,23 MB/s** |
| HTTP loopback (`curl` su 10.0.2.2) | 6,36 MB in 25 s | — | **0,25 MB/s** |
| HTTP, 4 flussi in parallelo | 4 × ~60–70 KB/s | — | **~0,26 MB/s aggregati** |

Due conclusioni **misurate**, non ipotizzate:

1. **La crittografia di SSH non è il collo di bottiglia**: HTTP in chiaro va uguale (0,25 MB/s).
   Cambiare cifrario (`aes128-ctr`, `aes256-gcm`) non cambia nulla (test su 1 MB: 8,2 / 8,4 / 8,5 s).
2. **Il limite è condiviso**: 4 flussi paralleli danno lo stesso totale di uno solo, quindi è il
   percorso emulato (`slirp` + NIC `usb-net` su dwc_otg), non la latenza per connessione. Non c'è
   modo di “aprire più corsie” per andare più veloce.

Conseguenza operativa: **579 MB di `Starcraft.mpq` = ~30–45 minuti** a questo rate. Su una VM che
viene uccisa dall'esterno questo rende la **ripresa** obbligatoria, non opzionale:

```bash
# host: servente HTTP loopback con Range (nuovo: tools/qemu/qa-http-range.py)
./tools/qemu/qa-http-range.py ~/dev/final-starcraft/Starcraft 8898 127.0.0.1 &
# guest: ripresa automatica dal byte già presente
cd /home/pi/sc && curl -C - -o Starcraft.mpq http://10.0.2.2:8898/Starcraft.mpq
```

> `python3 -m http.server` **non** serve a questo scopo: ignora `Range` e risponde `200` con il file
> intero (misurato: `code=200`, nessuna ripresa possibile). Da qui `qa-http-range.py`.
> **Ripresa validata end-to-end** su un asset reale: `curl -m 2` tronca a 498.752 B, `curl -C -`
> completa a 1.086.853 B e lo `sha256` finale è identico a quello dell'host
> (`f692f7e4…501ca`). Nessuna esposizione di rete: bind su `127.0.0.1`, raggiunta dal guest come
> `10.0.2.2`.

Manifest QA: per ogni artefatto `sha256  byte  nome`; per il binario anche `stat -c %s,%y` e
versione QEMU/kernel. 

> **Identità del binario — limite noto.** Nel port ARM il build-guard **non c'è**: `llvm-nm` mostra
> solo lo stub `T build_guard_check` (in `runtime/arm/game_glue.c`, “controllo non configurato,
> accettato”), e `strings` non contiene `sc_build_git`/`sc_build_time`. A differenza del binario
> i386, questo **non sa dichiararsi obsoleto** (niente riga `[build] git=…`, niente exit 3).
> L'identità va quindi garantita **fuori** dal binario: sha256 assegnato dal supervisore + manifest
> QA + copia immutata in `/home/pi/sc/` (nessuna ricompilazione tra copia e misura).

### 6.2 Launcher nel guest (bozza, non eseguita)

```sh
# scoperta del cookie X (il percorso cambia a ogni avvio di X)
XAUTH=$(tr '\0' '\n' < /proc/$(pgrep -x Xorg)/cmdline | sed -n 's/^-auth//p')
cd /home/pi/sc
DISPLAY=:0 XAUTHORITY=$XAUTH SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
SC_FPS_LOG=1 nohup ./starcraft-armv6 > game.log 2>&1 &
```

- Fullscreen: il gioco chiede 640×480; sullo schermo 800×480 conviene lasciare la richiesta del
  gioco (`SC_FULLSCREEN_MODE` default = `desktop`, letterbox 4:3 gestito da SDL) invece di forzare
  la finestra (`SC_FORCE_WINDOWED`), perché 480 px + decorazioni non ci stanno.
- Nessun input simulato: avvio via SSH, evidenza via `screendump` QMP.
- **Nessuna variabile “comoda” attivata in silenzio**: ogni `SC_*`/`SDL_*` usata va nel manifest
  della misura.

### 6.3 Strategia di performance (definita, da eseguire in fase 2)

| metrica | come | note |
|---|---|---|
| **wall time fino al menu** | `date +%s` al lancio del processo → prima riga `[fps]` nel `game.log` (e primo frame non nero in uno `screendump` QMP) | due sorgenti indipendenti, stessa unità |
| **FPS presentati** | `SC_FPS_LOG=1` → righe `[fps] N fps, present medio X ms, viewport WxH, finestra WxH` (una al secondo) | è la metrica di *present*, non del sim |
| **update sim distinti** | **non strumentati** nel binario ARM: nessun contatore di tick | proposte: (a) replay deterministico `SC_INPUT_REPLAY` e tempo per completare N azioni note; (b) contatore nel runtime (fuori scope QA). Da decidere col supervisore |
| **CPU / RSS del gioco** | sampler nel guest da `/proc/<pid>/{stat,status}` (`utime+stime`, `VmRSS`, `VmHWM`) a 1 Hz | script QA, nessun pacchetto extra |
| **p95** | p95 sulle medie al secondo di `present medio` (60 campioni) | **non** è il p95 per-frame: dichiararlo. Per-frame servirebbe instrumentazione |
| **bound host** | `%CPU`/RSS di `qemu-system-arm` (pid dal file) come sanity check | non è il costo del gioco |
| **durata campioni** | 30–60 s a regime (menu), 30–60 s in partita | + 10 s di warm-up scartati |
| **workload/versioni/env** | sha256 binario, QEMU `9.2.2`, kernel `6.18.50+rpt-rpi-v6`, `/proc/cpuinfo`, lista `SC_*`/`SDL_*`, carico host al momento | obbligatorio nel report di misura |

**Avvertenza da ripetere in ogni report di misura:** QEMU TCG **non ha una frequenza**; i BogoMIPS
sono un artefatto e **non** si può assumere “Pi reale = QEMU”. Riferimenti già misurati in
`work/qemu/PROFILING-BOOT.md` (SHA256 23,7 MB/s; fork+exec 20,6 ms; multi-user 141 s vs 142–154 s
su Pi Zero vero → emulatore ~1,4×). Nessun FPS va inferito dal client VNC o da uno screenshot.

---

## 7. Accesso alla partita senza input simulato — punto aperto

Il brief vieta l'iniezione di input (host e guest) senza autorizzazione esplicita caso per caso.
Nel runtime esistono già questi appigli (letti in `host_elf.c`, non modificati):

- `SC_INPUT_REPLAY=<file>` — riproduce una sessione **registrata** (formato `t_ms tipo x y [btn]`,
  20.000 eventi max), con `SC_REPLAY_AFTER_MS` per far partire la coda dopo uno scenario;
- `SC_INPUT_SCRIPT=mp|mp-select|mp-create` — scenari scriptati che però vanno sul **percorso
  multiplayer/network**, non sul single-player;
- `SC_DLG_INPUT_DEMO` — demo dei dialoghi.

**Non esiste** un percorso deterministico già pronto che arrivi a una **partita single-player**
(menu → Single Player → … → Start). Le opzioni, in ordine di costo:

1. **(consigliata)** una registrazione reale fatta **una volta dalla persona** su un build che
   arriva al menu, salvata come `SC_INPUT_REPLAY`: da lì in poi la partita è riproducibile e la
   misura ripetibile;
2. un passo manuale della persona (clic) con la QA che misura solo dopo;
3. un nuovo scenario in `SC_INPUT_SCRIPT` → è codice del runtime: **non QA**, va al collega.

Nessuna di queste è stata eseguita: la scelta spetta al supervisore.

---

## 8. Blocchi che il supervisore deve risolvere

### 8.1 (bloccante, confermato sul campo) Killer esterno sulle VM QEMU — §2.2 e §2.4

`~/projects/esp95/build-lean/run-gate.sh` uccide ogni processo con `comm ~ /^qemu-system/`:
**4 uccisioni in 20 minuti** di questa VM, di cui **2 consecutive** in fase 2a (a metà `dpkg` e 4 s
dopo un rilancio), dalla **stessa** esecuzione del loro gate. Non è un disturbo raro, è la norma.

Effetti misurati: (a) lavoro perso a metà transazione (`dpkg` interrotto, da riparare);
(b) **carico host 9,9–13,6** che rende il guest inservibile via SSH e **invalida ogni misura di
performance**. Servono, in capo a chi possiede `~/projects/esp95` (QA **non** tocca quel progetto né
i suoi processi):

- filtro per identificazione della VM: `-name` (`Raspberry Pi Zero`), `cwd` del pid, o pid-file; oppure
- finestra concordata in cui la QA usa il rig, con il loro gate fermo.

Finché non è risolto: lotti brevi con rilancio, e **nessun numero di performance è attendibile**
mentre il gate gira.

### 8.2 (fatto in fase 2a, da completare alla ripresa) `sudo` nel guest per SDL2

Autorizzato e usato: `apt-get clean` **completato**, `apt-get install libsdl2-2.0-0` **avviato**
(11 `.deb` scaricati) e interrotto dall'uccisione della VM: la transazione `dpkg` va completata alla
ripresa (§3, primo comando). `sudo` nel guest funziona con la password già nota al team (non
riportata qui).

### 8.3 (decisione) Accesso alla partita — §7

Serve la scelta tra replay registrato dalla persona / passo manuale / scenario nuovo nel runtime.

### 8.4 (risolto) Spazio

`apt-get clean` ha liberato 181 MiB: **904 MiB liberi** misurati. Il set minimo (643 MiB di MPQ +
8 MB di binario) ci sta con ~254 MiB di margine. Il piano B NBD non serve; resta documentato al §5.2
se un domani servisse Brood War.

---

## 9. Piano eseguibile della fase 2 (con lo **stato di avanzamento** reale)

| passo | stato |
|---|---|
| 1. Preflight (VM, SSH, `df`, `minimal-x`, manifest) | **fatto** |
| 2. Igiene spazio: `apt-get clean` | **fatto** (181 MiB liberati, 904 MiB liberi) |
| 3. Dipendenze: `apt-get install libsdl2-2.0-0` | **fatto** (fase 2b): configurato, `dpkg --audit` vuoto, `.so` presente, `ldd` 0 mancanti |
| 4. Trasferimento verificato MPQ + binario | **asset fatti** (3 MPQ, sha256 OK, 163 MiB liberi); **binario: manca il candidato con hash** |
| 5. Smoke (avvio, screendump, stop su crash) | **non iniziato** (e comunque vietato finché non c'è il candidato) |
| 6. Misura menu (wall time, `[fps]`, CPU/RSS) | **non iniziato** |
| 7. Misura partita (passo manuale o replay verificato) | **non iniziato**; percorso da approvare (§8.3) |
| 8. Report di misura | — |

Dettaglio dei passi residui (invariato rispetto alla fase 1):

1. **Preflight** (2 min): verificare QEMU pid vivo + SSH; `df -B1 /`; confermare `minimal-x` attivo;
   scrivere il manifest (`sha256` candidato, QEMU, kernel, `cpuinfo`, carico).
2. **Igiene spazio** (decisione §8.4): `apt-get clean` (+ eventuale rimozione `lists` se offline).
3. **Dipendenze**: `apt-get install -y libsdl2-2.0-0` (11 pacchetti) → `ldd` sul binario per
   confermare che tutti i `NEEDED` risolvono (senza eseguirlo).
4. **Trasferimento verificato**: `scp` dei 3 MPQ + binario; `sha256sum -c` nel guest; misura del
   throughput su `StarDat.mpq`; `df` dopo (margine atteso ~250 MiB).
5. **Smoke**: avvio con `SC_FPS_LOG=1`, `SDL_VIDEODRIVER=x11`, `SDL_AUDIODRIVER=dummy`;
   `screendump` QMP ogni 10 s; stop se compare `SIGILL`/`SIGSEGV`/“API NON IMPLEMENTATA” (nel qual
   caso: log, immagine, **nessuna** conclusione di gameplay).
6. **Misura menu**: wall time fino a primo `[fps]`; 60 s a regime con sampler CPU/RSS.
7. **Misura partita**: solo con il percorso approvato al §8.3; 30–60 s, stessi campioni; A/B
   `SC_PRESENT=gpu|cpu` solo se il primo run è stabile.
8. **Report di misura** con: hash, env, numeri, p95 dichiarato come approssimazione, distinzione
   esplicita fra “menu” e “partita”, e la frase “nessun FPS dedotto da VNC”.

**Criterio di accettazione già concordato:** menu con evidenza **e** partita con evidenza, entrambe
con perf ripetibile; il menu da solo **non** è gameplay.

---

## 10. Strumenti aggiunti / da aggiungere in `tools/qemu/`

| file | stato | scopo |
|---|---|---|
| `tools/qemu/qa-shot.py` | **nuovo, provato** | `screendump` QMP con path **assoluto** e conversione PNG, singolo o serie — evita il doppio inganno di `qmp-key.py shot` (path relativo risolto nella cwd di QEMU; `unlink` del file quando gli si passa `.ppm`). Nessun evento QMP oltre `screendump`. |
| `tools/qemu/qa-http-range.py` | **nuovo, provato** | servente HTTP su loopback con **Range** (206), per la ripresa dei trasferimenti lunghi: `curl -C -`. Necessario perché `python3 -m http.server` ignora `Range` (risponde 200 col file intero). Ripresa validata su un asset reale con hash finale identico. |
| `build/qemu/ssh/gscp` | **nuovo, provato** | `scp` verso il guest con la stessa auth di `gssh` (askpass, 2222, timeout brevi). |
| `tools/qemu/qa-perf.sh` | **da scrivere quando c'è il candidato** | sampler guest (CPU/RSS da `/proc`, p95 sulle medie `present medio`) |
| `tools/qemu/qa-deploy.sh` | **non più necessario così** | il trasferimento è gestito da `gscp` + `qa-http-range.py`; un wrapper unico serve solo se il deploy diventa ripetitivo |

Deliberatamente **non** ho scritto ora lo strumento di misura delle performance: senza un candidato con
hash e con la VM uccidibile dall'esterno, sarebbe codice non verificabile (regola del progetto: non
consegnare strumenti mai eseguiti come se fossero provati).

Nota misurata: **`qmp-key.py shot` con path relativo fallisce** (`screendump` scrive nella cwd di
QEMU, non in quella della shell) e con estensione `.ppm` **cancella l'output**.

---

## 11. Decisioni autonome

1. **Ho rilanciato la VM** (pid 361253) dopo che era stata uccisa dall'esterno: ripristino dello
   stato di CHECKPOINT, stesso overlay, stessa rete, stesso kernel — nessuna modifica a `lite.img`.
   Motivazione: la fase 1 richiede lo stato del guest e la VM era già morta (non “interrotta da me”).
2. **Non ho installato SDL2, non ho copiato asset, non ho fatto `apt clean`**: alterazioni dell'overlay
   rinviate alla fase 2, dove sono dichiarate e giustificate.
3. **Non ho rilanciato il binario del gioco** (né quello vecchio né altri), come richiesto.
4. **Non ho aperto nuove finestre sull'host** e non ho inviato input a nessun livello; l'unica
   evidenza visiva è `screendump` QMP.
5. Piano B (NBD) e allargamento disco restano **non** eseguiti: sono decisioni del supervisore.

## 12. Cosa NON è verificato (onestà)

- Che `libSDL2-2.0.so.0` 2.32.4 del guest basti davvero al runtime: i **nomi** dei `NEEDED`
  risolvono e `ldd` sulla libreria non segnala mancanti, ma i **simboli** usati dagli shim
  (`SDL_CreateRenderer`, `SDL_UpdateTexture`, `SDL_OpenAudioDevice`, …) potranno essere verificati
  solo eseguendo il binario — che non è stato fatto.
- Perché l'installazione SDL2 abbia occupato ~99 MB con 4,4 MB di `.deb`: ordine di grandezza
  plausibile (11 pacchetti scompattati) ma **non** verificato pacchetto per pacchetto.
- Che il percorso `SC_PRESENT=gpu` funzioni su llvmpipe/ARM1176 emulato (probabile lento).
- Qualsiasi comportamento del gioco nel guest: **mai eseguito**. La causa del SIGILL resta
  dell'area runtime; questa QA non la tocca.
- Che 163 MiB residui bastino alla partita: il gioco scrive save/personaggi (piccoli), ma il numero
  non è stato provato a gioco avviato.
- **Da riconciliare, non usato qui:** in memoria condivisa esiste un record di un'altra sessione
  (`78bf4bb9-6dec-43cd-a1a1-f44b0e6f7070`, project `sc-reasm`) che attribuisce il SIGILL non a una
  conversione CPSR/Thumb ma a `tcall ecx` che diventa `bx` su un VA dispari (`0x4060C1`, primo
  inizializzatore CRT mancante dalla `_initterm` a `0x50C4C0`). Riguarda un binario diverso
  (`starcraft-armv6-data`) e **non** è stato verificato dalla QA: la §3 di CHECKPOINT resta
  invariata, la riconciliazione spetta a chi possiede il runtime.

---

## 13. Log delle operazioni di fase 2a (per la ripresa)

| ora | azione | esito |
|---|---|---|
| 15:29:48 | preflight SSH/`df`/`sudo` | OK — 724 MiB liberi |
| 15:30 | `apt-get clean` | OK — 181 MiB liberati |
| 15:33:16 | `df` post-clean | **948.215.808 B (904 MiB) liberi** |
| 15:30–15:38 | `apt-get install -y libsdl2-2.0-0` | 11 `.deb` scaricati (4,4 MB), `dpkg --unpack` in corso |
| **15:38:46** | **VM uccisa** (pid 478340, esp95 gate) | `fase2-launch-kill1.log` |
| 15:40:20 | rilancio QA (pid 489002) | boot avviato |
| **15:40:24** | **VM uccisa** di nuovo (stesso pid 478340), boot a 2,8 s | `fase2-launch-kill2.log`, `fase2-serial-kill2.log` |
| 15:43:52 | **STOP** come da istruzione (due uccisioni durante le operazioni) | nessun trasferimento eseguito |

**Stato del rig allo stop:** VM **non in esecuzione**; overlay `guest-lite.qcow2` con `dpkg` interrotto
(coerente al riavvio, ext4 fa journal recovery); `lite.img`, kernel e DTB **intatti** (mtime 10:42:42
/ 10:37:34); MPQ originali **intatti** (mtime 2019, sola lettura); nessun processo esp95 toccato.

**Sequenza di ripresa (fase 2b), in ordine:**

```bash
# 1. rilancio (stesso comando di §2.1) e attesa SSH con timeout 30-40 s
# 2. riparazione transazione interrotta
build/qemu/ssh/gssh pi@127.0.0.1 'echo raspberry | sudo -S dpkg --configure -a; \
  echo raspberry | sudo -S apt-get -f install; dpkg --audit; \
  dpkg -l libsdl2-2.0-0 | tail -1; ls -l /usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0*'
# 3. solo se il .so manca ancora, riprendere l'installazione (il resto è già in cache)
# 4. trasferimento verificato: patch_rt.mpq -> StarDat.mpq -> Starcraft.mpq,
#    ognuno con sha256 in ingresso e in uscita; misura del tempo per file
# 5. df finale (atteso ~254 MiB liberi) e manifest QA
```

Ogni passo va tenuto **sotto i 5 minuti** di durata e interrotto se compare una nuova uccisione:
al secondo kill consecutivo ci si ferma e si scrive qui, come in questa fase.

---

## 14. Fase 2b (ripresa del 2026-11-20 15:48–16:45)

**Verifica di quiescenza (solo osservazione, identità esatta dei processi):**

| ora | `run-gate.sh` | `qemu-system*` | sessione esp95 | load host |
|---|---|---|---|---|
| 15:46:49 | assente | **nessuno** | pid 506690 `pi`, cwd `/home/enne2/projects/esp95`, `ep_poll` (idle) | 9,4 |
| 15:48:07 | assente | **nessuno** | idem, vivo | 7,4 |

Il carico 7–10 **non** era di esp95: veniva da `~/dev/libday-cpp` (test `ld_tests`, 4 istanze al
97–100 % CPU) e da Firefox. Nessun processo di `~/projects/esp95` è stato toccato.

**Operazioni eseguite:**

| ora | azione | esito |
|---|---|---|
| 15:48:11 | rilancio VM (pid **542755**, una sola istanza) | boot, SSH a **+60 s** |
| 15:51 | `dpkg --configure -a` + `dpkg --audit` | transazione chiusa, audit **vuoto** |
| 15:51 | `libsdl2-2.0-0` 2.32.4 + `.so` 1.677.260 B | **installata**; libero 845.242.368 B |
| 15:52 | `ldd libSDL2` → 0 “not found”; `libgcc_s.so.1` presente | i 4 `NEEDED` del binario risolvono |
| 15:52 | `scp` `patch_rt.mpq` (1,09 MB) | 10,5 s, sha256 **uguale** all'host |
| 15:56 | `scp` `StarDat.mpq` (66,2 MB) | 286,4 s (**0,23 MB/s**), sha256 **uguale** |
| 15:57–16:00 | prove di canale: HTTP loopback (0,25 MB/s), 4 flussi paralleli (~0,26 MB/s totali), cifrari diversi (nessuna differenza) | il limite è il percorso emulato, non il crypto |
| 16:00 | `qa-http-range.py` + `curl -C -`, ripresa validata su `patch_rt` (498.752 → 1.086.853 B, sha256 ok) | meccanismo di ripresa **funzionante** |
| 16:00:43 | avvio `Starcraft.mpq` (579 MB) con ripresa | in corso (§14.1) |

**Nessuna uccisione durante la fase 2b** (fino all'ultima verifica); nessun binario del gioco eseguito;
`lite.img`/kernel/DTB invariati; originali MPQ intatti.

### 14.1 Trasferimento di `Starcraft.mpq` — avanzamento misurato

| ora | byte nel guest | note |
|---|---|---|
| 16:00:43 | 0 | avvio |
| 16:02:37 | 40.988.672 | ~0,36 MB/s |
| 16:07:53 | 116.117.504 | ~0,27 MB/s medi, load guest 1,71 |
| 16:12:10 | 177.811.456 | ~0,24 MB/s nell'ultimo intervallo |
| 16:40:23 | 587.038.720 | fasi di stallo + ripresa; `ssh` di autenticazione in timeout quando il guest è saturo |
| **16:42:30** | **606.857.006** | **completo** (dimensione identica all'host), `curl` terminato |
| 16:45:51 | — | `sha256sum -c MANIFEST-assets.sha256` → **patch_rt.mpq: OK, StarDat.mpq: OK, Starcraft.mpq: OK** |

Tempo totale: **~42 minuti** (16:00:43 → 16:42:30) per 579 MB, cioè **~0,23 MB/s medi**,
coerente con le misure di canale. Il file è stato ripreso più volte senza perdere byte.

**Stato finale del guest (verificato 15:45 locali del guest):**

```
/home/pi/sc/  patch_rt.mpq 1.086.853 | StarDat.mpq 66.184.568 | Starcraft.mpq 606.857.006 | MANIFEST-assets.sha256
spazio libero su /           171.085.824 B (163 MiB)   [uso 96%]
libsdl2-2.0-0:armhf 2.32.4+dfsg-1   installata (dpkg --audit vuoto)
load guest 1,08 (1 min) · RAM 128/427 MB usati, 298 disponibili
```

Pulizia fatta: rimossi `probe.mpq` (prova cifrari) e `curl.log`; **server HTTP spenti** (porte 8898 e
8899 chiuse, pid verificati). Il margine di 163 MiB è quello previsto dal §5.1 (l'installazione SDL2
è costata ~99 MB in più del previsto: il numero è misurato, non stimato).

**Nota metodologica utile per le misure future:** con il guest saturo (trasferimento in corso) l'SSH
si blocca in **autenticazione** — il banner `SSH-2.0-OpenSSH_10.0p2` arriva subito, poi il client
va in timeout a 30–110 s. Per seguire un lavoro lungo senza aggiungere carico conviene leggere
(i) la crescita del qcow2 dell'overlay sull'host e (ii) `query-blockstats` QMP, non aprire SSH.
