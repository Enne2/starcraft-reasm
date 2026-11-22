# SMACK ARMv6 — fix `lib_load()` e verifica

## Perimetro

Modificati solo `build/runtime-arm/shim-port/tools/elf/smack_elf.c`, il relativo oggetto compilato e questo report. Non modificati `win32_shim.c`, `win32_ui.c`, `com_ddraw.c`, `com_dsound.c`, il guest o i sorgenti in `~/dev/third_party/libsmacker/`.

## Fix applicato

In `lib_load()` il percorso ottenuto da `/proc/self/exe` veniva concatenato senza copiare il prefisso, lasciando `exe_so[0..n-1]` non inizializzato. Ora copia `exe_path[0..n-1]` prima del suffisso, verifica che il path completo entri nel buffer e ignora `SC_SMACKER_SO` se è una stringa vuota. Se un candidato si apre ma non espone tutti i simboli richiesti, viene chiuso e si prova il successivo, azzerando i puntatori parzialmente risolti. Ordine: `SC_SMACKER_SO`, `./libsmacker-1.2.0.so`, directory dell’eseguibile, path host `build/arm-libs`.

## Build e gate

- Oggetto `smack_elf.o` compilato per ARMv6 hard-float; hash SHA-256 `20eb111e5a4eb07f5710f45e8d2c3a9950d9de8df596fe37ec7d024d8867fefe`.
- Link runtime-only con `TMPDIR=/dev/shm` (necessario per evitare l’errore di scrittura LLVM su `/tmp`): `build/starcraft-armv6-smk2`.
- SHA-256 candidato: `a78a9f33571949eccc40b2c6279e905d1f6caf80a729931db834fdb009663727`.
- `TMPDIR=/dev/shm make check`: PASS — test ARMv6 sotto QEMU, gate ordinale PE/Wine e bridge CPU/stack guest.

## Smoke `SC_SMACK=1`

Eseguito da `build/asset-sandbox`, con `ulimit -c 0`, `TMPDIR=/dev/shm`, `SDL_VIDEODRIVER=dummy` e `timeout -k 2s 15s qemu-arm-static -cpu arm1176 -L ../../build/sysroot ../../build/starcraft-armv6-smk2`.

- Exit QEMU: 71, non timeout.
- Log completo: `build/qa/run-smk2-on.log`.
- Evidenza positiva: `[smack] libsmacker caricata: /home/enne2/dev/starcraft-armv6/build/arm-libs/libsmacker-1.2.0.so`; viene chiamata `SmackOpen(arg=0x13, flags=0x000FF000)`, legge `0x25131C` byte da un HANDLE (una entry), riconosce `SMK2`, 320×308, 171 frame, e completa `SmackBufferOpen`/`SmackToBuffer`.
- Non è ancora arrivato al menu. Il successivo arresto è `STOP: salto indiretto a un indirizzo non tradotto`, target `0x20170A9C`. L’origine di questo target non è determinata in questa verifica e resta il prossimo blocco.
- Il modulo audio separato continua a segnalare un `dlopen` fallito sul vecchio path i386 (`[smack-audio]`); il decoder video SMACK invece carica e apre il filmato.

## Oracolo IDA: perché `SC_SMACK=0` mostrava il dialogo

Analizzati in sola lettura i soli database indicati, senza rename/commenti/patch:

- `storm.lift.dll.i64`, sessione `3b5d2e3d`: `storm_454` a `0x1502EA90`; chiamata indiretta a `SmackOpen` tramite tabella a `0x1502ED71`. Se il risultato è nullo, il ramo `LABEL_99` chiama `storm_458` (`0x1502EF8B`) per cleanup e ritorna 0 (`0x1502EF93`).
- `StarCraft.orig.exe.i64`, sessione `61ba8fcf`: `sub_4D4470` a `0x004D4470` seleziona `off_512690[index]`, passa un output handle a `storm_454` (`0x004D44C5`) e testa il risultato a `0x004D44CD`. Con risultato zero, gli indici 1 e 24 sono eccezioni; gli altri invocano `sub_4212C0` (`0x004D4538`). La string table prova che indice 0 è `smk\\blizzard.smk` (`0x00502208`). `sub_4212C0` formatta il messaggio e apre il dialogo modale; è quindi il chiamante del gioco a trattare il fallimento di apertura come errore per `blizzard.smk`, non uno skip.

Conclusione circoscritta: il vecchio smoke ARM con `SC_SMACK=0` restituiva `SmackOpen=0`, `storm_454` falliva e il gioco mostrava il dialogo per l’indice 0. L’implementazione x86 che saltava i filmati non dimostra la stessa semantica nel percorso ARM con la tabella di thunk attiva. Non ho modificato il comportamento `SC_SMACK=0` oltre il perimetro autorizzato.

## Decisioni autonome

Conservato il percorso di skip inattivo richiesto senza intervenire sul guest o sul codice del gioco: l’evidenza IDA mostra che tale ritorno zero non è uno skip pulito per il filmato indice 0. Non dichiaro raggiunto il menu: il candidato ora supera l’apertura/decodifica del filmato SMK2, poi si ferma sul target non tradotto `0x20170A9C`. Le due sessioni IDA aperte per questa verifica sono state chiuse senza annotazioni.