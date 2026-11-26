# Checklist — bring-up Raspberry Pi Zero con Linux + framebuffer

Scopo: prima prova **sul Pi Zero originale ARMv6**, senza desktop. Nessun risultato di prestazioni su hardware è disponibile in questo task; dove manca, il criterio richiede misura. Il test QEMU verificato non sostituisce il bring-up reale.

## 1. Sistema operativo e console

- [ ] Installare **Raspberry Pi OS Lite 32-bit (armhf)** su microSD. È l’edizione CLI ufficialmente indicata anche per modelli Zero/older Pi; verificare modello/arch dopo il boot:
  ```sh
  cat /etc/os-release
  dpkg --print-architecture
  uname -m
  grep -E '^(model name|Features|CPU architecture)' /proc/cpuinfo
  ```
  Successo: `armhf`, kernel ARM 32-bit e CPU ARM1176/ARMv6 con VFP; **non Zero 2 W**. Riferimenti OS: https://www.raspberrypi.com/documentation/computers/os.html e hardware: https://www.raspberrypi.com/documentation/computers/zero.html.
- [ ] Rimanere su console `multi-user.target`, niente display manager:
  ```sh
  systemctl get-default
  systemctl is-active display-manager || true
  tty
  ```
  Successo: target multi-user, nessun display manager attivo, console locale `tty1` o seriale. Raspberry Pi OS Lite non include desktop. Non disabilitare servizi a caso: prima verificare che nessun servizio usi il framebuffer.
- [ ] Controllare esistenza e formato framebuffer:
  ```sh
  cat /proc/fb
  ls -l /dev/fb0
  fbset -fb /dev/fb0 -i
  cat /sys/class/graphics/fb0/bits_per_pixel
  ```
  Successo: `/dev/fb0` presente e apribile; risoluzione/stride coerenti con `fbset`; eseguibile può aprire il device. La presenza/driver effettivi dipendono dall’immagine/kernel: **se manca `/dev/fb0`, checklist bloccata per questo presenter**; verificare stack display/driver, non assumere che KMS esponga il framebuffer richiesto.

## 2. Build, sysroot e deployment

- [ ] **Raccomandazione: build incrociata da `frigate`, deploy del binario armhf**; il Pi Zero ha un solo core ARM1176 e 512 MB. Non è stato cronometrato il build di questo progetto sul Pi. Un benchmark indipendente su vecchio Pi ARM1176 ha riportato circa 99–107 minuti per una compilebench ridotta, non equivalente al progetto; non usarlo come previsione. Fonte non ufficiale, solo indicativa: https://dench.flatlib.jp/linux:compilebench. Non affermare “build sul Pi impossibile” né “accettabile” senza test.
- [ ] Verificare prerequisiti/runtime sul Pi:
  ```sh
  dpkg --print-architecture
  dpkg-query -W libc6 libstdc++6 libsdl2-2.0-0 libasound2 2>/dev/null
  file ./starcraft-armv6 ./libsmacker-armv6.so
  readelf -h ./starcraft-armv6 | grep -E 'Class:|Machine:|Type:'
  readelf -A ./starcraft-armv6 | grep -E 'Tag_CPU_arch|Tag_FP_arch'
  ```
  Successo: ELF32 ARM, ARMv6/ARM1176 + VFP (no NEON), hard-float ABI coerente col sistema; dipendenze runtime risolte (`ldd`). Se il binario è statico o `ldd` non lo riconosce, verificare `readelf -d`/link map.
