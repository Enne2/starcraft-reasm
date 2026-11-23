# Filmato introduttivo .smk NERO su ARMv7 — causa quasi isolata, serve l'ultimo anello

## Fatti MISURATI (non ipotesi), build attuale su Pinebook
- Il film si apre e decodifica: SMK2 320x308, 171 frame, `SmackBufferOpen bytes=0x18100 pitch=0x140 height=0x134`.
- Il blit SCode 2x scrive DENTRO il canvas: `dst = canvas + 0xD700`, `w=0x13F rows=0x134`, entry guest `0x20120628`.
- Subito dopo il blit, nella STESSA esecuzione: `nonzero src/dst/canvas = 0x5580 / 0xB1B9 / 0xB1B9`
  → il canvas CONTIENE il frame.
- Al `host_elf_present`, nella stessa esecuzione, sullo STESSO puntatore di canvas (verificato:
  `[canvas] alloc 0xEC104008`, `present-dbg canvas=0xEC104008`, UNA SOLA allocazione in tutto il log):
  `nz_idx=0x00000000` → il canvas è completamente azzerato.
- Palette sana al present: `pal_nonblack=0xEC` (236 voci non nere).
- Nessun `memset(g_canvas)` fuori da `canvas_alloc` (grep verificato).
- Le scritture SCode sul canvas registrate (`[canvas-write]`) sono SOLO i blit del film; nessun fill SCode full-screen.

## Domanda da chiudere
Chi azzera il canvas fra il blit del film e `host_elf_present`, oppure perché il frame non c'è più?
Candidati da verificare in ordine: (1) scrittura diretta guest (memcpy/memset non via SCode) sul canvas,
es. una `FillRect`/`Blt` GDI o un blit DirectDraw che ripristina un buffer precedente; (2) snapshot
doppio (`g_present_snapshot`) o percorso `present_gl/present_gpu` che usa un'altra superficie;
(3) ordine dei thread: film blittato dopo il present, e il present misurato è quello del frame prima
— in tal caso il fix è nel percorso di presentazione, non nel blit.

## Strumentazione già presente (usala, non riscriverla)
- `SC_PRESENT_DBG=1` → `[present-dbg] nz_idx=… max_idx=… canvas=… whp=… pal_nonblack=…`
- `REASM_TRACE=1` → `[canvas-write] off/w/r/dd/sd/entry` per ogni scrittura SCode sul canvas,
  e `[storm-scode-video] src/dst/canvas + nonzero`.
- `[canvas] alloc` a ogni (ri)allocazione del canvas.

## Vincoli
- Verifica NUMERICA, niente immagini nel contesto: `convert f.png -colorspace Gray -format "mean=%[fx:mean] max=%[fx:maxima]" info:`
  (su Pinebook `convert`, non `magick`). Nessuno screenshot da trasferire, nessuna immagine da leggere.
- Oracolo: se serve comportamento del binario, IDA (regola globale). Non inventare.
- Non toccare le altre sessioni IDA. Non `pkill -f` con pattern presente nella propria command line.
- Deploy ARM: build `SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`; kill delle sole istanze
  con `readlink /proc/<pid>/cwd` = `$HOME/starcraft-armv7-test`; poi `scp build/starcraft-armv7 enne2@10.0.1.126:~/starcraft-armv7-test/`.
- Regressione x86: `./tools/build.sh --arch x86 --skip-entry` + intro numerico (`SC_SKIP_INTRO=0`, media luminanza > 0.02).
- Committa SOLO quando il fix è verificato numericamente su ARM (e non peggiora x86); messaggio in italiano, push su `gitea` main, verifica SHA locale == remoto.
