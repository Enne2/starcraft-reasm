# Report tecnico — Save/Load Mission isolato

**Esito:** PASS end-to-end — salvataggio e caricamento verificati in Boot Camp  
**Data/host:** 21 novembre 2026, frigate (Fedora Linux 44, x86_64)  
**Autore:** enne2 — email verificata in `~/.gitconfig`  
**Classificazione:** uso interno  
**Modalità:** test runtime isolato con Xvfb privato e audio dummy; nessuna finestra desktop o audio reale.  
**Binario:** `build/qa/fix-saveload/starcraft-x86-save` (ELF32/i386)

## Sintesi

Il Save Mission non completava la scrittura. La traccia con `REASM_TRACE=1` ha mostrato che il gioco crea un percorso nuovo `Save/<personaggio>/<nome>.sng`, ma la shim non traduceva `CREATE_NEW` in `O_CREAT|O_EXCL`; inoltre il resolver case-insensitive dei path rifiutava un componente finale non ancora esistente, anche quando l’apertura lo doveva creare. Il `CreateFileA` falliva con `ENOENT`; il gioco proseguiva con uno stream non valido e terminava con SIGSEGV in `fwrite` (guest `0x0041182A`).

Il runner ora predispone `Save/` e una sottocartella per ogni profilo `.spc`. La shim gestisce `CREATE_NEW` e consente al resolver di mantenere il nome del solo componente foglia mancante, lasciando a `open()` la creazione effettiva. Non sono stati fabbricati file di salvataggio: il file verificato è stato scritto dal gioco ed è stato poi riletto dal gioco.

**In una frase:** il file non veniva creato perché mancava il supporto completo a un’apertura Win32 `CREATE_NEW` di un nuovo componente finale, con directory di destinazione assenti nel sandbox iniziale.

## Perimetro e metodo

- Diagnosi in Boot Camp attraverso `./tools/run-isolated.sh`, identificatore `fix-save`, `--trace 1`, `SC_BIN` privato. Xvfb assegnato `:151`; SDL audio impostato dal runner a `dummy`.
- Screenshot iniziali letti: `build/qa/close-play/{12-save-dialog,13-save-name,14-after-save,17-load-dialog}.png`. Coordinate effettive: campo nome circa `(320,84)`, bottone Save `(198,262)`, bottone Cancel `(440,262)`.
- Build dei due oggetti shim in `build/qa/fix-saveload/obj/`, applicazione di `llvm-objcopy --redefine-syms`, link ELF32 privato. Gli oggetti preesistenti in `build/obj-x86/` sono stati usati solo in lettura.
- Originale esaminato via IDA: `sub_4A7050` (`0x4A7050`) enumera i salvataggi e distingue `.sng`/`.snx`; `sub_4CEE60` (`0x4CEE60`) seleziona `.sng` per il caso single-player; `fwrite` (`0x41182A`) è il punto del fault osservato. La traccia runtime ha determinato il path concreto `.sng`.
- Test unitario esistente `build/qa/saveload/x86_saveload_test`: PASS.

## Risultati e prove

| Requisito | Evidenza | Esito |
|---|---|---|
| Directory necessarie nel sandbox | `tools/run-isolated.sh` crea `Save/` e `Save/<basename-profilo>.spc` | PASS |
| Creazione file nuovo | `game.log`: `[file] guest=...\\save\\qasave\\testsave.sng ... fd=0x00000010`; `Save/qasave/testsave.sng` presente, **104598 byte** | PASS |
| Chiusura dialogo | `build/isolated/fix-save/55-second.png`: ritorno alla partita dopo il click Save | PASS |
| Presenza nel dialogo Load | `build/isolated/fix-save/56-load-dialog.png`: voce `testsave` visibile e selezionata | PASS |
| Apertura e ripresa | Tracce di apertura del medesimo `save/qasave/testsave.sng` alle righe 86436 e 100666 di `game.log`; `57-after-load.png` mostra il gameplay, processo ancora vivo | PASS |
| Chiusura isolata | `run-isolated.sh stop --id fix-save`; verifica successiva `game_alive=0`, `xvfb_alive=0` | PASS |

### Evidenza diagnostica prima del fix

