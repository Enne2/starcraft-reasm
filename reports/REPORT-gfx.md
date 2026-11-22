# Report — scx86-gfx (21/11/2026)

## Esito

Applicati due fix circoscritti: gestione stateful di `DirectDrawClipper` in `com_ddraw.c` e conversione bpp in `SetDIBits` in `gdi_elf.c`. Build privata ELF32 linkata e percorso Boot Camp→partita verificato; ordinato a un SCV di costruire un Supply Depot. La sessione `gfx-camp2` è stata chiusa con `run-isolated.sh stop`; non risultano processi StarCraft/Xvfb di questa prova.

## Fonti/oracolo

- PE originali: `build/lift-full/StarCraft.orig.exe`, `build/lift-full/storm.orig.dll`; ispezione richiesta eseguita con `llvm-objdump -d` (dump mirati conservati in `/tmp/gfx-storm-smack-objdump.txt` e `/tmp/gfx-storm-sdraw-objdump.txt`).
- IDA su copia temporanea di `storm.orig.dll`: `storm_457` a **0x1502F500** (decompilato per intero); wrapper `storm_453` a **0x1502E810**; loader Smack `sub_1502E430` a **0x1502E430**; kernel SCode `storm_334` a **0x15030AB0** (lettura completa). La sessione temporanea è stata chiusa. Non sono state modificate/chiuse le altre sessioni IDA che sono apparse durante la prova.
- `storm_457` chiama `SmackDoFrame` e `SmackNextFrame`, aggiorna la palette via `SmackBufferNewPalette` solo quando il flag palette del filmato è presente, e instrada la tavolozza/il blit attraverso SDraw (`storm_357`, `sub_1502EFA0`). `storm_334` riconfigura i kernel e relativi target SCode in base ai flag/descrittori passati; non è emerso un nuovo opcode non supportato nel percorso provato.
- Ricerca documentale API Smacker (MultimediaWiki, informazione comunitaria non primaria): https://wiki.multimedia.cx/index.php/RAD_Game_Tools_Smacker_API . La documentazione descrive l'output RGB16 attraverso i flag alti di `SmackToBuffer`, ma la build osservata passa `flags=0`; non è stata aggiunta una conversione speculativa.

## Diagnosi Smack e ritratti

Evidence runtime in `build/isolated/gfx-camp2/game.log`:

- i filmati SMK si aprono e avanzano; `ToBuffer` osservati usano destinazioni, pitch e altezza coerenti (es. `0xB8`, `0xD8`, `0xFC`) e non generano il log `ToBuffer flags non coperti`.
- `DoFrame` calcola hash identici tra frame decodificato e buffer di destinazione per i campioni strumentati; i campioni di pixel non sono tutti zero e la palette è presente. Un solo avviso `SmackSoundUseDirectSound` conferma l'instradamento al mixer host. Nessuna chiamata `SmackVolumePan` né `SmackBufferNewPalette` compare nel log di questa sessione; i filmati campionati dichiarano `track_mask=0`.
- `SmackToBuffer` resta quindi indicizzato 8-bit/flags=0 e fail-closed per flags non coperti. `smack_audio.c` già inoltra l'audio supportato e gestisce volume/pan; nessun cambiamento è giustificato dai dati (nessuna traccia audio nel materiale provato, VolumePan non invocato).
- `play6-07-bootcamp.png` (baseline) e `build/qa/gfx/26-briefing.png` sono stati letti con `read`: al briefing i riquadri restano perlopiù neri, un ritratto è visibile ma con scanline. Il decoder/buffer riceve dati validi, quindi la causa non è la mancata esecuzione di `SmackDoFrame` o un flag `ToBuffer` rifiutato. La corruzione/assenza a valle (palette blit/SDraw o contenuto/ordine dei portrait SMK) resta aperta: la prova non consente di attribuirla con certezza. Nessuna modifica di Smack/SDraw è stata fatta per non alterare il formato a tentativi.

## Fix applicati

### `runtime/shim-port/elf/com_ddraw.c`

- `Surf` mantiene un riferimento al clipper; `SetClipper` rimpiazza correttamente il riferimento e `GetClipper` restituisce `DDERR_NOCLIPPERATTACHED` se assente, altrimenti `AddRef` e `DD_OK`.
- Il clipper mantiene `HWND`, `RGNDATA` copiato, dimensione, presenza e stato changed. `SetClipList(NULL)` cancella; una lista non nulla viene validata (header/type, numero e dimensione rettangoli con limite 1 MiB) prima di copiarla. `GetClipList` restituisce la lista/size oppure `DDERR_REGIONTOOSMALL`; `IsClipListChanged` legge e azzera il flag; `SetHWnd` marca il cambiamento. Creazione inizializza tutti i campi, rilascio libera dati e oggetto.
- Il backend non interroga il rettangolo visibile della finestra e il percorso di rendering non è stato dimostrato usare un clipper: l'API conserva/espone la clip list, ma il clipping effettivo dei `Blt` e l'intersezione con il clip rect restano da testare.

