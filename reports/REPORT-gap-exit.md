# Report — scx86-gap-exit

## Esito sintetico

- Creato il binario privato `build/qa/gap-exit/starcraft-x86-exit` copiando `build/starcraft-x86`; istanza avviata solo tramite `tools/run-isolated.sh` su Xvfb `:132` e audio SDL dummy.
- Il click sintetico autorizzato su Exit ha portato al drain on-exit, ma **non** all'uscita pulita: il runtime si è fermato fail-closed su `0x004FDE60` (caller guest `0x00404F26`). Non è stato osservato `0x1805F2D4` in questa build. D19 non è stato allentato e nessun fix a resolver/CRT è stato applicato.
- Aggiunto ed eseguito `tests/x86_dsound_test.c`: PASS in build `clang -m32`, con 40 buffer secondari e pool host simulato da 32 voci.
- Ispezione SDL e run isolato: SDL2 compat è disponibile; `host_elf.c` crea direttamente la finestra con `SDL_WINDOW_SHOWN`, senza `SDL_HideWindow`/`SDL_PumpEvents`/`SDL_ShowWindow`. Il run Xvfb raggiunge la schermata di menu e crea/presenta la finestra. Nessuna modifica a `host_elf.c`.
- L'istanza è stata chiusa con `run-isolated.sh stop`; nessuna finestra su `:0` e nessun audio reale.

## On-exit: evidenza e diagnosi

### Oracolo statico

`llvm-objdump` su `build/lift-full/StarCraft.orig.exe`:

- `0x00404F09–0x00404F26` (`doexit`): carica e decrementa il puntatore `0x006DD684`, legge il callback e lo chiama indirettamente a `0x00404F24`, quindi ripete.
- `0x00406041` (`__onexit_lk`): gestisce l'array delimitato dai globali `0x006DD684`/`0x006DD688`; il punto di scrittura del callback è `movl %edi,(%ecx)` a `0x004060B4`, seguito dall'incremento del puntatore a `0x004060B6`.
- Il caller osservato nel codice guest è `0x004FD3A0`: `push 0x004FDE60` a `0x004FD3AB`, poi call al wrapper `_onexit` a `0x004FD3B0` (destinazione `0x00406121`). Il valore passato e poi scritto dal writer CRT è quindi `0x004FDE60`.
- `0x004FDE60` è codice valido nell'immagine PE: `push 0x006D62B8; call [0x004FE204]; ret` (disassemblato fino a `0x004FDE6B`). La callback non è un valore arbitrario prodotto dal CRT.

### Prova runtime

- Screenshot prima dell'input: `build/qa/gap-exit/before.png`; Exit è a circa `(510,328)` nel frame 640×480.
- Watchpoint GDB su `*(unsigned int*)0x006DD684` è stato attivato. Il primo hit osservato dopo il click era la normale decrementazione eseguita da `doexit` (`loc_404F09`, host PC `0x0801AD9F`), non la scrittura del callback: il valore passò da `0x20180C4C` a `0x20180C48`.
- Log completo: `build/isolated/gap-exit/game.log`. Esito: `[arm-dispatch] STOP`, target `0x004FDE60`, caller `0x00404F26`, `in mappa = 1` (memoria guest mappata; non significa entry presente nella tabella dispatch). Nessun `STOP 0x1805F2D4`.
- La cattura del watchpoint non ha registrato dinamicamente lo store iniziale del callback: il processo era già oltre l'inizializzazione quando GDB è stato collegato. Il writer e il dato sono identificati staticamente dai due siti sopra e il valore è confermato dal target del drain.

### Root cause e fix disposition

La causa non è un writer che genera `0x1805F2D4` in questa build: il vero writer CRT memorizza il callback guest valido `0x004FDE60`, fornito dal codice PE a `0x004FD3AB`. Il problema concreto è che la tabella di dispatch del binario avviato non risolve questo entry guest, benché il relativo codice esista nell'immagine. L'unico comportamento sicuro è mantenere il fail-closed. **Non** ho aggiunto whitelist, fallback a chiamata diretta o target indovinati.