- [ ] Per build incrociata servono **sysroot 32-bit della stessa release/arch del Pi**, header SDL2 e librerie di sviluppo corrispondenti (almeno glibc, SDL2; ALSA se necessaria al linker), clang/LLVM con backend ARM e linker. L’output deve essere armhf ARMv6 (`-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard`, no NEON). I nomi pacchetto di sviluppo vanno verificati nella release scelta; non mescolare sysroot Bookworm e target Trixie senza compatibilità verificata.
- [ ] Nota build del repo: `tools/build-runtime-arm.sh` è la pipeline ARMv6 (`tools/build-runtime-arm.sh:72-74`), ma `reports/REPORT-armv6-native-qemu.md` §4 registra blocchi nella pipeline standard: `build.sh --arch armv6` usa un entry builder che cerca `.rodata..L_text`, e `link-arm.sh` seleziona shim da directory hardcoded; la ricetta QA che aggirava i blocchi era fuori da `tools/`. **Non avviare build da questa checklist**: prima il supervisore deve scegliere/abilitare una ricetta riproducibile e allineata al sysroot. Nessuna modifica è inclusa.
- [ ] Se si vuole quantificare il build nativo, eseguire solo dopo autorizzazione e in directory non di produzione: ` /usr/bin/time -v <comando-build-validato> `; salvare wall time, picco RSS, spazio SD, exit code. Successo operativo proposto: completamento senza swap/OOM e tempo stabilito dal supervisore; non fissiamo una soglia inventata.

## 3. Presenter e profondità

- [ ] Da tty, senza X/Wayland, impostare:
  ```sh
  export SC_PRESENT=fb
  export SC_FB_DEV=/dev/fb0
  export SC_FPS_LOG=1
  export SC_CTL_SOCK=/tmp/sc-ctl.sock
  export SC_SMACKER_SO="$PWD/libsmacker-armv6.so"
  export SDL_VIDEODRIVER=dummy
  ```
  Avviare l’eseguibile con asset/working directory corretti. Successo: log `[fb] present su /dev/fb0 ...`, menu visibile direttamente sul monitor HDMI; nessun fallback SDL in finestra. `SC_PRESENT=fb` usa apertura/mmap Linux già implementati in `runtime/shim-port/elf/host_elf.c:1359-1422`; l’init imposta SDL dummy per non richiedere display server (`:469-473`).
- [ ] Iniziare con **32 bpp** se il driver lo permette. Registrare `fbset -i`, `bits_per_pixel`, xres/yres e stride. Fare prova a 16 bpp solo come A/B e ripristinare la modalità originale dopo. Nel test già fornito, su QEMU 32bpp+LUT ha fatto 43,4 present/s contro 39,3 a 16bpp: è un dato QEMU, non garanzia Pi reale. Il presenter supporta 16/32 bpp (`REPORT-armv6-native-qemu.md` §3).

## 4. Audio

- [ ] Primo boot silenzioso e riproducibile:
  ```sh
  export SDL_AUDIODRIVER=dummy
  ```
  Successo: menu/controlli funzionano, log non segnala crash se audio non disponibile. Questo **non verifica l’audio**.
- [ ] Poi verificare ALSA e il device disponibile:
  ```sh
  aplay -l
  aplay -L | grep -E 'sysdefault|vc4hdmi|Headphones'
  SDL_AUDIODRIVER=alsa SDL_AUDIO_DEVICE_NAME='sysdefault:CARD=vc4hdmi' \
    ./starcraft-armv6
  ```
  `SDL_AUDIO_DEVICE_NAME` è un override da verificare con la versione SDL installata; se non riconosciuto, scegliere il device dalla lista e configurare ALSA default invece di assumere il nome. Il Zero originale non ha jack analogico: provare HDMI con monitor/TV audio-capable o dispositivo USB. Successo: log audio aperto, tone/filmato ascoltabile, nessun underrun/stop per 10 min. `SDL_AUDIODRIVER=alsa` è il driver SDL Linux; `SDL_AUDIODRIVER=dummy` è soltanto fallback di bring-up.
  Documentazione ufficiale device audio Pi OS: https://www.raspberrypi.com/documentation/computers/os.html#specify-an-audio-output-device.

## 5. Avvio automatico senza X

