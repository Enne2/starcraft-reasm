# Stato — scx86-gfx (21/11/2026)

- Fix in `runtime/shim-port/elf/com_ddraw.c`: clipper con `HWND`, copia/lettura RGNDATA, changed flag, refcount su surface, limiti input e cleanup.
- Fix in `runtime/shim-port/elf/gdi_elf.c`: `SetDIBits` supporta conversioni 8/24/32 bpp; palette DIB in ingresso, nearest-color verso bitmap 8bpp.
- Smack esaminato: log `gfx-camp2` mostra `SmackToBuffer flags=0`, dati pixel non-zero e hash sorgente=destinazione; nessuna `SmackVolumePan`/`SmackBufferNewPalette`, track_mask=0. Audio e pan già instradati in `smack_audio.c`; nessun cambiamento non supportato applicato.
- Briefing: `build/qa/gfx/26-briefing.png`, prevalentemente nero, un ritratto a scanline. Decodifica/buffer validi; causa a valle palette/SDraw o asset/ordine ritratti ancora aperta.
- Compilazione in `build/qa/gfx/`; link privato `build/qa/gfx/starcraft-x86-gfx` verificato ELF32 i386. Oggetti preesistenti di `build/obj-x86/` solo letti.
- Prova solo Xvfb + SDL dummy (`run-isolated.sh`, id `gfx-camp2`): Boot Camp avviato, HUD giocabile; SCV selezionato e ordinato Supply Depot. Minerali 150→50 e SCV al sito: costruzione avviata (`build/qa/gfx/41-depot-progress.png`). Menu verificato (`42-menu.png`). Zerg/Protoss intro visualizzati (`08-zerg-confirm.png`, `13-protoss.png`).
- Nessun nuovo STOP/opcode SCode/ROP; `proc_range` e D19 invariati. Snp `spiReceive` non implementata, nessun blocco.
- `gfx-camp` e `gfx-camp2` arrestati; status finale camp2 `game_alive=0`, `xvfb_alive=0`; nessun processo StarCraft attivo.
- Limiti: clip list non provata con caller reale né applicata ai Blt; mancano unit test diretti. Refinery non provata. Diagnosi ritratti non conclusiva.
- Report: `reports/REPORT-gfx.md`.

## Decisioni autonome

- Nessun decoder flags Smack speculative o kernel SCode ipotetico: i flag osservati sono zero e gli opcode incontrati sono supportati. I limiti residui sono dichiarati, fail-closed resta intatto.
