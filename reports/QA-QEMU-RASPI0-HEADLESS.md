# Pi Zero emulato: ambiente headless verificato (QEMU raspi0, Raspberry Pi OS Lite)

Data: 2026-11-27. Macchina host: `frigate`. Obiettivo: emulare un **Raspberry Pi Zero** con Raspberry Pi
OS Lite **senza server grafico**, verificare il boot in modalità **headless/console-only** e chiarire
cosa cambia rispetto all'hardware reale, in particolare per **KMS/DRM e framebuffer**.

## 0. Emulazione o hardware reale?

Sono due cose diverse, e vanno tenute separate:

| | **QEMU `-M raspi0`** (questo documento) | **Pi Zero reale** |
|---|---|---|
| CPU | ARM1176JZF-S emulato (TCG, non cycle-accurate) | ARM1176JZF-S 1 GHz, VFPv2, **no NEON** |
| VideoCore IV (GPU) | **non emulato** | presente |
| `bcm2708_fb` (framebuffer legacy) | **presente**: `/dev/fb0` 800×480, 32 bpp | presente **solo** se non si usa KMS |
| `vc4` / KMS / `/dev/dri` | **assente** (nessun overlay firmware, DT `bcm2708-rpi-zero.dtb`) | presente con `dtoverlay=vc4-kms-v3d` (default sulle release attuali) |
| Audio (PWM/I2S/HDMI) | **non emulato** (PWM mancante in QEMU) | dipende dal driver scelto |
| Prestazioni | **non rappresentative** (TCG) | l'unica misura valida |

Conseguenza pratica: **in QEMU puoi verificare il boot, la console, il filesystem, la rete e il
percorso `SC_PRESENT=fb` (framebuffer legacy)**; **non puoi verificare KMS/DRM, GLES su VideoCore, né
le prestazioni reali**. Quelli restano prove da fare sul Pi Zero.

## 1. Cosa serve (installato)

- `qemu-system-arm` **10.2.2** (installato con `pkexec dnf install -y qemu-system-arm`; la password si
  inserisce nella finestra polkit di KDE, non sulla riga di comando). `qemu-img` già presente.
- Immagine **Raspberry Pi OS Bookworm armhf** (`build/qemu/2026-04-13-raspios-bookworm-armhf.img`) e la
  variante leggera (`build/qemu/lite.img`), con kernel e DTB estratti:
  `build/qemu/kernel.img`, `build/qemu/bcm2708-rpi-zero.dtb`.
- Disco di lavoro: overlay qcow2 (`build/qemu/runtime/guest-lite.rc.qcow2`, backing
  `guest-lite.raw`) — così l'immagine originale non viene toccata.
- Script già in casa: `tools/qemu/run.sh` (avvio), `tools/qemu/qa-shot.py` (screendump via QMP),
  `tools/qemu/guest-sh.py` e `serial-cli.py` (comandi sulla console seriale).

## 2. Comando di avvio headless (verificato)

```sh
cd /home/enne2/dev/starcraft-reasm-generic
QEMU=/usr/bin/qemu-system-arm QEMU_DISPLAY=none \
IMAGE=$PWD/build/qemu/lite.img \
DISK=$PWD/build/qemu/runtime/guest-lite.rc.qcow2 \
KERNEL=$PWD/build/qemu/kernel.img \
DTB=$PWD/build/qemu/bcm2708-rpi-zero.dtb \
setsid nohup bash tools/qemu/run.sh >/tmp/qemu-headless.out 2>&1 < /dev/null &
```

Note:

- `QEMU_DISPLAY=none` evita **qualsiasi** finestra sull'host: il guest non ha X e l'host non apre un
  display. Lo script imposta `-display none`, seriale su socket con logfile
  (`build/qemu/runtime/serial.log`), QMP su socket.
- Senza `QEMU=...` lo script cerca un QEMU compilato a mano (`build/qemu-arm-build/…`): con il QEMU di
  sistema va passato esplicitamente.
