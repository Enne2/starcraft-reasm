# Report — ARM SDraw / JIT blitter

## Stato iniziale

- Obiettivo: impedire che il guest ARMv6 salti nel blitter x86 dinamico di Storm (`0x20170A9C`) e correggere la ricerca di `libsmacker` nel path audio.
- Evidenza contestuale fornita dall’utente: `smk\blizzard.smk` viene aperto/decodificato da libsmacker ARMv6; il flusso poi salta a `0x20170A9C`. Il path audio tenta ancora `build-i386/libsmacker-1.2.0.so`.
- Memoria condivisa consultata: annota lo stesso arresto dopo `SmackToBuffer`, prima del menu; non costituisce verifica attuale.
- Piano autorizzato: esaminare il solo DB IDA del progetto, ricostruire la semantica prima di implementare override ARM, correggere la ricerca audio, quindi build/check/smoke con `TMPDIR=/dev/shm`.
- Vincoli: nessuna modifica a `win32_shim.c`, `win32_ui.c`, `com_ddraw.c`, `com_dsound.c`, `smack_elf.c`, `code_entry_points.sci`, guest o repository esterni.

## Avanzamento

### IDA — verifiche eseguite

Database del progetto `build/lift-full/ida/storm.lift.dll.i64`, sessione `1adcd661` (nessun DB altrui toccato). `0x150362E3` ricade in `storm_313`, inizio `0x15036260`, dimensione `0x169`; l’ordinale identificativo è 313. Xref: chiamanti diretti includono `storm_210` (0x15010F60), `storm_221` (0x15011540), `storm_201` (0x150119C0), `storm_358` (0x15035390), `storm_342` (0x15035620), `storm_315` (0x150364A0) e `storm_314` (0x150365D0). Decompilato per intero a `0x15036260`.

`storm_313` (0x15036276–0x150363C6) valida dimensioni; per ROP `0xCC0020` o `0xF00021`, allineamenti `(a4|a2)&3==0` e `a4<=0x280`, prende da `sub_150015C0(&stru_1505EBEC)` un kernel indicizzato con `a4>>2`, quindi salta indirettamente via `ebx` (`jmp ebx`, 0x1503631F). Il blocco tradotto a `0x15036330` è parte del kernel/gateway. Altrimenti prepara una struttura locale (flags `0x40`, coordinate/dimensioni e ROP) e chiama `storm_334` a `0x150363BB`.

`storm_334` (0x15030AB0) è il dispatcher per il loop configurabile: la struttura riceve width/height e stride; applica allineamento a 4 byte, sposta gli ingressi interni di `a1`, poi carica registri da `a2` e tail-jumpa al kernel a `0x15030D2A`. Le routine template statiche `0x15030D30`/`0x15030D50` fanno il loop esterno, aggiornano ESI/EDI e ritornano 1 via `retn 8` a `0x15030D9D`. `storm_313` termina `retn 0x20` a `0x150363C6` (8 argomenti sullo stack; ECX è il `this`), mentre `storm_334` è il target di chiamata a due argomenti con pulizia di 8 byte nel template. I wrapper esterni dovranno quindi rispettare questi due stack contract.

**Audit allocazioni eseguibili IDA:** Storm ha 10 xref di `VirtualAlloc`; gli unici percorsi con `flProtect=0x40` sono `sub_1501FD20` (0x1501FD20: reserve PAGE_NOACCESS e commit PAGE_EXECUTE_READWRITE) e `sub_15020A00` (0x15020C5F: commit executable per SCode). Gli altri xref sono memoria dati (protezione 4). L’eseguibile StarCraft (DB progetto `build/lift-full/StarCraft.orig.exe.i64`, aperto read-only) ha cinque xref VirtualAlloc, tutti CRT (`___sbh_alloc_new_region`, `___sbh_alloc_new_group`, `__resetstkoflw`, `sub_41EB80`, `sub_41EF00`), nessun JIT blitter apparente.

`0x1502325A` non è inizio funzione: ricade in `storm_388` (entry `0x15023210`). `storm_388` invoca `storm_313` a `0x150232BB` con ROP `0xF00021` (PATCOPY). Il call site osservato immediatamente prima del JIT nella traccia è invece `storm_358` (entry `0x15035390`), call a `storm_313` da `0x15035506` con ROP `0xCC0020` (SRCCOPY). La chiamata di `storm358` passa 8 argomenti stack e `ECX=480`; il JIT ottimizzato in `storm313` carica registri e stride direttamente dal frame. `storm_342` è capture PCX, non il chiamante runtime.

La sostituzione proposta dall’utente è strutturalmente possibile: `external_procedures.sci` instrada le procedure lifted verso simboli C/ASM, `--map-extra` può puntare il mapping VA alla entry assembly, e `reasm_extern` gestisce il ritorno guest. Nota ABI: una wrapper che passa da `reasm_extern` deve lasciare il return address sullo stack perché `reasm_extern` stesso lo poppa e fa il branch; i body devono soltanto adeguare lo stack degli argomenti (`0x20` byte per storm313, 8 byte per storm334). Non va poppato una seconda volta il return nel body.

