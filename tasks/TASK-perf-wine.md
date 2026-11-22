# Missione: confronto prestazioni binario nativo ottimizzato vs StarCraft.exe originale sotto Wine

## Obiettivo
Misurare e confrontare **CPU** e **RAM (RSS/PSS)** del nostro `build/starcraft-x86` (nativo i686, ottimizzato)
contro **`StarCraft.exe` originale 1.16.1 eseguito tramite Wine** (`wine-11.20 Staging`, wow64),
su tre fasi: **boot/avvio**, **menu**, **gameplay (Boot Camp)**.

Tutto deve girare **off-screen**: solo display virtuali Xvfb dedicati (`:300`+), **audio disabilitato/dummy**,
**nessuna finestra sul display dell'utente (`:0`/Wayland)** e **nessun input iniettato sul desktop reale**.
L'iniezione input è ammessa SOLO dentro i nostri Xvfb (via `python3 -c "import Xlib"` + XTEST, che è installato).

## Dove sono i pezzi
- Nativo: `build/starcraft-x86`, sandbox asset `build/asset-sandbox/`, runner `tools/run-isolated.sh`
  (`start --id X --skip-intro 1 --smack 1`, `cmd --id X 'click x y'`, `cmd --id X 'move x y'`, `shot`, `stop`, `list`).
  Il runner usa già Xvfb dedicato + `SDL_AUDIODRIVER=dummy`, e supporta `SC_PRESENT`, `SC_FPS_CAP`, `SC_CONTROL_FILE`.
- Originale: `/home/enne2/dev/final-starcraft/Starcraft/` (`StarCraft.exe` 1.2M, `storm.dll`, MPQ vari,
  contiene `noboot.dat` che salta i filmati iniziali, e `ddraw.dll`/`cnc-ddraw` per DirectDraw).
- Wine: `wine-11.20 Staging`, `wine32`/`wine64` presenti, `~/.wine` esiste ma **usa un prefisso dedicato**
  `build/qa/wine/prefix` creato con `WINEPREFIX=... wineboot -u` (una volta sola, poi riusa).

## Cosa produrre (deliverable)
1. **`tools/bench-compare.sh`**: harness riproducibile, tutto parametrizzato, che
   - avvia Xvfb `640x480x24` dedicato per ciascun target,
   - per il nativo: usa `tools/run-isolated.sh`,
   - per Wine: `WINEPREFIX=build/qa/wine/prefix WINEDEBUG=-all wine StarCraft.exe` con cwd asset dir,
     audio disabilitato (chiave `HKCU\Software\Wine\Drivers` / `Audio` = `""`, oppure env equivalente),
   - campiona per **5 secondi** in ogni fase, a 1 Hz, con metodo identico per i due target:
     - CPU: somma `utime+stime` da `/proc/<pid>/task/*/stat` su **tutto l'albero di processi**
       (per Wine includere `wineserver`, `explorer.exe`, `services.exe`, processi figli; per il nativo
       includere `llvmpipe-*`, `SDLAudioP*`, worker Smacker),
     - RAM: `smaps_rollup` del processo di gioco (`Rss`, `Pss`, `Private_Clean/Dirty`, `Anonymous`, `Swap`)
       **più** la somma degli RSS dell'albero (wineserver/explorer inclusi),
     - FPS: per il nativo `SC_FPS_LOG=1` (`[fps]`), per Wine lasciare `null`/`n/d` se non ottenibile,
   - fasi: **boot** (0→5 s dal launch), **menu** (5 s a ~20 s), **gameplay** (5 s dopo essere entrati in partita),
   - per Wine usa input XTEST sui nostri Xvfb con le **stesse coordinate** del nativo
     (`220,120` Single Player → `260,226` Original → `100,109` profilo → `515,393` OK → `305,250` Terran →
     `320,440` Rebel Yell → `320,440` Boot Camp → `538,390` Start → `197,262` OK tips; se il layout differisce,
     determina le coordinate giuste verificando screenshot con `import -window root` e `read`),
   - conclude con `stop` di tutte le istanze e `kill` dei processi Wine residui,
   - stampa in stdout una tabella riassuntiva e la salva in `build/qa/perf-compare/`.
2. **`reports/PERF-WINE-COMPARISON.md`**: report con
   - tabella comparativa per fase (CPU % core, CPU % normalizzata), RSS/PSS MiB, delta e rapporto,
   - **overhead complessivo** di Wine (CPU e RAM extra: wineserver + explorer + traduzione API + ddraw),
   - note metodologiche, comandi esatti, limiti (es. FPS non misurabili su Wine, ddraw/cnc-ddraw in uso),
   - 2 esecuzioni per target per confermare la riproducibilità (riporta min/mediana/max).
3. **`reports/STATE-perf-wine.md`**: stato sintetico (fatto / non fatto / limiti).

## Vincoli
- NON modificare `runtime/`, `tools/` esistenti (solo **aggiungere** `tools/bench-compare.sh`), né `build/starcraft-x86`.
- Nessun rebuild dell'eseguibile nativo: si misura il binario già ottimizzato attuale.
- Se Wine non riesce ad avviare StarCraft, **documenta l'errore esatto** (log `WINEDEBUG=+loaddll` mirato,
  o messaggio della finestra) e riporta comunque i dati nativi + il motivo del fallimento; non inventare numeri.
- Se l'input XTEST su Wine non funziona, riporta comunque boot+menu e dichiara esplicitamente che il gameplay
  Wine non è misurabile in modo affidabile, spiegando perché.
- Chiudi tutte le istanze e verifica con `./tools/run-isolated.sh list` e `ps -ef | grep -E 'wine|starcraft'`.

## Criterio di accettazione
Tabella numerica con valori reali letti da `/proc` per entrambi i target, riproducibile con un singolo comando
(`./tools/bench-compare.sh`), con dichiarazione esplicita di cosa è misurato e cosa non lo è.
