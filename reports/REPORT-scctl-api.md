# `sc_ctl` — API di automazione dentro lo shim (screenshot framebuffer + input)

Data: 2026-11-22 · Modulo: `runtime/shim-port/elf/sc_ctl.c` (+ `sc_ctl.h`), client `tools/scctl`

## Cosa fa (attivo automaticamente)
All'avvio, se `SC_CTL` non vale `0`, lo shim apre un server su **socket Unix** `SC_CTL_SOCK`
(default `./sc-ctl.sock`, nella CWD del gioco); con `SC_CTL_PORT=N` anche su TCP `127.0.0.1:N`.
Protocollo testuale, una riga per comando, risposta `ok ...` / `err ...`:

| comando | effetto |
|---|---|
| `shot <path>` | **PNG 640x480 RGB** dal canvas 8bpp + palette corrente (encoder interno, nessuna lib) |
| `click <x> <y> [btn]` | click nella coda eventi del gioco; `btn` 1=sinistro (default), **2=destro** |
| `move/down/up <x> <y> [btn]` | eventi separati |
| `key <nome\|num>`, `keydown`, `keyup` | `esc enter space tab backspace left right up down f1..f12` o keycode SDL |
| `text <stringa>` | testo nella finestra/dialogo attivo |
| `state` | `ok state frame=<n> w=640 h=480 presented=<n> pal=<0\|1>` |
| `quit` | chiude la connessione (non il gioco) |

Lo screenshot **non** viene letto dal thread del server: si imposta una richiesta e il thread del gioco
copia canvas+palette (niente tearing). L'input passa dalla stessa via del control-file
(`host_elf_test_event`), quindi nessun tool X e nessun focus richiesto.

Client: `tools/scctl {shot,click,move,down,up,key,text,state,wait,seq}` (python3, nessuna dipendenza),
`--sock <path>` oppure `SC_CTL_SOCK`.

## Verifiche fatte (x86, Xvfb)
```
./tools/run-isolated.sh start --id apix7 --skip-intro 1 --smack 1
tools/scctl --sock build/isolated/apix7/sandbox/sc-ctl.sock state
  -> ok state frame=401 w=640 h=480 presented=401 pal=1
tools/scctl ... shot /tmp/z2-create.png
  -> ok shot /tmp/z2-create.png 922410      (PNG 640x480, ~0.9 MB: zlib "stored")
```
Sequenza completa pilotata **solo** dall'API: menu → Single Player → Original → Registry → OK →
**Play Custom → "Create" con la lista mappe popolata** (`(2)Bottleneck.scm` selezionata, dettagli mappa
a destra: 128x128, Space, 2 giocatori). Screenshot: `build/qa/pbp-test/api-*.png`, `/tmp/z2-create.png`.

Nota: la latenza `shot` e' dominata dalla scrittura del PNG (zlib stored, ~0.92 MB): pochi ms su x86,
~decine di ms su Pinebook.

## Fix collaterale
`runtime/shim-port/elf/win32_game.c`: `readdir64` veniva letto con **offset cablato 19** per `d_name`
(valido per `dirent64` x86). Sostituito con un accesso al campo tramite il layout di `dirent64`
(`struct dirent64_layout`): su ABI diverse l'offset cambia e la lista file risultava vuota.

## Stato sul Pinebook (onesto)
- API **funzionante**: `state`, `shot` (PNG 640x480 del canvas, non del desktop), `click`, `key`, `text`.
- Raggiunte via API: menu → Single Player → Select Game Type → Registry → profilo creato → campagna
  (Terran/Zerg/Protoss) → **Play Custom → "Create"**.
- **Ancora bloccato prima della partita**, due punti distinti:
  1. **Campagna**: click su *Terran Episode I* → `Error initializing standard.snp`. Sul target il gioco
     **non esegue** `LoadLibraryA`/`CreateFileA` con `standard.snp` (traccia `REASM_TRACE`/`SC_BRIDGE_TRACE`
     su 5,1 M di righe: zero occorrenze), quindi si ferma prima dell'apertura del provider; su x86 la
     sequenza e' `[snp] caps.dat … → [risorse] caricato come FILE … standard.snp → SnpQuery → SnpBind`.
  2. **Create (Play Custom)**: sul Pinebook la lista mappe resta **vuota** (su x86 invece si popola con
     `[campaign] [ladder] [replays] [scenario] (2)Bottleneck.scm …`), nonostante `maps/` sia presente e
     i file deployati: la differenza e' ARM-specifica nella enumerazione file.
