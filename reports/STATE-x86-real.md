# Stato x86 reale — frigate

- Prova 5 s reale: `build/qa/run-x86-real-sdl.log`, `SDL_VIDEODRIVER=wayland`, exit timeout 124.
- SDL: init Wayland, finestra creata, renderer GPU; PipeWire reale 22050 Hz stereo; SmackOpen e FPS ripetuti.
- Menu principale non certificato: il dispatcher si ferma fail-closed su `0x1805F2D4` dopo FPS/animazioni SMK.
- X11 backtrace: `SDL_CreateWindow` → SDL2-compat/SDL3 `X11_ShowWindow` → `XIfEvent` → `_XReadEvents` → XCB `poll`. No audio nel percorso bloccato. Hint bypass compositor=0 non risolve.
- Librerie 32-bit audio presenti: `/lib/libpulse.so.0`, `/lib/libpipewire-0.3.so.0`; PipeWire effettivamente aperto.
- `0x00404F09..26` nel PE originale drena callback on-exit da `0x006DD684`; fa `call *%eax`, poi ripete. Target `0x1805F2D4` non è simbolo ELF né codice port (`.text` `0x18000000..0x1803A435`); provenienza del puntatore ignota.
- `0x004FDE70` chiama `0x00401970`, che scollega nodi da lista; `0x004FDE7B` riprende i globali. `llvm-objdump` usato perché `ida_open` database fallito.
- Nessuna modifica sorgente, nessun target arbitrario, resolver D19 invariato. Nessun git/input desktop.

## Decisioni autonome

Wayland scelto come driver funzionante; nessun fix X11 speculativo. Stop fail-closed mantenuto; il menu non viene dichiarato raggiunto senza evidenza.

Dettagli, stack GDB e prove: `reports/REPORT-x86-real.md`.