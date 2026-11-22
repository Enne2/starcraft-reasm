# Obiettivo: far funzionare il VIDEO INTRODUTTIVO su ARMv7 (ora e' nero)

## Fatto / verificato da me
- Su **x86** l'intro si vede: con `SC_SKIP_INTRO=0`, screenshot dal framebuffer (`sc_ctl shot`) hanno
  luminosita' media ~0.05 (contenuto visibile).
- Su **ARMv7 (Pinebook)** con `SC_SKIP_INTRO=0` il canvas e' **completamente nero** (media esattamente 0)
  per tutta la durata dei filmati; il gioco **decodifica** (log: `[smack] filmato pronto: 0x140x0x134`,
  `[smack] SmackBufferOpen: byte=0x18100`, `[smack] ToBuffer #1 dest=0x1B6EBB80 pitch=0x140 rows=0x134
  px0=0 dst0=0`, `ToBufferRect` ripetuti, `Wait`), poi il menu funziona normalmente.
- Su x86 nello stesso momento i `ToBuffer` hanno dimensioni diverse (es. `pitch=0xB8 rows=0x80`,
  `0xD8x0x88`, `0xFCx0x7C`) e `dst0`/`px0` valorizzati (0x2A), quindi il percorso e' simile ma con
  film non identici: non fidarti del confronto 1:1, strumenta.
- Nota: `SmackBufferOpen byte=0x18100` (98304) contro `pitch*rows = 320*308 = 98560`: il buffer chiesto
  dal gioco e' **piu' piccolo** di quanto scriviamo in `ToBuffer` -> sospetto overrun di 256 byte.
- Log utile gia' presente: `[smack] BufferNewPalette non coperta: palette applicata separatamente da storm`
  -> la palette del film la applica Storm: se quella strada su ARM non funziona, tutto esce nero.

## Strumenti a disposizione
- **API in-process** (dentro lo shim): `runtime/shim-port/elf/sc_ctl.c` + client `tools/scctl`
  (`state|shot <png>|click X Y|key|text`). `shot` = PNG **640x480 dal canvas** (8bpp+palette).
- Sul Pinebook il gioco si lancia con `~/.local/bin/starcraft-armv7` (launcher, `SC_CTL=1`,
  `SC_SKIP_INTRO=0` di default) e il socket e' `~/starcraft-armv7-test/sc-ctl.sock`.
- Debug disponibile: `REASM_TRACE=1` (file/open + `[ff]`), `SC_SMACK_TRACE=1`, `SC_SCODE_DUMP=1`
  (kernel SCode e dump), `SC_FPS_LOG=1`, `SC_PRESENT=cpu|gpu|gl`.
- Build x86: `./tools/build.sh --arch x86 --skip-entry`; ARM: `SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`;
  deploy ARM: ferma il processo (`pgrep -f "starcraft-armv7$"` -> kill), poi
  `scp build/starcraft-armv7 enne2@10.0.1.126:~/starcraft-armv7-test/starcraft-armv7`.

## Compito
1. Isola **dove** si perde il filmato su ARM, con misure: e' (a) il decode che produce frame neri,
   (b) il blit `ToBuffer` che scrive nel buffer sbagliato / con pitch sbagliato, (c) la palette del film
   non applicata (canali indici corretti ma palette a zero), o (d) il blit buffer->canvas (SCode) che
   su ARM non avviene. Distingui i casi leggendo gli **indici** del canvas, non solo l'RGB: aggiungi se
   serve un comando `sc_ctl` tipo `rawshot <path>` che scriva i 640x480 byte INDICIZZATI (o un istogramma),
   e/o logga il primo byte e un checksum del buffer di destinazione dentro `smack_elf.c`.
2. Correggi la causa (aree candidate: `runtime/shim-port/elf/smack_elf.c` — `SmackBufferOpen`/`ToBuffer`/
   `ToBufferRect`/palette; `runtime/common/storm_sdraw_arm.c` per il blit video; `com_ddraw.c`/`host_elf.c`
   per la palette). Attenzione all'overrun 98304 vs 98560: o si rispetta il buffer del gioco, o si gestisce
   il caso con clamping e log (mai scrivere oltre).
3. Verifica su **x86** di non rompere: build, `tests/x86_scomp_test/check.sh`, `tests/x86_runtime_check/check.sh`,
   e intro visibile (media canvas > 0).
4. Verifica su **ARM**: intro **visibile** (screenshot con media > 0 e contenuto riconoscibile), menu e
   partita ancora funzionanti (`Play Custom` -> `(2)Bottleneck.scm`).
5. Se il decode non e' la causa e il tempo stringe, fornisci anche una **scorciatoia sicura**: la variabile
   per saltare solo l'intro (`SC_SKIP_INTRO=1`) e documentala nel report.

## Regole di contesto
- **Comprimi ogni immagine prima di leggerla** (`magick x.png -quality 70 -strip x.jpg`, ~40-70 KB) e
  leggi **solo** le immagini decisive. Niente loop periodici di screenshot.
- Non toccare `~/starcraft-armv7-test` mentre gira (ferma per PID prima di scp; mai scp su file in esecuzione).
- Working tree con modifiche altrui: niente `revert`/`stash`/`reset`.
- Chiudi con `reports/REPORT-intro-video.md` + `STATE-intro-video.md`: causa radice, patch, misure
  (media/istogramma canvas prima-dopo), screenshot compressi e cosa resta.
