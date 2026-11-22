# Missione: rending hardware OpenGL/GLES2 affidabile e diagnosticabile (auto-select)

## Stato attuale verificato
- `runtime/shim-port/elf/host_elf.c` ha già il presenter GL/GLES2 palette-shader (`present_gl()`, `gl_init()`),
  con rifiuto dei renderer **software** (`software_gl_renderer()`: llvmpipe/softpipe/swrast/SwiftShader/Microsoft Basic)
  e fallback `gl → gpu(texture) → cpu`.
- `g_gl_requested` è vero con `SC_PRESENT=gl` o `auto`/unset (driver != dummy).
- **Problema 1:** in caso di fallimento `gl_init()` non logga *perché* (CreateContext, shader compile/link, proc mancante):
  silenziosamente si finisce su GPU/CPU. Su una sessione Wayland reale (3440x1440, kwin) il gioco ha scelto
  "percorso GPU (renderer + texture)" senza dire nulla, mentre in un compositore Wayland 640x480 con GPU
  (kwin_wayland `--virtual`) il percorso GL **funziona**.
- **Problema 2:** il contesto è creato solo come **GLES 2.0** (`SDL_GL_CONTEXT_PROFILE_ES`). Se il driver
  rifiuta quel profilo non c'è retry con **desktop GL 2.1** (ho verificato con un probe ctypes: ES2 OK su wayland
  e x11; GL2.0 fallisce solo su wayland perchè il profilo richiesto è ES).

## Obiettivo
1. **Diagnostica**: log chiaro (una volta sola) di ogni abbandono del percorso GL: `SDL_GL_CreateContext` fallita
   con `SDL_GetError()`, shader non compilato/linkato con **info log** del driver, simbolo GL_LOAD mancante,
   `SC_GL_VERIFY` mismatch. Prefisso uniforme `[host] GL …`.
2. **Retry profilo portabile**: prova `GLES 2.0`; se fallisce prova `desktop GL 2.1` (senza `PROFILE_ES`),
   poi eventualmente `GLES 3.0`/`GL 3.2 core` come ultimo tentativo, prima di dichiarare `g_gl_failed`.
   Deve funzionare su driver diversi (Mesa radeonsi, Intel, NVIDIA, ANGLE, Mali) senza `#ifdef` di architettura.
3. **auto-select**: con GPU hardware disponibile deve usare GL (già così) e **mai** software GL (già così),
   senza regressioni sul percorso GPU/CPU.

## Ambiente di verifica hardware SENZA toccare il desktop dell'utente (già pronto)
- Compositor Wayland **GPU-backed headless** già avviato: `kwin_wayland --virtual --width 640 --height 480 --socket wl-nested`.
  Usa `WAYLAND_DISPLAY=wl-nested SDL_VIDEODRIVER=wayland`. Nessuna finestra compare sul desktop reale.
- Esempio: `( cd build/asset-sandbox && env WAYLAND_DISPLAY=wl-nested SDL_VIDEODRIVER=wayland SC_SKIP_INTRO=1 \
  SC_FPS_LOG=1 SDL_AUDIODRIVER=dummy SC_SMACKER_SO=$PWD/../i386-libs/libsmacker-1.2.0.so [SC_PRESENT=…] \
  timeout -k 1 15 ../starcraft-x86 > /tmp/gl.log 2>&1 )`
- **Attenzione:** NON usare `pgrep -f` per il PID (ho ottenuto CPU assurde perché matchava il wrapper):
  seleziona il PID leggendo `/proc/*/comm == "starcraft-x86"`.

## Verifiche richieste
1. **Parità pixel**: `SC_GL_VERIFY=1` deve stampare `SC_GL_VERIFY PASS: 307200 pixel esatti` sul percorso GL hardware
   (e resta valido il test esistente `tests/x86_gl_palette_test`).
2. **A/B prestazioni** nello stesso compositor headless, stesso punto (menu e partita Boot Camp):
   `SC_PRESENT=gl` vs `SC_PRESENT=gpu` vs `SC_PRESENT=cpu` → CPU del processo (metodo `/proc/<pid>/task/*/stat`)
   e FPS da `[fps]`; riporta 2 run e la mediana.
3. Compilazione i686 **e** host x86-64 di `host_elf.c`: PASS (`-std=c11 -Wall -Wextra`).
4. Screenshot di parità: con `SC_PRESENT=gl` cattura il menu (usa `import -window root -display` sul display
   Xwayland del compositor **oppure** `SC_GL_VERIFY`) e conferma che la resa è identica al percorso GPU.

## Vincoli
- Modificare SOLO `runtime/shim-port/elf/host_elf.c` (+ eventuale test in `tests/x86_gl_palette_test/`).
- Il working tree è sporco: altri sottoagenti stanno modificando `host_elf.c` (control-file), `snp_elf.c`,
  `win32_shim.c`. **Non fare revert/stash**, e coordina le modifiche al solo blocco GL/present di `host_elf.c`.
- C11 portabile, CPU-agnostico, nessun assembly, nessun `#ifdef` di architettura.
- Nessuna finestra sul desktop dell'utente e nessun input iniettato.

## Deliverable
- Patch in `host_elf.c`; `reports/REPORT-opt-gl.md`, `reports/STATE-opt-gl.md` con A/B, log di diagnosi e parità pixel.