Il fix va applicato alla generazione delle entry tradotte: promuovere il corpo PE `0x004FDE60–0x004FDE6B` a funzione tradotta, produrre l'entry guest `0x004FDE60` nella tabella `reasm_va_va/reasm_va_fn`, e ricostruire/verificare che il resolver restituisca tale entry. La patch non è stata applicata perché richiede interventi a `tools/dispatch-ir.py`/input della pipeline fuori dai file di proprietà del task; `--map-extra` non è una soluzione da solo, perché non esiste nel modulo attuale un simbolo tradotto da associare a quella VA. Serve una build rigenerata e poi un nuovo run Exit end-to-end. Il requisito exit 0 resta non soddisfatto.

Il riferimento richiesto `0x1805F2D4` non compare nei log o nel disassemblato esaminato per questo run. Non è corretto dichiararlo risolto né sostituirlo con un target permissivo.

## Test DirectSound

File: `tests/x86_dsound_test.c` (nuovo). Build isolata: `build/qa/gap-exit/x86_dsound_test`, stub logger in `build/qa/gap-exit/reasm_elf.h`, output compilazione in `build/qa/gap-exit/dsound-build.log`.

Comando verificato:

```sh
clang -m32 -std=c11 -O0 -g -fno-pie -no-pie \
  -I build/qa/gap-exit -I runtime/shim-port/elf \
  runtime/shim-port/elf/com_dsound.c tests/x86_dsound_test.c \
  -o build/qa/gap-exit/x86_dsound_test
build/qa/gap-exit/x86_dsound_test
```

Esito: `x86_dsound_test: PASS (DirectSound, buffer APIs, 40 buffers, 32-voice LRU)`.

Copertura: `DirectSoundCreate`, `SetCooperativeLevel`, buffer primario/secondari, 40 buffer secondari, `DuplicateSoundBuffer`, lock wrapping e unlock, play/stop/status, volume/pan/frequency set+get, release/shutdown. L'host mixer è uno stub in memoria: nessun dispositivo audio reale viene aperto. Il test esercita il furto LRU oltre le 32 voci; verifica `voice_steals > 0` e `voice_fail == 0`.

## X11 / SDL2 compat

- `/lib/libSDL2-2.0.so.0` risolve a `libSDL2-2.0.so.0.3200.74`; `readelf -d` riporta SONAME `libSDL2-2.0.so.0` e dipendenza `libc.so.6`. `nm -D` espone `SDL_HideWindow`, `SDL_PumpEvents`, `SDL_ShowWindow`.
- `/lib/libSDL3.so.0` risolve a `libSDL3.so.0.4.20`; SONAME `libSDL3.so.0`, dipendenze `libm.so.6` e `libc.so.6`; `nm -D` espone gli equivalenti SDL3 `SDL_HideWindow`, `SDL_PumpEvents`, `SDL_ShowWindow`.
- In `runtime/shim-port/elf/host_elf.c`, `host_elf_init` imposta `SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=1`; `host_elf_display_mode` chiama `SDL_CreateWindow(..., SDL_WINDOW_SHOWN)`. Non esiste la sequenza hidden → pump → show né un hint SDL3.
- Il binario privato ha creato e presentato la finestra su Xvfb `:132` e ha raggiunto il menu. Questo conferma il percorso attuale in questa configurazione; **non** è una prova che la sequenza hidden/show o un hint SDL3 eviti l'eventuale hang X11_ShowWindow sotto un window manager reale. Non ho modificato il codice per introdurre una variante non misurata.

## File e limiti

Modificato: `tests/x86_dsound_test.c`.
Creati sotto `build/qa/gap-exit/`: copia binario privata, test binary, stub header, screenshot, script/log GDB e log build. Nessun file runtime o fuori proprietà modificato; nessun comando Git eseguito.

### Decisioni autonome

Ho mantenuto il resolver fail-closed: il callback guest `0x004FDE60` è presente nel PE ma non risolto nella dispatch map corrente; nessun target non tradotto viene accettato. Ho lasciato invariato il percorso SDL attuale perché la prova isolata non giustifica una modifica della sequenza di visualizzazione. Il task resta parzialmente incompleto per il mancato exit 0 e per l'assenza di una cattura GDB dello store iniziale.