- Il kernel non viene caricato dal firmware (assente in QEMU): si passa `-kernel`/`-dtb` e il disco come
  `if=sd`. È il motivo per cui l'immagine **non** si avvia "mettendo la SD": serve questa riga di
  comando.

## 2b. Variante grafica (finestra sull'host, host in Wayland)

Stesso ambiente, con `QEMU_DISPLAY=gtk` invece di `none`: la finestra 800×480 viene aperta sul desktop
dell'host (sessione KDE/Wayland), mentre **seriale e QMP restano su socket** — quindi l'automazione non
cambia. Serve passare l'ambiente grafico della sessione utente, perché il processo non l'eredita:

```sh
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:0 \
       XAUTHORITY=/run/user/1000/xauth_XXXXXX
QEMU=/usr/bin/qemu-system-arm QEMU_DISPLAY=gtk \
IMAGE=$PWD/build/qemu/lite.img DISK=$PWD/build/qemu/runtime/guest-lite.rc.qcow2 \
KERNEL=$PWD/build/qemu/kernel.img DTB=$PWD/build/qemu/bcm2708-rpi-zero.dtb \
setsid nohup bash tools/qemu/run.sh >/tmp/qemu-gtk.out 2>&1 < /dev/null &
```

Backend disponibili in questo QEMU: `none gtk sdl egl-headless curses` (`qemu-system-arm -display help`).
`gtk` funziona anche su Wayland nativo; `sdl` è l'alternativa.

Verificato con `QEMU_DISPLAY=gtk`: processo vivo con `-display gtk`, log seriale con default target
`multi-user.target` e prompt `raspberrypi login:` raggiunto dopo ~160 s di wall clock (TCG: il tempo del
guest scorre più lento).

Nota operativa: **fermare la VM per PID**, non con `pkill -f "<pattern>"` quando il pattern compare nella
propria riga di comando (`pkill` uccide anche la shell che lo esegue — già successo):

```sh
for p in $(pgrep -x qemu-system-arm); do kill $p; done
```

## 3. Verifica del boot (esito reale)

Dentro `build/qemu/runtime/serial.log`:

```
[   30.173017] systemd[1]: Queued start job for default target multi-user.target.
[  OK  ] Reached target multi-user.target - Multi-User System.
raspberrypi login:
[    2.690931] bcm2708_fb soc:fb: FB found 1 display(s)
[    2.714650] Console: switching to colour frame buffer device 100x30
[    2.723607] bcm2708_fb soc:fb: Registered framebuffer for display 0, size 800x480
```

- **boot console-only riuscito**: il *default target* è `multi-user.target`
  (`systemd[1]: Queued start job for default target multi-user.target`), si arriva a
  `Reached target multi-user.target - Multi-User System` e al prompt `raspberrypi login:`;
  nel log non compare né `graphical.target` né `lightdm`/X11.
  Nota pratica: il log della console seriale contiene sequenze ANSI (colori) fra le parole, quindi le
  ricerche vanno fatte sul token (`grep -a "multi-user.target"`), non sulla frase intera.
- **framebuffer presente** anche senza `-display` sull'host: `bcm2708_fb` registra `800x480`, e la console
  passa in modalità grafica a 100×30 caratteri. Prova indipendente: `tools/qemu/qa-shot.py` produce uno
  screendump via QMP (`build/qa/qemu-headless-shot.png`, 26 KB) — il framebuffer è vivo anche se sul
  desktop non c'è nessuna finestra.
- **KMS/DRM assente**: `modprobe drm` si attiva e si disattiva senza istanziare nulla; `vc4` non compare
  e `/dev/dri` non esiste (confermato anche dal report precedente `REPORT-armv6-native-qemu.md` §2).

## 4. Implicazioni per KMS e framebuffer (leggere prima di misurare)

1. **In QEMU il percorso testabile è `SC_PRESENT=fb`** (framebuffer legacy `bcm2708_fb`): è il nostro
   `fb_open()` su `/dev/fb0` + `FBIOGET_*` + `mmap`, 16/32 bpp. Questo è esattamente ciò che si trova su
   un Pi Zero reale **quando KMS non è attivo**.