### `runtime/shim-port/elf/gdi_elf.c`

- `SetDIBits` accetta le coppie supportate 8/24/32 bpp; copia diretta quando uguali, espande 8bpp usando `RGBQUAD` del DIB, converte 24/32bpp a 8bpp con nearest-color sulla palette della bitmap (fallback grayscale), e scrive RGB24/32 quando la destinazione è true-color.
- Stride DIB allineato a 4 byte; conserva il contratto di righe già usato dalla funzione. `GetDIBits`, `SetDIBitsToDevice` e il blit GDI avevano già conversioni separate.

## Build e prove isolate

- Compilati in `build/qa/gfx/`: `com_ddraw.o`, `gdi_elf.o`, `smack_elf.o`, `smack_audio.o`; oggetti modificati per i simboli `__c` con `shim_rename.txt`. Gli altri oggetti da `build/obj-x86/` sono stati usati solo in lettura.
- Link custom (equivalente a `tools/link-x86.sh`, output privato): `build/qa/gfx/starcraft-x86-gfx`, verificato ELF32/EXEC/Intel 80386. Un primo link con i nomi non ridefiniti ha fallito; corretto con `llvm-objcopy --redefine-syms=runtime/shim-port/shim_rename.txt` sugli oggetti privati.
- Avvio solo tramite `SC_BIN=$PWD/build/qa/gfx/starcraft-x86-gfx ./tools/run-isolated.sh start ...`; Xvfb dedicato e `SDL_AUDIODRIVER=dummy`, nessuna finestra visibile o audio reale.
- `gfx-camp2`: menu→Single Player `(220,120)`→Original `(260,226)`→profilo `(100,109)`/OK `(517,391)`→Terran `(320,250)`→story `(320,440)`→Boot Camp `(320,440)`→Start `(538,390)`→Tips OK `(197,262)`. Screenshot verificati con `read`: `build/qa/gfx/21-main.png`, `22-registry.png`, `23-campaign.png`, `24-terran-story.png`, `25-bootcamp-title.png`, `26-briefing.png`, `27-start.png`, `28-game.png`, `29-ingame.png`.
- Gameplay: SCV identificato in `(289,207)`; command card build `(522,455)`, hover `(568,375)` identificato come Supply Depot da tooltip; selezione `(568,375)` e piazzamento `(350,250)`. Screenshot `build/qa/gfx/36-candidate.png`, `39-hover-middle.png`, `40-depot.png`, `41-depot-progress.png`: minerali scendono da 150 a 50, SCV resta selezionato sul sito e appare la fase di costruzione. Obiettivo «Build a Supply Depot» verificato; Refinery non costruita (risorse insufficienti e il task richiedeva un Depot/Refinery).
- Menu in-game aperto e letto: `build/qa/gfx/42-menu.png`; ritorno in gioco (`43-after-menu.png`). Nessun `STOP`, `unsupported`, `SIGSEGV` o flag Smack non supportato nel log della prova. `Snp spiReceive` resta non implementata come già noto, senza bloccare la partita.
- Campagne Zerg e Protoss provate sul primo percorso: click `(125,330)`/conferma `(235,314)` mostra “ZERG CAMPAIGN: OVERMIND” (`08-zerg-confirm.png`); click Protoss `(540,330)`/conferma mostra “PROTOSS CAMPAIGN: THE FALL” (`13-protoss.png`). Questi sono intro della campagna, non avvio missioni. Primo tentativo successivo Terran ha mostrato errore `standard.snp`; percorso Boot Camp è stato ripetuto da sandbox fresca `gfx-camp2` ed è riuscito.
- Chiusura `./tools/run-isolated.sh stop --id gfx-camp2`; successivo `status` ha dato `game_alive=0`, `xvfb_alive=0`. Anche `gfx-camp` iniziale è stato fermato; nessun processo `starcraft-x86` vivo.

## Limiti e verifiche residue

1. Aggiungere test mirati per `RGNDATA`/refcount e conversione `SetDIBits` 8↔24/32, inclusi RGBQUAD e palette vuota. In questa sessione la verifica è compilazione + link + gioco, non unit test delle nuove API.
2. Verificare IDirectDrawClipper con un caller reale e applicare clip list ai Blt solo dopo aver misurato il contratto richiesto.
3. Isolare perché solo alcuni portrait sono visibili, confrontando palette finale in `storm_457`/`storm_357` e l'ordine dei filmati SMK effettivamente disegnati.
4. Non è stata raggiunta la Refinery; solo l'ordine di costruzione Supply Depot è confermato.

## Decisioni autonome

- Nessun supporto inventato per `SmackToBuffer` flags non osservati, audio non presente o opcode SCode non incontrati; `proc_range`, controlli espliciti e comportamento fail-closed D19 sono rimasti invariati.
- Clipper gestisce liste fornite dall'app e l'HWND senza interrogare il desktop host; niente input desktop. In assenza di prove di uso del clipper da parte del gioco, la verifica si è fermata alla partita reale e non a una simulazione di clipping.
