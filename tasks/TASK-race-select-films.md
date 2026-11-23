# Bug: nella schermata di selezione campagna (Single Player > Original > profilo > Ok)
# solo il personaggio Protoss e' visibile; Terran e Zerg sono NERI.

## Evidenze MISURATE (x86, sandbox Xvfb, build attuale)
- La schermata e' composta da **film Smacker**: 34 `SmackOpen`, header tipici `SMK2 w=0xB8 h=0x80
  frames=0x32` (184x128, 50 frame) — una animazione per personaggio.
- Nessun `SmackBufferOpen`: i film usano buffer propri del gioco; `ToBuffer` registra al volo
  SOLO la prima destinazione (`registrata al volo: 0x... byte=0x5C00` = 184*128).
- Nessun rifiuto (`rifiutata: 0`), nessun `DoFrame async non disponibile`, nessun errore smack.
- Nessun Blt/BltFast DirectDraw su questa schermata (`SC_DDRAW_TRACE=1` -> 0 righe): tutto passa
  dal blitter SCode di storm verso memoria guest (canvas = 0x20560000 in quel run; i dst SCode
  cadono a offset coerenti con le tre figure, es. 0x1DB5E -> riga 118).
- Screenshot (griglia 4x4 di luminanza): Terran/Zerg restano neri; Protoss disegnato.

## Da scoprire
Perche' i frame dei film di Terran e Zerg non arrivano al canvas mentre quello Protoss si'.
Candidati: (a) `SmackToBufferRect` (dirty rect) restituisce la rettangolo una volta sola e storm
salta i blit successivi; (b) i tre film condividono stato nel nostro shim (slot async, palette,
buffer di destinazione) e si sovrascrivono; (c) la registrazione "al volo" del dst avviene solo
per il primo film e gli altri scrivono in buffer non tracciati; (d) ordine/timing di DoFrame.

## Strumenti disponibili (usali, non riscriverli)
- `SC_PRESENT_DBG=1` -> `[present-black]/[present-seq] nz/h/why`
- `REASM_TRACE=1` -> `[sd-trace] dst= x= y= w= r= dw= sw= src= entry= dflags=` per ogni chiamata
  SCode (max 400) e `[canvas-write]` nel fast path 2x
- `SC_DDRAW_TRACE=1` -> `[ddraw-trace] Blt/BltFast ...`
- `tools/shot-stat.sh file.png` -> mean/max/stddev (verifica numerica, niente immagini)
- Click: `tools/scctl --sock <sock> click X Y`; coordinate 640x480: SP(220,120), Original(262,214),
  riga profilo(100,127), Ok(517,391). Campagna: Terran(320,140).
- Sandbox: `SC_SKIP_INTRO=1 SC_CTL=1 ./tools/run-isolated.sh start --id <id> --smack 1`
  (usa Xvfb, NON lo schermo dell'utente) e `... stop --id <id>`.
- Deploy ARM: `SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`; kill solo i PID con
  `/proc/<pid>/cwd == $HOME/starcraft-armv7-test`; `scp build/starcraft-armv7 enne2@10.0.1.126:~/starcraft-armv7-test/`.

## Vincoli (vincolanti)
- **Mai** lanciare il gioco sullo schermo dell'utente (:0) ne' in foreground: solo Xvfb/sandbox,
  sempre in background, `SDL_AUDIODRIVER=dummy`.
- Verifica **numerica**: per la schermata campagna misura la luminanza media delle tre aree
  (Terran ~ x 170-330 y 120-260; Zerg ~ x 20-180 y 250-400; Protoss ~ x 440-600 y 120-300).
  Obiettivo: tutte e tre > 0.02 (prima: due a ~0).
- Non leggere immagini nel contesto; se serve un'occhiata, `magick ... -resize 320x -quality 45`
  (<= 15 KB) una sola volta.
- Oracolo IDA se serve comportamento del binario; nessuna deduzione inventata.
- Regressione: `./tools/build.sh --arch x86 --skip-entry`, `tests/x86_runtime_check/check.sh`,
  intro x86 senza frame neri (con `SC_PRESENT_DBG=1` i "present-black" devono restare 1-2 totali).
- Committa solo se la verifica numerica passa su x86 **e** ARM; messaggio in italiano, push su gitea
  main, verifica SHA locale == remoto.