**Semantica pixel ancora non dimostrata:** `storm_313` sceglie un kernel da una tabella inizializzata a runtime; `storm_334` riusa `a1` come oggetto SCode/JIT, patcha gli entrypoint/offset e carica EDI/ESI/EBP/ECX/EDX/EBX dalla struct prima del salto. I template statici descrivono solo l’iterazione/stride/epilogo, non il corpo per-pixel del kernel allocato. Il dispatcher non contiene una tabella ROP né la formula del singolo pixel. `VirtualAlloc` RWX porta ai generatori SCode `sub_1501FD20`/`sub_15020A00`, e dal call graph esaminato non risulta un corpo di blit statico da cui copiare. Non basta inferire PATCOPY/SRCCOPY dai codici ROP per implementare con evidenza IDA esatta il layout/operando pattern e tutte le varianti. `storm_354` (0x15034C20) aggiorna palette; `storm_356` (0x15034740) unlocka superfici DirectDraw; `storm_357` (0x15034EC0) aggiorna palette.

**Limite implementativo emerso:** non è ancora ricostruita la semantica pixel completa del kernel allocato: argomenti e layout superfici dell’API, pitch/clipping/ROP e casi generici non sono sufficientemente determinati dalla sola routine `storm_313`. Il runtime attuale risolve gli ordinali Storm a VA originale tramite `arm_bridge_ordinal_va()` (`runtime/arm/bridge_arm.c:252`); le voci `ENTRY` della tabella bridge sono thunk guest-ABI per gli shim e non sostituiscono automaticamente un ordinale Storm. Un override improvvisato o una copia lineare rischierebbe corruzione memoria o output errato. Non applicherò un blitter ARM basato su supposizioni; continuo l’analisi del flusso di superfici e in parallelo correggo il path audio.

`git status` non disponibile: il percorso `/home/enne2/dev/starcraft-armv6` non contiene `.git` (verificato con `find`), ma i file di lavoro sono presenti.

## Modifica eseguita — lookup libsmacker audio

Aggiornato `build/runtime-arm/shim-port/tools/elf/smack_audio.c`: usa `SC_SMACKER_SO` (ignorando valore vuoto), `./libsmacker-1.2.0.so`, directory dell’eseguibile ricavata da `/proc/self/exe`, poi path ARMv6 assoluto nel progetto. Se un candidato manca simboli, chiude la libreria e prova il successivo; logga il fallimento finale. Ricompilato `build/runtime-arm/shim-port/obj/smack_audio.o` con clang ARMv6 hard-float e `TMPDIR=/dev/shm` (successo, exit 0). La verifica `strings build/starcraft-armv6-sdraw` conferma nel nuovo ELF `SC_SMACKER_SO`, `./libsmacker-1.2.0.so` e il path assoluto previsto; il vecchio path `build-i386` non è presente.

## Build / test

- `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-sdraw`: completato, ELF ARM32 creato. Il runtime era tutto in cache (15 hit, 0 ricompilazioni); il link ha usato l’oggetto `smack_audio.o` ricompilato prima.
- `TMPDIR=/dev/shm make check`: PASS (toolchain, freestanding/QEMU, slice/runner e bridge ordinal/ARM1176).
- Smoke richiesto: da `build/asset-sandbox`, `TMPDIR=/dev/shm SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, core dump disabilitato, timeout 20s; log `build/qa/run-sdraw.log`. Il gioco ricarica la libreria Smacker, arriva a `storm_313` (`entry VA 0x150362E3`), poi termina `exit=71` saltando ancora a `0x20170A9C`. Lo stack mostra `0x280` e i valori già riportati, quindi nessun progresso fino al menu/frame successivo.

## Decisioni autonome

Scelta conservativa: non introdurre wrapper `storm_313`/`storm_334` che restituiscano success senza eseguire l’operazione o che approssimino il codice JIT con un semplice `memcpy`/ROP bytewise. Gli indirizzi IDA studiati dimostrano il dispatcher e la gestione del frame, ma non la semantica del kernel runtime specifico; il menu/immagini errati o superfici corrotte sarebbero un falso successo.

## Esito / limite

Il fix path audio è implementato e verificato nel binario. **Il blocco JIT blitter non è risolto**: nessun override pixel ARM è stato aggiunto. IDA dimostra che il candidato ovvio `storm_313` (`0x15036260`) ha sia il ramo JIT (`jmp ebx`, `0x1503631F`) sia un ramo generico verso `storm_334` (`0x15030AB0`, `jmp eax` a `0x15030D2A`); non basta un override dell’ordinale 313. Mancano ancora layout/semantica completi delle superfici e del kernel JIT per implementare un sostituto conservativo. Mantenere un blitter inventato avrebbe violato il requisito di semantica IDA esatta. Perciò il report separa esplicitamente il fix audio completato dal JIT rimasto bloccato.
