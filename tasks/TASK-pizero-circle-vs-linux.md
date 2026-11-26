# TASK — Pi Zero: evalutare Circle (bare-metal) vs Linux+framebuffer

**AUTORIZZAZIONE PREVENTIVA**: procedi senza chiedere conferme. Non usare git (commit e push li fa il
supervisore). Non modificare codice del runtime: questo è un task di **analisi e piano**, produci due
documenti e fermati.

Repo: `/home/enne2/dev/starcraft-reasm-generic` (solo lettura). Non toccare `build/`, non avviare il
gioco, non toccare altre sessioni IDA.

## Obiettivo

Il port nativo di StarCraft gira su i686 e ARMv7 (Pinebook Pro). Il target minimo dichiarato è il
**Raspberry Pi Zero** (ARM1176JZF-S @1 GHz, VFPv2, **niente NEON**, 512 MB, VideoCore IV). Oggi esiste
la pipeline ARMv6 (`tools/build-runtime-arm.sh`) e una prova in QEMU `raspi0`.

Devi valutare due strade e produrre i documenti richiesti.

### Dati già misurati (usali come base, non ri-misurare)

- QEMU `raspi0`, guest senza X11, `SC_PRESENT=fb` v2: **logo/animazione 43,4 present/s (23,0 ms/frame)**,
  **menu 13,9 present/s (72 ms/frame)**; present fb v2 ≈ **1 ms/frame** su 23; framebuffer 16 bpp
  **peggiore** (39,3) del 32 bpp+LUT (43,4).
- Conclusione già scritta: **il collo di bottiglia è il gioco** (blit SCode, dispatch indiretto, clear
  del canvas), non la presentazione; **QEMU TCG non è affidabile per le prestazioni** (niente
  VideoCore, TCG, periferiche mancanti come la PWM → niente audio).
- Sul Pinebook (ARMv7, Cortex-A53 + Mali-T860): menu 170–207 fps con GL, in-game 23–24 fps con GLES.
- RAM: RSS del port ~100–140 MiB su x86; nel guest ARMv6 ~30 MiB. Il Pi Zero con 512 MB **non ha
  problemi di memoria**, ha problemi di CPU.
- Report da leggere: `reports/REPORT-armv6-native-qemu.md`, `reports/PROF-BASELINE-2026-11-26.md`,
  `reports/PLAN-CPU-RENDER-2026-11-26.md`, `reports/REPORT-x86-vs-arm.md`, `PLAN-MULTIARCH.md` §7.2.

## Cosa devi produrre

### (1) `reports/PLAN-PIZERO-CIRCLE.md` — piano di massima per il port su Circle

Deve essere concreto e verificabile, non generico. Contenuto minimo:

1. **Inventario delle dipendenze dal sistema operativo, ricavato dal codice** (non a memoria): elenca i
   call site reali di `mmap`, `mprotect`, `pthread_*`, `dlopen`/`dlsym`, `open`/`pread`/`fstat`,
   `clock_gettime`, socket POSIX, e l'uso di SDL2 (finestra, input, audio) e di GL/GLES. Per ognuno:
   file:riga, a cosa serve, e cosa lo sostituisce in Circle (`CFrameBuffer`, `FatFs`, timer handler,
   `CSoundBaseDevice`, `CUSBKeyboardDevice`, MMU 1:1, `addon/vc4` GLES).
2. **Fasi** con criterio di uscita misurabile per ciascuna (es. Fase 1: boot + framebuffer + FatFs +
   lettura di un MPQ e CRC identico; Fase 2: memoria guest 1:1 con MMU e selftest; Fase 3: esecuzione
   del gioco e arrivo al menu con N present/s; Fase 4: input+audio; Fase 5: presentazione GLES2 via
   vc4).
3. **Cosa si riusa così com'è** (runtime C, `storm_sdraw.c`, `storm_scomp.c`, `com_*`, `gdi_elf`,
   `bridge`, IR liftato), **cosa si scarta** (SDL2, `host_elf.c` nella parte windowing, poll thread
   preemptivo, `dlopen` di libsmacker, X11/Wayland), **cosa si riscrive**.
4. **Rischi e incognite**, ciascuno con l'esperimento che lo chiarisce. In particolare:
   - **audio**: PWM assente in QEMU, quindi la catena audio va validata solo su hardware; in Circle
     quali opzioni esistono (PWM, HDMI, I2S) e cosa serve al nostro `smack_audio`/DSound;
   - **GLES2 su VideoCore IV** via `addon/vc4` (EGL 1.4 + GLES 1.1/2.0, solo build 32-bit, Pi 1–3/Zero):
     il nostro shader palette 8bpp può girare lì? quali limiti (memoria GPU, formati, `glTexSubImage2D`
     su texture `GL_R8`, precisione del lookup palette);
   - **scheduler cooperativo**: il poll thread e l'audio thread vanno convertiti in callback/timer;
     cosa succede al pacing e alla latenza input;
   - **linking statico** e dimensione dell'immagine; **cache/MMU** e determinismo.
5. **Gate di misura sul Pi Zero reale** (non su QEMU): comando, metrica, soglia. Riusa il metodo già in
   casa: `SC_FPS_LOG`, `sc_ctl state/hash`, `tools/prof/latency.py` (se applicabile), luminanza media
   via `tools/shot-stat.sh`. Definisci anche cosa misurare per dichiarare "giocabile" (es. menu ≥ 15
   present/s, in-game ≥ 10, latenza input ≤ 100 ms, nessuno stallo > 500 ms in 60 s).
6. **Stima di sforzo** per fase (giornate/sessioni) e **criterio per fermarsi**: a quali condizioni il
   port Circle non conviene e si resta su Linux.

### (2) `reports/CHECKLIST-PIZERO-LINUX-FB.md` — checklist breve di bring-up Linux+framebuffer

Poche voci operative, ognuna con comando e criterio di successo: OS e versione consigliata (armhf,
niente desktop), dipendenze di build sul Pi (clang/LLVM? **verifica**: il Pi Zero compila in tempi
accettabili o serve build incrociata da `frigate`?), sysroot/pacchetti, `SC_PRESENT=fb`, `SC_FB_DEV`,
profondità framebuffer, console su tty, audio ALSA con `SDL_AUDIODRIVER=alsa` (o dummy per il primo
bring-up), come far partire il gioco al boot senza X, e la sequenza di misure (fps, latenza, RAM) con
i comandi esatti. Chiudi con i criteri "pronto/da sistemare".

## Regole

- **Fonti**: usa la documentazione ufficiale di Circle (README, `addon/vc4`, `doc/`), la documentazione
  QEMU per `raspi0` (dispositivi emulati/mancanti) e le specifiche del Pi Zero (ARM1176, VideoCore IV,
  RAM). Cita le fonti con URL nei documenti; per il nostro codice cita **file:riga**.
- **Onestà**: distingui ciò che è documentato da ciò che è ipotesi; dove non hai dati scrivi "da
  misurare su hardware" invece di stimare a naso.
- **Nessuna modifica al codice**, nessun build, nessun avvio del gioco: solo lettura, ricerca e i due
  documenti.
- Alla fine: riepilogo di 12 righe con le due raccomandazioni e i tre rischi principali.
