# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, vtable o crash guest (`0x00404F26`, `0x004FDE70`, ecc.), l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`) oppure `llvm-objdump -d` sui PE originali e `rg` mirato su `build/lift-full/*.llasm`. Controlla sempre `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON iniettare input desktop (no mouse/tastiera/ydotool/click/tasti).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-real`: Sbloccare la finestra reale SDL2 su `frigate` e chiarire lo stop `0x1805F2D4`

Leggi prima:
- `reports/STATE-x86-bringup6.md` e `reports/REPORT-x86-bringup6.md`
- `runtime/shim-port/elf/host_elf.c` (per capire `host_elf_init` / `SDL_Init` / `SDL_CreateWindow` / `SDL_OpenAudioDevice`!)

## File di proprietà
- `runtime/shim-port/elf/{host_elf.c,win32_ui.c,smack_audio.c,com_dsound.c}` (solo se emerge un bug dimostrato per SDL2 reale / Wayland / X11 / audio 32-bit su `frigate`)
- `runtime/arm/*`, `runtime/x86/*`, `tools/*x86*`
- `build/obj-x86/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-real.md`
- `reports/STATE-x86-real.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Diagnosi del blocco SDL2 reale (`[host] init: video driver = x11` e basta)**:
   - Esegui il comando GDB dal `Prossimo passo` di `REPORT-x86-bringup6.md`:
     ```sh
     cd /home/enne2/dev/starcraft-reasm-generic/build/asset-sandbox
     SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=x11 \
       timeout -k 2s 15s gdb -q -batch -ex 'set pagination off' -ex 'set debuginfod enabled off' \
       -ex 'handle SIGSEGV stop print nopass' \
       -ex 'break SDL_CreateWindow' -ex 'break host_elf_display_mode' \
       -ex run -ex 'info threads' -ex 'thread apply all bt' --args ../starcraft-x86
     ```
     (E se `run` non si ferma prima del timeout, lancia `../starcraft-x86` in background per 3s e attacca `gdb -p` o manda `SIGABRT` dopo 3s per vedere esattamente in quale funzione SDL2/X11/PulseAudio/PipeWire/Mesa è fermo il thread!)
   - Controlla anche:
     - `SDL_VIDEODRIVER=wayland` vs `SDL_VIDEODRIVER=x11` (su Fedora 44 KDE Wayland + `sdl2-compat.i686`!).
     - `SDL_AUDIODRIVER=pulseaudio` vs `alsa` vs `pipewire` vs `dummy` (spesso `SDL_OpenAudioDevice` / PulseAudio 32-bit si blocca o manca `pipewire-alsa.i686` / `pulseaudio-libs.i686` — verifica quali `.so` 32-bit audio/video sono presenti con `ldconfig -p | grep -E 'SDL|pulse|pipewire|X11|wayland'`!).
     - Se `SDL_VIDEODRIVER=wayland` o `x11` funziona senza bloccarsi o se `host_elf.c` fa qualcosa (es. `SDL_INIT_AUDIO` / fullscreen / `SDL_WINDOW_FULLSCREEN_DESKTOP` / GL / renderer) che blocca su `sdl2-compat.i686`!

2. **Verifica reale della finestra SDL2 e menu su `frigate`**:
   - Applica l'eventuale fix pulito (o documenta il driver/config corretto se è un'opzione SDL2/Wayland/PulseAudio) e verifica con un run di **5 secondi** (`timeout -k 1s 5s`) da `build/asset-sandbox`:
     - Deve stampare `[host] init: video driver = ...` (non `dummy`!), `[host] finestra creata`, inizializzazione audio reale (o fallback esplicito documentato se manca libreria 32-bit su host), `SmackOpen`, e multipli `[fps]`!
     - Salva il log completo in `build/qa/run-x86-real-sdl.log`.

3. **Chiarisci lo STOP `0x1805F2D4` in `build/qa/run-x86-bringup6-smoke.log`**:
   - Controlla i timestamp / righe intorno al minuto in `build/qa/run-x86-bringup6-smoke.log` (es. quante righe `[fps]` prima di `0x1805F2D4`, cosa è `0x1805F2D4` con `llvm-nm -n` / `readelf -SW`, e cosa fa il guest a `0x00404F26` / `0x004FDE70` / `0x004FDE7B` in `StarCraft.orig.exe` / `StarCraft.llasm`!).
   - Se è un bug facilmente risolvibile (es. una exit/callback table o un secondo bug come `0x0041CAC8` su ARM), documentalo con precisione (e fissalo solo se chiaro e pulito senza aggirare D19; altrimenti documentalo nel report con gli indirizzi reali).

4. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-real.md` e `reports/STATE-x86-real.md` (max 70 righe).

## Gate di accettazione (oggettivi)
1. Causa esatta del blocco SDL2 reale a `[host] init` dimostrata con backtrace/evidenza reale.
2. Esecuzione reale su `frigate` (non dummy) per 5 secondi raggiunge `[host] finestra creata` e `[fps]` nel menu principale (`build/qa/run-x86-real-sdl.log`).
3. Analisi precisa di `0x1805F2D4` (`0x00404F26` / `0x004FDE70`).
4. `reports/REPORT-x86-real.md` e `reports/STATE-x86-real.md` completi.
