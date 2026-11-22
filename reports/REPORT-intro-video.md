# Report — intro video nero su ARMv7

**Esito:** causa non chiusa; non dichiarare il bug risolto. La build diagnostica è installata sul Pinebook e il gioco è stato lasciato avviato con `SC_SKIP_INTRO=1`.

## Evidenze misurate

- **Readback accoppiato:** aggiunto `scctl rawshot <path>`: scrive i 640×480 indici grezzi (307200 byte) e `<path>.png`, RGB trasformato con la palette dallo stesso frame. Sul Pinebook, cattura `build/qa/intro-video/arm-final.jpg`: tutta nera; indici 0 nonzero, un solo valore (0); RGB media 0. La compressione della PNG originale è stata fatta con `magick -quality 70 -strip` prima della lettura.
- **Controllo x86:** `build/qa/intro-video/x86-pair.jpg` mostra il logo Blizzard; 60833 indici non-zero e 210 valori distinti nel rawshot.
- **Decode/SCode ARM:** log `game-intro-video-trace2.log`: `DoFrame` ha frame con pixel nonzero (`nonzero=0x981C1` al frame `0x1E`); `dsthash` segue l'hash del frame. Il SCode 2× LUT viene eseguito (`entry=0x201600BC`). La sonda in `storm_sdraw_arm.c` ha letto dopo il blit 0xB1B9 byte non-zero sia dalla destinazione sia dal puntatore restituito da `host_elf_canvas()`, mentre il rawshot accoppiato ha trovato zero. Questo disallineamento temporale/puntatori non consente di concludere che il canvas resti sempre vuoto; suggerisce un problema nel successivo clear/handoff/present o nella coerenza della lettura, non un guasto dimostrato del decoder.
- **Palette:** non è la causa dimostrata. La cattura RGB e quella indicizzata accoppiate sono entrambe nere. Il percorso Storm su IDA `storm_457` `0x1502F500` → `storm_357` `0x15034EC0` → `storm_354` `0x15034C20` chiama `SetPaletteEntries` e `RealizePalette`; il backend è in `runtime/shim-port/elf/gdi_elf.c` (`SetPaletteEntries`, `RealizePalette`, `palette_to_host`). Non ho provato che il problema sia lì né applicato una modifica di palette.
- **Buffer/overrun:** argomenti loggati: pitch `0x140` (320), altezza `0x134` (308), byte allocati `0x18100`. Correzione aritmetica: `0x18100 = 98560 = 320×308`; `98304 = 0x18000`, dunque il sospetto overrun di 256 byte nasce da un valore esadecimale letto male. Aggiunto registro delle allocazioni in `smack_elf.c`; `ToBuffer` rifiuta frame se `pitch×height` supera i byte assegnati e `copy_frame_data` verifica il limite prima di copiare. Nei log eseguiti non compare un rifiuto per dimensione.

## Modifiche e verifiche

- `runtime/shim-port/elf/sc_ctl.c`, `tools/scctl`, `tests/sc_ctl_test/check.sh`: rawshot e PNG RGB accoppiata; test unitario PASS.
- `runtime/shim-port/elf/smack_elf.c`: tracking delle dimensioni allocate e rifiuto/log del frame fuori limite.
- `runtime/common/storm_sdraw_arm.c`: diagnostica, attiva con `REASM_TRACE=1`, per confrontare source/destination/canvas sul fast path video 2× LUT.
- Build x86 `./tools/build.sh --arch x86 --skip-entry` PASS; `tests/x86_scomp_test/check.sh` PASS; `tests/x86_runtime_check/check.sh` PASS. Intro x86 visibile nella cattura compressa sopra.
- Build ARMv7 PASS e deploy verificato: SHA256 locale/remoto `9164a7a6ad42d438acb1f956f7cd622dcce404e24dd0cef8303a7e32aebc83a3`.
- Avvio Pinebook con `SC_SKIP_INTRO=1` mantenuto vivo e socket `sc-ctl` risponde (`presented=137`, palette presente); restano alcuni filmati SMK brevi di startup/menu. Non sono stati verificati visivamente menu e partita in questa chiusura.

## Cosa serve per chiudere

Una singola cattura sincronizzata tra fine di `storm_334_arm_body` e `sc_ctl_present` (puntatore canvas + checksum/indici nei due punti), evitando confronti tra frame successivi. Se il contenuto scompare tra i punti, seguire chi azzera/cambia `g_canvas`; se persiste ma rawshot è zero, verificare il puntatore effettivo catturato da `sc_ctl_present`. Solo dopo intervenire su palette o blit.

## Decisioni autonome

Ho scelto la correzione conservativa: aggiungere diagnostica e limiti di memoria, senza alterare palette o semantica SCode in assenza di prova. Ho lasciato il gioco avviato con `SC_SKIP_INTRO=1`, così resta fruibile senza l'intro mentre la causa rimane aperta.
