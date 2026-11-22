# REPORT game-glue
## Stato (aggiornato: venerdì 20 novembre 2026 — 13:08)
MISSIONE 2: link exit 0; QEMU si ferma con SIGILL prima di WinMain/DirectDraw. IDA ha identificato il PC fault come l'istruzione x86 `retn` a `0x4060C0` dentro `__onexit_lk` (inizio `0x406041`); il flusso tradotto salta al VA PE originale invece che restare nel codice ARM.

## Cosa ho fatto (misurato)
- Preso come fatto verificato dal supervisore, senza ripetere la ricerca: nel lift originale `loc_404C21` è `private fastcc` e non ha call/invoke interne; il DCE di llc la elimina.
- Creato `build/lift-full/StarCraft.entry.ll` dalla copia dell'originale: rinominata la definizione privata in `@sc_loc_404C21_impl`, aggiunto in coda il wrapper globale C-ABI `@loc_404C21` che chiama l'implementazione con `fastcc`. La copia misura 72.307.841 byte e 2.132.548 righe.
- Applicata la pipeline di `tools/compile-arm.sh` via pipe, senza intermediari: clang target-layout, `opt -passes=verify`, `opt -passes='default<O2>'`, `llc -mtriple=armv6-unknown-linux-gnueabihf -mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard`. Data layout: `e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64`. `llvm-nm` conferma `00175664 T loc_404C21`; `readelf -h` conferma ELF32 ARM EABI5.
- Rimosso da `runtime/arm/game_glue.c` soltanto lo stub `loc_404C21`; `tools/link-arm.sh` usa `StarCraft.entry.o` al posto di `StarCraft.o` e mantiene `/tmp/game_glue.o`. `bash -n` passa e i controlli confermano il nuovo input e l'assenza dello stub.
- Con QEMU `-d in_asm,cpu,guest_errors`, il fault ha `R15=0x004060C0`, `R01=0x004060C1`, PSR Thumb. IDA (`build/relocs/StarCraft.exe.i64`, sessione `e11792b1`) identifica `0x406041` come `__onexit_lk` (size `0x80`): decompilata per intero; l'istruzione `0x4060C0` è `retn`. IDA xref: `__onexit` chiama `__onexit_lk` da `0x406101`. Nessuna annotazione o salvataggio IDA eseguito.
- `./tools/link-arm.sh` ha exit 0. Il gate QEMU termina con SIGILL/exit 132 dopo il log `.rsrc`; nessun core file o processo QEMU rimasto.

## Gate
- MISSIONE 1: `./tools/link-arm.sh` -> exit 0; stdout verbatim:
  ```text
  creato: /home/enne2/dev/starcraft-armv6/build/starcraft-armv6
  ```
- MISSIONE 1: `timeout 60s qemu-arm-static -L build/sysroot build/starcraft-armv6` -> exit 3, non timeout; `SC_*` assenti. Output combinato stdout/stderr verbatim:
  ```text
  [arm-game] runtime ARMv6 — immagini tradotte, import statici
  [arm-game] header PE exe a 0x00400000
  [arm-game] header PE storm a 0x15000000
  [arm-game] cpu=0x0E000000
  [arm-game] TEB=0x0E000088
  [arm-game] PEB=0x0E001088
  [arm-game] bridge: 324 import, 33 UNK dichiarati
  [arm-game] entry: start() del CRT a 0x00404C21
  [arm-game-glue] loc_404C21 assente da StarCraft.o: stub, gioco non avviato
  [arm-game] l'entry e' tornata senza consumare il sentinella (pila sbilanciata)
  ```
- MISSIONE 2: `./tools/link-arm.sh` -> exit 0; stdout verbatim:
  ```text
  creato: /home/enne2/dev/starcraft-armv6/build/starcraft-armv6
  ```
- MISSIONE 2: `timeout 60s qemu-arm-static -L build/sysroot build/starcraft-armv6` -> exit 132 (SIGILL), non timeout; `SC_*` assenti. Output combinato stdout/stderr del programma verbatim:
  ```text
  [arm-game] runtime ARMv6 — immagini tradotte, import statici
  [arm-game] header PE exe a 0x00400000
  [arm-game] header PE storm a 0x15000000
  [arm-game] cpu=0x0E000000
  [arm-game] TEB=0x0E000088
  [arm-game] PEB=0x0E001088
  [arm-game] bridge: 324 import, 33 UNK dichiarati
  [arm-game] entry: start() del CRT a 0x00404C21
  [risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
  qemu: uncaught target signal 4 (Illegal instruction) - core dumped
  timeout: the monitored command dumped core
  ```
  La shell ha inoltre notificato: `/bin/bash: riga 1: 3613697 Istruzione non consentita  timeout 60s qemu-arm-static -L build/sysroot build/starcraft-armv6 2>&1`.
- Diagnostica non-gate: ultimo PC `0x004060C0`; IDA conferma che lì il binario x86 originale contiene `retn` (`__onexit_lk`, `0x406041–0x4060C0`). Non è output di gate.

## Decisioni autonome
- La copia esporta il nome C esatto `@loc_404C21`: definizione privata rinominata `@sc_loc_404C21_impl` e wrapper globale C-ABI che la chiama `fastcc`. Nessuna modifica a `game_main_arm.c` o al lift originale.
- La pipeline LLVM è via pipe per non creare `build/pipeline/*`, fuori dal perimetro autorizzato.
- Il fault mostra un salto a un VA PE originale; non aggiungo ancora wrapper a `__onexit_lk` prima di leggere l'IR e il chiamante originale, per evitare un'altra esportazione o destinazione inventata.

## Prossimo passo
Leggere in IDA per intero `__onexit` a `0x4060E9` e i relativi call/xref; confrontare con la sezione IR di `loc_406041` e col ramo che porta a `0x004060C1`, quindi correggere una sola causa nella copia IR, compilare, rilinkare e ripetere il gate.
