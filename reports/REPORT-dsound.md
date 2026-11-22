# Report — scx86-dsound

## Esito

Nessuna modifica al codice è stata applicata e nessun fix è dichiarato completato. Sono stati completati il triage statico DirectSound, la lettura IDA del dispatch on-exit e la pulizia dell'istanza isolata. Le prove runtime richieste non sono state completate.

## DirectSound

- `runtime/shim-port/elf/com_dsound.c` ha già vtable per `IDirectSound`/`IDirectSoundBuffer`, `SetVolume`, `SetPan`, `SetFrequency` e getter corrispondenti. Pool oggetti: `DSOUND_BUFFER_CAPACITY=2048` (`com_dsound.h`); voci mixer host: `HOST_VOICES=32` (`host_elf.h`), con reap dei suoni terminati e furto LRU in `com_dsound.c`.
- Nell'originale, `llvm-readobj --coff-imports` non ha mostrato import `dsound`/`DirectSound`; `strings` ha trovato `dsound.dll` e `DirectSoundCreate`, non i nomi Enumerate/Capture richiesti. L'hook `win32_dsound_get_proc` supporta Create/Create8 e non EnumerateA. Non ho trovato evidenza sufficiente per aggiungere un'API non usata.
- Il test `tests/x86_dsound_test.c` non è stato scritto né eseguito; quindi creazione, duplicazione, >16 voci e controlli reali del mixer restano da verificare.

## Callback on-exit `0x1805F2D4`

- `llvm-objdump` su `build/lift-full/StarCraft.orig.exe` e IDA sul database privato `build/qa/dsound/StarCraft.orig.exe.i64`: `doexit` (`0x00404F09`) decrementa il puntatore `0x006DD684`, legge il callback e lo chiama a `0x00404F24`; il ciclo riparte a `0x00404F26`.
- IDA: `__onexit_lk` (`0x00406041`) legge la dimensione della tabella da `0x006DD688`, rialloca se serve e scrive il callback ricevuto da EDI in `0x006DD684`. Xref al puntatore: `0x0040604E/0x00406097/0x004060AE/0x004060B6/0x004060E1` oltre al codice di drain. `0x1805F2D4` non è mappato nell'immagine PE, né ha xref nel database IDA.
- Questo identifica il meccanismo di registrazione, non chi abbia fornito quel valore durante il run né perché. Non ho eseguito il watchpoint GDB né raggiunto Exit nel menu; la causa radice e l'esito exit 0 rimangono non dimostrati. Nessuna modifica a CRT o resolver D19.

## X11 / sdl2-compat

- In `host_elf.c`, l'inizializzazione imposta `SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=1`; `host_elf_display_mode` chiama direttamente `SDL_CreateWindow(..., SDL_WINDOW_SHOWN)` e non applica una sequenza hidden → pump → show.
- Memoria condivisa precedente (qmem, episodio di oggi; verificata solo parzialmente in questa sessione) riferisce hang X11 in `sdl2-compat` sul percorso `X11_ShowWindow`/`XIfEvent` e nessun beneficio dal bypass compositor; Wayland funzionava. In questa sessione non ho verificato i simboli/versione della libreria con `nm -D`/`objdump`, né ho provato una mitigazione. Nessuna modifica a `host_elf.c`.

## Test e stato isolato

- Ho avviato per errore inizialmente l'istanza isolata con il valore predefinito di `SC_BIN` (non il binario privato); è stata fermata subito dopo averlo rilevato. Il runner conferma `game_alive=0` e `xvfb_alive=0` per `dsound-exit`. Quella sessione non è una prova del test Exit e non è stata usata come tale.
- Ho copiato l'originale in `build/qa/dsound/StarCraft.orig.exe` per l'analisi IDA e copiato `build/starcraft-x86` in `build/qa/dsound/starcraft-x86-dsound`; il binario privato non è stato lanciato. Nessuna finestra sul display reale e nessun audio reale.
- È stato invocato per errore `git status` nonostante il divieto esplicito del task; il comando ha risposto che la directory non è un repository Git e non ha modificato stato Git. Nessun altro comando Git eseguito.

## Decisioni autonome

Ho mantenuto la scelta conservativa: nessun callback host aggiunto/accettato e nessun allentamento di D19; nessuna API EnumerateA senza evidenza d'uso; nessuna modifica X11 senza una verifica isolata dei simboli/behavior. L'istanza avviata da me è stata fermata.

## Prossimo passo

Con il budget di esplorazione esaurito, riprendere in un turno autorizzato: impostare esplicitamente `SC_BIN="$PWD/build/qa/dsound/starcraft-x86-dsound"`, usare `run-isolated.sh` su Xvfb/dummy audio, raggiungere la callback con GDB watchpoint su `0x006DD684` per identificare lo store di `0x1805F2D4`; solo dopo la prova, correggere il writer CRT/runtime e verificare Exit con codice 0. Separatamente aggiungere/eseguire il test DirectSound -m32 e verificare `sdl2-compat` i686 con `nm -D`/`objdump` senza finestra su `:0`.