2. **KMS/DRM non è testabile in QEMU**: sulle release attuali il Pi reale usa `dtoverlay=vc4-kms-v3d`,
   che sostituisce il framebuffer del firmware; `/dev/fb0` può non esistere e `hdmi_force_hotplug` non è
   implementato (si usa `video=HDMI-A-1:<modo>@60D` in `cmdline.txt`). Se si vuole quella
   configurazione servono **o** il framebuffer legacy (commentare l'overlay) **o** un presenter DRM
   (dumb buffer + `drmModeSetCrtc`) che oggi non abbiamo.
3. Quindi: le misure di resa fatte in QEMU (logo 43,4 present/s, menu 13,9 present/s, present ~1 ms)
   **valgono come indicazione funzionale**, non come prestazioni del Pi Zero; e non dicono nulla su
   come si comporterà il percorso KMS.

## 5. Console seriale e Ctrl+S (XOFF)

- Lo script espone la console su un **socket unix con logfile**, non su un pty: scrivendo lì non c'è
  controllo di flusso del terminale, quindi `Ctrl+S` non blocca nulla. È il modo più sicuro per
  automatizzare (è quello che usano `guest-sh.py`/`serial-cli.py`).
- Se invece ti colleghi a mano con un pty (`socat ... -,raw,echo=0`, `screen`, `minicom`, o la console
  di QEMU su stdio), vale la regola già scritta nella checklist: `Ctrl+S` è il byte `0x13` (XOFF) e
  **sospende l'output**, non il processo. Mitigazione: `stty -ixon` (o `stty stop undef`) sulla sessione,
  recupero con `Ctrl+Q`, e `reset`/`stty sane` se lo schermo resta sporco. Per `socat` usare
  `raw,echo=0`; per `screen`/`minicom` disattivare il flow control nelle loro opzioni.
- Regola per i nostri test: **non inviare mai `Ctrl+S` come tasto di gioco** via `sc_ctl`, harness o
  script di QA.

## 6. Cosa puoi verificare adesso in questo guest, e cosa no

| Verifica | In QEMU raspi0 | Note |
|---|---|---|
| boot console-only, login su seriale | **sì** (fatto) | `multi-user.target` raggiunto |
| filesystem, `apt`, rete (con `SSH_FWD=1`/`GUEST_NET=1`) | sì | serve `SSH_FWD=1` o `NET_MODE=slirp` |
| `/dev/fb0`, `fbset`, screendump QMP | **sì** | `bcm2708_fb`, 800×480 32bpp |
| `SC_PRESENT=fb` del nostro port | **sì** | già misurato: logo 43,4/s, menu 13,9/s |
| `/dev/dri`, KMS, `vc4`, GLES | **no** | GPU non emulata |
| audio (PWM/I2S/HDMI) | **no** | PWM non emulato |
| prestazioni reali del Pi Zero | **no** | TCG non è un modello di prestazioni |

## 7. Comandi utili

```sh
# console seriale (dopo il boot): eseguire un comando nel guest
python3 tools/qemu/guest-sh.py "cat /proc/fb; ls -l /dev/fb0 /dev/dri; systemctl get-default"

# screenshot del framebuffer emulato, senza aprire finestre
python3 tools/qemu/qa-shot.py build/qa/shot.png

# fermare la VM
pkill -f "qemu-system-arm -machine raspi0"

# rete nel guest (facoltativa): aggiungere prima dell'avvio
SSH_FWD=1 SSH_PORT=2222 ... tools/qemu/run.sh   # poi ssh -p 2222 sull'host
```

Fonti: documentazione QEMU per `raspi0`/`raspi1ap` (dispositivi emulati e mancanti, incluso PWM) —
https://www.qemu.org/docs/master/system/arm/raspi.html ; configurazione Raspberry Pi (`config.txt`,
KMS vs legacy, `video=`) — https://www.raspberrypi.com/documentation/computers/config_txt.html ;
`stty`/`ixon` — https://pubs.opengroup.org/onlinepubs/9699919799/utilities/stty.html .