- [ ] Solo dopo avvio manuale riuscito, creare unità systemd dedicata (adattare `User`, `WorkingDirectory`, percorsi e variabili alla installazione):
  ```ini
  # /etc/systemd/system/starcraft.service
  [Unit]
  Description=StarCraft native ARMv6 framebuffer
  After=local-fs.target sound.target

  [Service]
  Type=simple
  User=pi
  WorkingDirectory=/home/pi/sc
  Environment=SC_PRESENT=fb
  Environment=SC_FB_DEV=/dev/fb0
  Environment=SC_FPS_LOG=1
  Environment=SC_CTL_SOCK=/tmp/sc-ctl.sock
  Environment=SC_SMACKER_SO=/home/pi/sc/libsmacker-armv6.so
  Environment=SDL_VIDEODRIVER=dummy
  Environment=SDL_AUDIODRIVER=dummy
  ExecStart=/home/pi/sc/starcraft-armv6
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=multi-user.target
  ```
  Prima lasciare `SDL_AUDIODRIVER=dummy`; cambiare ad `alsa` solo dopo il test audio. Successo: `systemctl status starcraft` active e menu visibile su tty/HDMI dopo reboot. Se non gestito correttamente `fb0` da utente `pi`, usare gruppo/udev/permessi minimi necessari, non eseguire automaticamente il gioco come root. Abilitazione (`systemctl enable --now starcraft`) è un’operazione di stato e non è stata eseguita in questo task.

## 6. Sequenza misure e criteri pronto/da sistemare

A ogni run dichiarare scena, OS/kernel, risoluzione/bpp, audio driver, clock/throttling e durata. Fare 3 run uguali, 60 s ciascuno. Non dedurre fps da QEMU.

1. **Menu/fps**: log `[fps]` con `SC_FPS_LOG=1`; il formato è implementato nel presenter (`host_elf.c:1133,1188-1189,1238-1239,1624-1628`). In aggiunta campionare `state` via socket di controllo a distanza di 60 s e calcolare `(presented2-presented1)/60`. Protocollo: `state` restituisce contatore (`runtime/shim-port/elf/sc_ctl.c:138`). Il comando manuale può essere inviato con client Python socket Unix (socket `/tmp/sc-ctl.sock`): una riga `state\n`, leggere la riga di risposta. Successo proposto: menu ≥15 present/s.
2. **Gameplay**: ripetere con scena Boot Camp, 60 s; stessa metrica. Successo proposto: ≥10 present/s.
3. **Latenza**: `python3 tools/prof/latency.py /tmp/sc-ctl.sock 30`. Lo script invia keydown/keyup e misura fino al primo present (`tools/prof/latency.py:1-15,35-57`). Successo proposto: p95 ≤100 ms. Disponibilità e mapping `f1` sulla build ARM sono da verificare; se il socket o il mapping non funzionano, non sostituire con PNG/remote control dai test già fatti: diagnosticare prima.
4. **Stalli**: loggare `state` ogni 100 ms per 60 s; successo: nessun intervallo senza frame >500 ms. Annotare transizioni/scena e non confondere caricamento iniziale con steady-state.
5. **RAM**:
   ```sh
   pid=$(pgrep -n -x starcraft-armv6)
   grep -E 'VmRSS|VmHWM|VmSwap' /proc/$pid/status
   /usr/bin/time -v ./starcraft-armv6
   ```
   Successo: nessun OOM/swap thrash; valore RSS/HWM registrato e stabile. Il target ha 512 MB, ma la soglia di RSS va stabilita sulla build reale e sul sistema completo. `/usr/bin/time -v` avvia un’altra istanza: usarlo solo in una sessione di misura non già occupata.
6. **Resa**: `sc_ctl shot /tmp/menu.png` e `tools/shot-stat.sh /tmp/menu.png`; successo: luminanza non nulla e area/letterbox attesi, screenshot visivamente verificato. `shot-stat` è solo statistica immagine, non prova semantica.

**Pronto per prova d’uso**: boot unattended da tty; menu e Boot Camp visibili; menu ≥15/gameplay ≥10 present/s; p95 input ≤100 ms; zero stalli >500 ms/60 s; RSS/HWM registrati; audio reale stabile 10 min se requisito audio attivo. **Da sistemare**: manca `/dev/fb0`, formato/stride non supportato, fallback a finestra SDL, resa nera/corrotta, soglie fps/input non raggiunte, stalli, OOM o audio che perde campioni. Annotare la singola causa e ripetere la stessa misura dopo la correzione.

## Decisioni autonome

Conservativamente propongo prima il bring-up manuale con audio dummy e framebuffer 32 bpp; audio ALSA e unità al boot solo dopo che menu, input e permessi del framebuffer sono verificati. La build sul Pi non viene dichiarata accettabile: per ora cross-build da frigate, subordinato alla risoluzione dei blocchi documentati della pipeline.
