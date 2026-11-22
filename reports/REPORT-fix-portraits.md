# Diagnosi parziale — ritratti animati briefing Terran

## Esito

Il difetto è stato riprodotto nel Briefing Terran in un’istanza isolata. La causa radice **non è stata dimostrata** e non è stato applicato alcun fix di rendering. È quindi scorretto dichiarare risolti i due riquadri neri o le scanline.

## Procedura e artefatti

- Creato `build/qa/fix-portraits/`.
- Compilati in privato gli oggetti `smack_elf.o` e `storm_sdraw_arm.o`, linkati sostituendo gli omonimi senza modificare gli oggetti condivisi. Il binario risultante è `build/qa/fix-portraits/starcraft-x86-port`.
- Esecuzione su Xvfb privato e audio dummy tramite `tools/run-isolated.sh`, ID `fix-port`; raggiunto il briefing e poi fermata l’istanza.
- Screenshot: `build/qa/fix-portraits/07-briefing-portraits.png` e `08-briefing-steady.png` (due riquadri neri, uno con scanline).
- Log live: `build/isolated/fix-port/game.log`.

## Evidenza dal log

I video-ritratto osservati si aprono come SMK2 con `flags=0x1000`, dimensioni `60×56`, 10 frame (alcune aperture riportano 15). Storm chiama `SmackBufferOpen(0, 4, 60, 56, 60, 56)`, che alloca `0xD20` byte. `SmackToBuffer` riceve `left=0`, `top=0`, `pitch=60`, `destheight=56`, `flags=0`; seguono `SmackDoFrame` e `SmackNextFrame`.

Questi dati escludono, per le chiamate catturate, l’ipotesi che i ritratti neri siano dovuti direttamente a `flags != 0` o a un `pitch/destheight` diverso da quello del video. L’instrumentazione del blitter `storm_313` non ha registrato chiamate `width=60 && rows=56`; quindi non dimostra né esclude l’ipotesi sulle operazioni Storm successive (palette, trasparenza o copia/scaling).

## Modifiche e limiti

Sono stati aggiunti temporaneamente log diagnostici in `runtime/shim-port/elf/smack_elf.c` e `runtime/common/storm_sdraw_arm.c`, poi rimossi: i sorgenti sono tornati senza questi marker. Il binario privato e il log conservano la diagnostica. Non è stato applicato un fix logico, non è stata prodotta una build pulita di verifica e non è stato verificato visivamente un esito corretto. Non sono stati modificati `win32_shim.c`, `win32_shell_dlg.c`, `build/starcraft-x86`, `build/obj-x86`, `proc_range` o D19.

`qmem_store`: episodio parziale salvato come `abe6d83f-6a46-40a3-a03f-a3f56815364a`.

## Decisioni autonome

Non avendo evidenza live sul blit/palette che causa il difetto, ho scelto di non alterare la logica dei buffer, dei colori o dell’interlacciamento con un fix congetturale. Il lavoro rimane aperto.
