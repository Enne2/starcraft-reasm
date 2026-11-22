# Obiettivo unico: portare StarCraft ARMv7 su Pinebook **IN PARTITA** (schermata di gioco reale)

## Ambiente gia' pronto (non ricrearlo)
- Pinebook Pro: `enne2@10.0.1.126` (aarch64 + multiarch armhf). Gioco: `~/pbst/scctl-api/` con asset
  (MPQ, `standard.snp`, `battle.snp`, `local.dll`, `maps/` con 5 mappe `.scm`), `characters/`, `Save/`,
  `starcraft-armv7`, `libsmacker-1.2.0.so`, **e il client `scctl`**.
- Avvio: `bash /tmp/api-run.sh` (usa `SDL_VIDEODRIVER=x11`, `DISPLAY=:0`, `XAUTHORITY` mutter,
  `SC_SKIP_INTRO=1`, `SC_FPS_CAP=25`, `SC_CTL=1`, socket `~/pbst/scctl-api/sc-ctl.sock`).
- **API dentro lo shim** (`runtime/shim-port/elf/sc_ctl.c`) + client `tools/scctl`:
  `scctl --sock <sock> state|shot <png>|click X Y [btn 1=sx 2=dx]|move|key <nome>|text <str>`.
  `shot` produce PNG **640x480 dal framebuffer** (coordinate = canvas 640x480).
- Build x86: `./tools/build.sh --arch x86 --skip-entry`; ARM: `SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`;
  deploy: stop del processo, `scp build/starcraft-armv7 enne2@10.0.1.126:~/starcraft-armv7-test/starcraft-armv7`
  (mai scp su file in esecuzione: ETXTBSY), poi rilancio.

## Coordinate utili (canvas 640x480, verificate)
menu: Single Player (220,120); Select Game Type: Original (262,214); Registry: OK (517,391),
campagna: **Terran (320,140)**, Play Custom **(310,415)**, Cancel (530,415); dialogo errore: Ok (320,312).

## Evidenza raccolta finora (usala, non riscoprirla)
1. **Profilo**: creazione OK (`characters/*.spc`); il falso "insufficient disk space" era la mancanza di
   `characters/`+`Save/` (risolto).
2. **Campagna** (click su Terran): dialogo `Error initializing standard.snp. Please reinstall StarCraft.`
   Sul target **nessuna** riga `[snp]`, nessun `LoadLibraryA`/`CreateFileA` con `standard.snp`
   (traccia `REASM_TRACE=1`, `SC_BRIDGE_TRACE=1` su 5,1 M righe; `[lib]`/`[file-open]` attivi e funzionanti).
   Su **x86** la sequenza e': `[snp] caps.dat del provider servito in memoria` →
   `[risorse] caricato come FILE … standard.snp -> handle 0x71000003` → `SnpQuery 0/1` → `SnpBind` → `spiInitialize`.
   Il provider si ottiene via `SFileOpenArchive("standard.snp")` (vedi commento in `snp_elf.c`), e il nostro
   `CreateFileA` serve un MPQ virtuale (`win32_snp_vfile_open`).
3. **Play Custom**: su x86 la lista mappe si popola (`[campaign] [ladder] [replays] [scenario] (2)Bottleneck.scm`),
   su ARM resta **vuota**. Traccia `[ff]` inserita in `game_FindFirstFileA` (`runtime/shim-port/elf/win32_game.c`,
   dietro `REASM_TRACE`) **non scatta ne' su x86 ne' su ARM**: l'enumerazione non passa da `FindFirstFileA`,
   quindi passa dal layer file di **Storm** (SFile*/MPQ + file locali).
4. Wiring statico a posto: `bridge_table.c`/`bridge_symbols.c` contengono `SnpQuery`/`SnpBind` sia ARM sia x86;
   `storm_ordinals` identici; `readdir64` non usa piu' l'offset cablato 19 (fix gia' applicato).

## Compito
1. **Consulta il modello GPT (5.2/6.2) sul provider Codex** (usa `agy`/`antigravity_chat` con modello Codex
   o il provider disponibile: elenca con `agy_models` e scegli il GPT piu' recente) descrivendogli il quadro
   sopra e chiedendo: *perche' su ARM32 il percorso `SFileOpenArchive("standard.snp")` e l'enumerazione
   `SFileFindFirstFile`/mappe non vengono nemmeno tentati, mentre su i686 si'?* Chiedi un piano diagnostico
   concreto (quali breakpoint/tracce, quali campi di `SFILE_ARH`/`TMPQArchive` guardare) e confronta le sue
   ipotesi con le misure.
2. Applica la diagnosi: strumenta il punto esatto (trace `REASM_TRACE`/env dedicata) e correggi la causa.
   Le aree candidate sono `runtime/shim-port/elf/win32_game.c` (enumerazione), `win32_shim.c`
   (CreateFileA/SFile bridging), `runtime/common/elf_support_arm.c`, `runtime/common/game_main_arm.c`,
   e — se serve — la tabella di import (`bridge_shims.def`/`bridge_dynamic.def`).
3. Verifica su **x86** di non rompere nulla: `./tools/build.sh --arch x86 --skip-entry`,
   `tests/x86_scomp_test/check.sh`, `tests/x86_runtime_check/check.sh`, e la sequenza fino a `Create` con
   lista mappe popolata.
4. Su **ARM**: arriva **in partita** (campagna Terran oppure Play Custom con `(2)Bottleneck.scm`) e
   dimostralo con screenshot `scctl shot` salvati in `build/qa/pbp-test/ingame-*.png`.

## Regole di contesto (importante)
- **Comprimi ogni immagine prima di leggerla**: `magick shot.png -quality 70 -strip shot.jpg` (≈40–70 KB).
  Non leggere PNG da 0.9 MB: il contesto e' gia' stato saturato da quello.
- Nessun loop periodico di screenshot: cattura **solo** quando serve.
- Non toccare `~/starcraft-armv7-test` mentre gira; ferma per PID prima di scp.
- Working tree con modifiche altrui: niente `revert`/`stash`/`reset`.
- Chiudi con `reports/REPORT-arm-ingame.md` + `STATE-arm-ingame.md`: causa radice, patch, misure,
  screenshot e — se non risolto — esattamente cosa manca e il parere ricevuto dal modello GPT.
