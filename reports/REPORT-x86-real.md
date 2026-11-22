# Report — x86 SDL2 reale e STOP `0x1805F2D4`

## Esito sintetico

- **SDL reale sbloccata scegliendo Wayland**: con `SDL_VIDEODRIVER=wayland` la finestra è creata, il renderer GPU parte, DirectSound apre audio reale su PipeWire e il log registra FPS. La prova completa di 5 s è `build/qa/run-x86-real-sdl.log`.
- **X11 resta bloccato**: il backtrace raccolto attachandosi al processo reale colloca il main thread dentro `SDL_CreateWindow` → SDL2-compat/SDL3 `X11_ShowWindow` → `XIfEvent` → lettura eventi XCB (`poll`). Il thread non è in `SDL_Init` né nell'audio. `SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=0` non cambia il blocco.
- **Audio 32-bit presente e funzionante**: `/lib/libpulse.so.0` e `/lib/libpipewire-0.3.so.0` sono ELF i386; il run effettivo usa `driver = pipewire`, 22050 Hz, stereo. Non serve il fallback dummy.
- **STOP identificato come invocazione di callback fuori mappa durante il drenaggio on-exit**: nel binario originale `0x00404F09` decremente il puntatore globale `0x006DD684`, carica una callback e la chiama indirettamente a `0x00404F24`; `0x00404F26` è la continuazione/loop. Il valore invocato `0x1805F2D4` è oltre il codice runtime presente (sezione `.text` del port `0x18000000..0x1803A435`), non compare tra i simboli ELF e non è una VA guest StarCraft/Storm. Non è possibile stabilire dai dati raccolti quale registrazione abbia inserito quel valore: **non aggiunto alcun target** e resolver D19 invariato.
- Non è dimostrato che il gioco abbia raggiunto il menu principale prima dello STOP: nel run di 5 s ci sono FPS e animazioni SMK, poi lo STOP. Quindi la richiesta finestra/audio/FPS è soddisfatta, ma il criterio “FPS nel menu principale” resta non dimostrato.

## Diagnosi SDL e prove

Il comando GDB del report bringup6 si fermava al primo breakpoint `host_elf_display_mode` e terminava il batch: così non poteva mostrare lo stack del blocco. Ho proseguito il processo reale e fatto `gdb -p` dopo 3 s.

Stack del main thread:

```text
poll
xcb_wait_for_event
_XReadEvents
XIfEvent
X11_ShowWindow              (libSDL3.so.0)
SDL_ShowWindow_REAL         (libSDL3.so.0)
SDL_FinishWindowCreation    (libSDL3.so.0)
SDL_CreateWindowWithProperties_REAL (libSDL3.so.0)
SDL_CreateWindow_REAL       (libSDL2-2.0.so.0)
host_elf_display_mode       (host_elf.c:390)
d_SetDisplayMode           (com_ddraw.c:584)
```

La prova X11 precedente conferma driver `x11` e poi nessun avanzamento; override del hint bypass compositor non la sblocca. Questo localizza il blocco nella presentazione/attesa eventi X11 della compatibilità SDL2→SDL3, ma non dimostra perché il server/WM non consegni l'evento atteso. L'alternativa conservativa verificata è il backend SDL Wayland nativo: non è stato cambiato codice SDL.

### Run di accettazione (5 s)

Comando eseguito da `build/asset-sandbox`:

```sh
SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=wayland \
timeout -k 1s 5s ../starcraft-x86
```

Nel log `build/qa/run-x86-real-sdl.log` sono verificati:

- `[host] init: video driver = wayland` e `[host] finestra creata`;
- renderer GPU attivo;
- `[host] audio aperto: driver = pipewire, ... canali = 2`;
- `SmackOpen` e quattro righe `[fps]`;
- exit da timeout 124, con STOP fail-closed `0x1805F2D4` prima del completamento dei 5 s.

Il run è reale, non dummy. L'assenza di un marker esplicito del menu principale e lo STOP impediscono di certificare quel sottocriterio.

## Analisi dello STOP

Dal log `build/qa/run-x86-bringup6-smoke.log` (394 righe) risultano 9 righe `[fps]` prima dello STOP, l'ultima a riga 305. Nel nuovo run Wayland lo STOP avviene dopo due righe FPS (poi le righe FPS successive sono nel run storico bringup6; nel log nuovo il contatore è visibile prima del dump STOP).

Evidenze sul PE originale, verificate con `llvm-objdump` poiché `ida_open` del database `StarCraft.orig.exe.i64` non è riuscito:

- `0x00404F09..0x00404F26`: carica la testa dell'elenco globale a `0x006DD684`, la arretra di 4 byte, controlla il limite `0x006DD688`, prende la callback e fa `call *%eax`; `0x00404F26` torna al loop.
- `0x004FDE70`: salva `EBX`, imposta `EBX=0x0051A334`, chiama `0x00401970`, quindi prosegue a `0x004FDE7B`.
- `0x00401970`: percorre una lista collegata a partire da `[EBX+8]`, scollega i nodi e azzera i campi dei nodi rimossi. `0x004FDE7B` riprende i globali `0x0051A338/0x0051A33C` e continua la routine.
- Al dispatcher il valore tentato è `EAX=0x1805F2D4`; la continuazione guest è `0x00404F26`. L'ELF linked ha `.text` a `0x18000000`, dimensione `0x3A435`, e `llvm-nm` non trova `0x1805F2D4`. Il valore non è un target tradotto presente nel binario.

Le tracce del dispatcher mostrano anche le continuazioni `0x004FDE70/0x004FDE7B`, compatibili con il percorso di cleanup/lista, ma non rivelano chi abbia scritto `0x1805F2D4` nell'elenco delle callback. Non si attribuisce quindi la causa a un generico bug della tabella di uscita senza prova della registrazione. Risolvere richiede tracing della scrittura/registrazione che inserisce il valore, non un alias o un target arbitrario.

## File e modifiche

- Creato `build/qa/run-x86-real-sdl.log` (output integrale, incluso il dump fail-closed).
- Creato questo report e `reports/STATE-x86-real.md`.
- Nessun sorgente o resolver modificato; nessun comando git o input desktop.

## Decisioni autonome

- Ho scelto `SDL_VIDEODRIVER=wayland`, il driver che ha superato la prova effettiva con finestra, audio reale e FPS; non ho alterato il codice per aggirare l'attesa X11, che non è ancora spiegata al livello dell'evento mancante.
- Ho lasciato il dispatcher fail-closed e non ho aggiunto `0x1805F2D4` alla mappa: la sua provenienza non è dimostrata.
- Ho dichiarato il menu principale non verificato anziché dedurlo dai soli FPS/SMK.

## Limiti / gate residui

- Causa localizzata dell'hang X11: attesa bloccante in `X11_ShowWindow`/`XIfEvent` attraverso SDL2-compat (con backtrace reale); causa specifica dell'evento X11 mancante non isolata.
- Finestra Wayland, PipeWire e FPS verificati nel run di 5 s.
- STOP `0x1805F2D4` localizzato al callback indiretto del loop `0x00404F09`; writer/provenienza del callback ancora da trovare.
- Menu principale nel run reale di 5 s non certificato.
