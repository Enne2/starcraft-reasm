# Ottimizzazioni CPU: GL hardware, syscall per frame, pacing reale (2026-11-22)

Ambiente: frigate, Xvfb 640x480 dedicato, audio dummy, partita Boot Camp, misure da `/proc` e `strace -c`.

## 1. Rendering hardware OpenGL/GLES2
- `host_elf.c`: diagnostica `[host] GL ...` su ogni punto di fallimento (creazione contesto con `SDL_GetError()`,
  shader compile/link con info log, simboli proc mancanti, mismatch `SC_GL_VERIFY`).
- retry dei profili: GLES 2.0 -> desktop GL 2.1 -> GLES 3.0 -> GL 3.2 core, prima del fallback GPU/CPU;
  resta il rifiuto dei renderer software (llvmpipe/softpipe/swrast/SwiftShader) in auto-select.
- Verifica su compositor Wayland GPU-backed headless (`kwin_wayland --virtual`, nessuna finestra utente):
  `[host] GL profil GLES 2.0 attivo ... renderer: AMD Radeon 780M (radeonsi)`,
  `[host] present: OpenGL/GLES2 palette shader (8bpp upload)`,
  `SC_GL_VERIFY PASS: 307200 pixel esatti` (parita' pixel 1:1), FPS 23-24 con cap.

## 2. Syscall per frame

| Intervento | File | Prima | Dopo |
|---|---|---|---|
| control-file: fd persistente + letture 512 B + backoff/rotazione/troncamento | `host_elf.c` | ~2.400 openat/s, ~3.400 read/s, ~2.400 lseek/s | 0 / 0 / 0 (steady-state) |
| gate `recvfrom` SNP, `SC_NET_POLL_MS` (default 1 ms, 0=off) | `snp_elf.c` | 103 recvfrom su 100 poll vuoti | 3 |
| CriticalSection lock-free (hash CAS + atomics) | `win32_shim.c` | 50,66 ns/op (2 thread, 8 CS) | 10,39 ns/op |
| tetto al pump di input `SC_PUMP_HZ` (default 500 Hz, 0=off) | `host_elf.c` | 54.398 recvmsg/3 s, 13.760 poll/3 s | 6.070 (-89%), 1.681 (-88%) |

## 3. Pacing reale del frame (la vera causa del ~100% CPU)

Il loop del gioco gira in spin: col cap a 25 FPS il nostro `present` usciva subito e il gioco continuava a
chiamarlo bruciando il core. Ora con `SC_FPS_CAP` attivo `present` dorme il tempo residuo al budget
(`SC_FPS_SLEEP_MAX`, default 4 ms per chiamata; `SC_FPS_SLEEP=0` disabilita), replicando il blocco del `Blt`
con vsync del 1998.

| Configurazione (Boot Camp, 25 FPS) | CPU (core) |
|---|---:|
| Busy-wait (`SC_FPS_SLEEP=0`) | 108,2 % |
| Pacing default (`SC_FPS_SLEEP_MAX=4`) | 23,6 % |
| Pacing aggressivo (`SC_FPS_SLEEP_MAX=20`) | 16,2 % - non usabile: caricamento missione bloccato |

Verifica funzionale col pacing default: partita Boot Camp raggiunta, frame identico
(`build/qa/opt-final/pace2-gameplay.png`), `[fps] GPU 23 fps` costanti, `Menu -> Exit` chiude il processo.

## Note
- `SC_FPS_SLEEP_MAX > 4` non e' compatibile con la schermata di caricamento (stato "Loading" bloccato).
- Il cap del pump a 500 Hz introduce <= 2 ms di latenza input (il gioco campiona a 40 ms).
- Ogni fix e' disattivabile per A/B: `SC_PUMP_HZ=0`, `SC_NET_POLL_MS=0`, `SC_FPS_SLEEP=0`.
