# Stato fix-portraits

- Esito: diagnosi parziale, causa radice non provata; nessun fix logico.
- Repro: briefing Terran in Xvfb/audio dummy, `fix-port` fermata.
- Screenshot: `build/qa/fix-portraits/07-briefing-portraits.png`, `08-briefing-steady.png`.
- Log: `build/isolated/fix-port/game.log`.
- Log: video SMK2 60×56; BufferOpen (0,4,60,56,60,56), ToBuffer pitch 60/destheight 56/flags 0; DoFrame/NextFrame ricevuti.
- Lo strumento `storm_313` non ha rilevato un blit 60×56. Palette/trasparenza/scaling successivi ancora da indagare.
- I log diagnostici temporanei sono stati tolti dai sorgenti; il binario QA conserva i log. Nessuna build pulita né verifica di fix.
- `proc_range`, D19 e file vietati intatti. Nessuna modifica a `build/obj-x86/` o `build/starcraft-x86`.
- qmem: `abe6d83f-6a46-40a3-a03f-a3f56815364a`.
