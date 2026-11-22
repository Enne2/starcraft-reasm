# Report — StarCraft ARMv7 in-game su Pinebook Pro

Data: 22 novembre 2026 · host build: `frigate` · target: `enne2@10.0.1.126` (Pinebook Pro, armhf)

## Esito

**Risolto: partita reale raggiunta** usando Play Custom → `(2)Bottleneck.scm`. La schermata di gioco con unità, risorse e HUD è conservata in:

- `build/qa/pbp-test/ingame-bottleneck.png` — PNG originale catturato con `scctl shot` (901 KiB).
- `build/qa/pbp-test/ingame-bottleneck.jpg` — copia compressa per ispezione (qualità 70, 53 KiB).

SHA-256 PNG: `d7a31afdf9749e1631075ad507afbf57cf99b7c7020b7ab4b9f4bcfadf55938b`.

## Causa radice — flag Linux errato per directory su ARM32

`game_FindFirstFileA` in `runtime/shim-port/elf/win32_game.c` apriva la directory di ricerca con il valore letterale `0x10000`, trattandolo come `O_DIRECTORY`. Quel valore è corretto per i686 Linux, ma **su armhf `O_DIRECTORY` è `0x4000`**; `0x10000` è invece `O_DIRECT`. Aprire `maps` con `O_DIRECT` fallisce con `EINVAL` (errno 22), quindi l’enumerazione fallisce e il dialogo Create mostra la cartella vuota.

Verifica ABI dai rispettivi header `fcntl.h`:

- `i686-linux-gnu`: `__O_DIRECTORY = 0200000` (ottale) = `0x10000`.
- `arm-linux-gnueabihf`: `__O_DIRECTORY = 040000` (ottale) = `0x4000`; `__O_DIRECT = 0200000` (ottale) = `0x10000`.

La traccia del processo ARM precedente mostrava il CWD corretto (`/home/enne2/pbst/scctl-api`), la directory `maps` presente e accessibile, ma il successivo open del path maps falliva con `errno=0x16` (`EINVAL`). Questo escludeva una directory mancante o un CWD errato.

La traccia del nuovo build, con `REASM_TRACE=1`, conferma il passaggio nel percorso di enumerazione e il risultato dopo la fix:

```text
[ff] dir-open pat=C:\home\enne2\pbst\scctl-api\maps\*.* native=/home/enne2/pbst/scctl-api/maps rc=0x00000010
[ff] first-match pat=C:\home\enne2\pbst\scctl-api\maps\*.* native=(2)Space Madness.scm rc=0x76000002
```

La lista ARM ha mostrato `testdir` e le cinque mappe `.scm`; l’apertura di `(2)Bottleneck.scm` ha avuto esito `ok` nei log. La selezione è arrivata al gioco: HUD visibile e frame presentati (`scctl state`: frame 7100, 640×480, `pal=1`).

### Ipotesi CWD / Storm

Verificata la premessa suggerita: il gioco gira davvero con CWD `~/pbst/scctl-api`, la directory `maps/` contiene le mappe e i path relativi vengono risolti correttamente. Non era la causa.

Ho letto in IDA la funzione Storm `SFileOpenArchive` a `0x15017DD0`: rifiuta nome nullo/vuoto e apre un file tramite `CreateFileA`; per path relativi prepara il prefisso secondo il proprio percorso modulo/base. La enumerazione osservata nella nuova traccia è invece `game_FindFirstFileA` con pattern `maps\*.*`. La precedente assenza di `[ff]` non era prova sul nuovo eseguibile: il processo ARM preesistente era stato avviato con un binario più vecchio della build strumentata.

Nessun difetto di layout `TMPQArchive`/`SFILE_FIND_DATA`, callback ABI, `SFileGetFileInfo` o import table è stato necessario per spiegare il guasto; non ho applicato ipotesi non misurate su questi aspetti.

## Modifica

In `runtime/shim-port/elf/win32_game.c` ho introdotto `GAME_O_DIRECTORY`: su ARM usa `O_DIRECTORY` dal sysroot armhf; sugli altri target conserva `0x10000` (valore corretto per i686 Linux). Il flag corretto viene usato nei due open di directory in `game_FindFirstFileA` e `game_CreateDirectoryA`.

Il file conteneva già modifiche non commesse di diagnostica `[ff]` (`REASM_TRACE=1`), presenti prima di questo intervento. Ho conservato tali modifiche e aggiunto log di apertura directory, primo match e lista vuota, senza sovrascrivere il lavoro preesistente. La traccia è disattivata senza `REASM_TRACE`.

## Consulto GPT

`agy_models` è fallito due volte nel wrapper con `flags provided but not defined: -output-format`; il consulto diretto `antigravity_chat` è riuscito usando `gpt-5.2-codex`. Il modello ha proposto path separator/case sensitivity, padding ARM32, regole AAPCS per argomenti a 64 bit e signedness di `char`, oltre a campi `TMPQArchive`. Erano ipotesi generiche, non confermate e in parte estranee a questo Storm 1.16; non ha individuato il valore `O_DIRECTORY` dipendente dall’ABI. La misura dei flag dal sysroot e la coppia di tracce EINVAL → `dir-open`/`first-match` hanno identificato e verificato la causa concreta.

## Verifiche

- `./tools/build.sh --arch x86 --skip-entry` — **PASS**, prodotto `build/starcraft-x86`.
- `tests/x86_scomp_test/check.sh` — **PASS** (4.292 settori confrontati, zero differenze).
- `tests/x86_runtime_check/check.sh` — **PASS**.
- Sequenza x86 isolata: menu → Single Player → Original → profilo → Play Custom; dialogo Create popolato con directory e `(2)Bottleneck.scm`. Traccia `[ff]` attiva.
- `SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh` — **PASS**, prodotto `build/starcraft-armv7`.
- SHA-256 build ARM locale e target: `0caa0b408e251ff3a6afc609f1d9f1b96740a64f082da7318289a49f36cf1ba5`.
- Deploy sul Pinebook dopo aver fermato il PID `2555826` solo dopo aver verificato il suo CWD (`~/pbst/scctl-api`); hash remoto uguale. Avvio con `REASM_TRACE=1 bash /tmp/api-run.sh`.
- Play Custom ARM: lista popolata, apertura della mappa riuscita e partita effettivamente disegnata; nessun dialogo fatale di `standard.snp` nel percorso verificato.
- `git diff --check` — **PASS**.

## Stato target e decisioni autonome

- La partita ARM resta aperta sul Pinebook, per lasciare la schermata di gioco disponibile all’utente.
- Il test x86 isolato (`arm-ingame-x86`) è stato fermato. Il processo separato `starcraft-armv7-final` con CWD `~/pbst/game` non è stato toccato.
- Nessun commit o push.

## Decisioni autonome

Ho scelto la correzione minima e verificabile del flag ABI, non cambiamenti a Storm, al CWD, ai path dei dati o alle tabelle d’import. Ho mantenuto l’eseguibile ARM in partita e lasciato intatte le modifiche altrui non correlate; nessun’altra ipotesi è stata trasformata in patch senza misura.