- L’asset tree non conteneva `build/asset-sandbox/Save`; il runner creava solo `characters/` e symlink degli asset.
- Creando manualmente le directory, la richiesta risultava `Save/qasave/testsave.sng`; prima della correzione, il log mostrava `[ci] component miss dir=./Save/qasave/ wanted=testsave.sng`, `CreateFileA` restituiva `INVALID_HANDLE` (`errno=ENOENT`) e il processo cadeva con SIGSEGV, fault address `0x0000000C`, nel percorso `fwrite`.
- Questo ha distinto la directory mancante nel sandbox dal difetto di creazione del file foglia. Nessun salvataggio è stato simulato o copiato nel sandbox.

## Modifiche

- `runtime/shim-port/elf/win32_shim.c`: `CreateFileA` traduce `CREATE_NEW` in `O_CREAT|O_EXCL`; il resolver case-insensitive ammette il solo componente finale inesistente per aperture con `O_CREAT`, preservando la grafia richiesta.
- `tools/run-isolated.sh`: prepara `Save/` e le directory dei profili presenti in `characters/`.
- `build/qa/fix-saveload/`: oggetti shim ricompilati/redefiniti e binario privato linkato; directory dedicata agli artefatti di verifica.
- `reports/STATE-fix-saveload.md`: stato sintetico.
- `win32_shell_dlg.c/.h` non modificati; i relativi oggetti sono stati ricompilati per il link privato. Nessun file vietato dal preludio è stato toccato; nessun comando Git o agente parallelo usato.

## Verifiche e copertura

**Finestra:** sessione del 21/11/2026, host frigate. **Campione:** una partita Boot Camp, un salvataggio e un caricamento consecutivo nello stesso sandbox isolato. Il file scritto è stato verificato per presenza e dimensione; il log mostra successive aperture riuscite e la schermata finale mostra il gameplay.

| Componente | Metodo | Esito |
|---|---|---|
| UI Save | screenshot prima/dopo, click del pulsante Save | PASS |
| Creazione path/file | trace Win32 e `find` sul sandbox | PASS |
| UI Load | lista e caricamento `testsave` | PASS |
| Persistenza nel sandbox | file `.sng` presente dopo il load | PASS |
| Build privata | `file` identifica ELF 32-bit Intel 80386 | PASS |
| Altri formati (multiplayer `.snx`, replay `.rep`) | non inclusi nel campione | NON VERIFICATI |
| Altre campagne/profili e sovrascrittura di salvataggi esistenti | non inclusi nel campione | NON VERIFICATI |

La prova è funzionale e limitata a una sessione single-player Boot Camp; non attesta compatibilità universale con versioni/formati diversi.

## Raccomandazioni

1. **P1 — mantenere `CREATE_NEW` e il caso di foglia mancante**: entrambi sono necessari per creare nuovi file sotto directory risolte case-insensitivamente.
2. **P2 — ripetere la matrice per `.snx` e replay** prima di dichiarare copertura oltre il single-player `.sng`.
3. **P2 — testare un profilo nuovo e la sovrascrittura** in sandbox separati, senza riusare i dati della prova.

## Decisioni autonome

- Ho applicato la correzione minima supportata dalla traccia: gestione `CREATE_NEW`, risoluzione della foglia mancante e predisposizione delle directory di sandbox. Non ho reindirizzato i salvataggi, creato file sintetici o allargato il test ad altri formati.
- Ho mantenuto i report nei percorsi Markdown richiesti dal task, invece di produrre un HTML autonomo.

## Appendice riproducibilità

- Screenshot diagnostici e finali: `build/isolated/fix-save/30-start.png`, `41-bootcamp.png`, `43-save-name.png`, `55-second.png`, `56-load-dialog.png`, `57-after-load.png`.
- Log completo: `build/isolated/fix-save/game.log` (trace `REASM_TRACE=1`).
- Salvataggio verificato: `build/isolated/fix-save/sandbox/Save/qasave/testsave.sng`.
- Build: sorgenti shim compilati i686 con le flag di `tools/build-shim-x86.sh`, `llvm-objcopy --redefine-syms=runtime/shim-port/shim_rename.txt`; link conforme a `tools/link-x86.sh` utilizzando gli oggetti preesistenti senza modificarli e output in `build/qa/fix-saveload/starcraft-x86-save`.
