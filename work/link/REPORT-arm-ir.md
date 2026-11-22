# Report — switch/jump table ARM IR (StarCraft)

## Esito statico IDA

Database letto in sola lettura: `build/lift-full/StarCraft.orig.exe.i64` (sessione IDA `d3dbbad1`, imagebase `0x400000`). Nessuna sessione IDA preesistente era aperta. Il checkout non espone una directory `.git` in `/home/enne2/dev/starcraft-armv6` (git status segnala filesystem boundary), quindi nessun dato git disponibile.

`0x004D1DBC` è **un case di switch**, non un return/call continuation. In `sub_4D1D70` la jump table individuata da IDA parte dal dispatch `0x004D1DB5` (`ncases=133`, tabella salti a `0x004D25B0`). `ida_xref.calc_switch_cases` mappa il caso `28` a `0x004D1DBC`. IDA verifica il target come head di istruzione e code. Disassembly del target: `mov eax, [ebp+wParam]` (`0x4D1DBC`), `xor ecx,ecx`, `test eax,eax`, `setz cl`, `mov dword_51BFA8,eax`, `mov esi,ecx`, `call sub_4D1750` (`0x4D1DCD`), `mov eax,dword_51BFA8`, `call sub_421730` (`0x4D1DD7`), ... Quindi il target runtime non tradotto deriva da un case di jump table mancante, non da push/ret o da un ritorno indiretto.

## Censimento batch (0x4D0000–0x4E5000)

Enumerate con IDA tutte le `switch_info_t` nella fascia: **24 dispatch switch, 219 destinazioni**. Tutti i 219 target sono istruzioni code/head validi in IDA. Inclusi i dispatch di `sub_4D1D70` (3 tabelle: `0x4D1DB5`, `0x4D1EEE`, `0x4D2345`) e `sub_4E0820` (`0x4E0977`). I controlli sui nomi richiesti hanno dato: `sub_4D4470`, `sub_4212C0`, `sub_41D930` non contengono switch; nella fascia indicata sono stati inclusi anche tutti gli altri dispatch identificati da IDA.

Confrontando i target col `code_entry_points.sci` attuale risultano 53 target unici assenti, e per tutti e 53 manca anche una definizione `@loc_<VA>` nell'IR `build/lift-full/StarCraft.ll`:

```
004D0A05 004D0A16 004D0A27 004D0A38 004D0A49
004D0A88 004D0A99 004D0AAA 004D0ABB 004D0ACC
004D0ADD 004D0AEE 004D0AFF 004D0B10 004D0B21 004D0B32
004D1DBC 004D1E4F 004D1E84 004D1EB2 004D1EF5 004D1F1F
004D1F7F 004D216F 004D2229 004D2299 004D22FB 004D252B
004DB7BD 004DB7DD 004DB7F7 004DB827 004DB839
004DFF16 004DFF37 004DFF49 004DFF81 004DFF86
004E158C 004E159C 004E15B4 004E15E1 004E1610
004E180C 004E181C 004E1834 004E1861 004E1890
004E1A92 004E1AD5 004E1AE9 004E1AFA 004E1B1C
```

Altri target delle tabelle sono già elencati nel SCI. Le tabelle con molteplici valori case che puntano alla stessa destinazione sono deduplicate nell’elenco sopra.

## Decisioni autonome

Ho scelto la via conservativa richiesta: conservare intatto l’IDB, basare l’elenco solo su switch tables riconosciute da IDA e aggiungere esclusivamente target verificati come head di istruzione/codice. Il file SCI è stato salvato in backup prima della modifica.

## Modifiche e verifiche

La prima analisi è stata completata prima di qualunque rigenerazione. Backup creato: `build/lift-full/code_entry_points.sci.bak`. Aggiunti in blocco i 53 target validati; la riga effettiva usa la forma canonica `loc_4D1DBC` (senza zeri iniziali superflui). Dopo `TMPDIR=/dev/shm ./tools/lift-full.sh srw exe && TMPDIR=/dev/shm ./tools/lift-full.sh ir exe`, l'IR contiene `define private fastcc void @loc_4D1DBC(...)`.

Comandi/verifiche:
- `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --no-smoke`: primo tentativo fallito nella fase opt/llc concorrente; rilanciato con wrapper `opt`/`llc` serializzati tramite `flock` in `/dev/shm`, completato con esito 0 e artefatti entry compilati. In quella seconda esecuzione le fasi risultavano cache hit.
- Link eseguito manualmente: `TMPDIR=/dev/shm ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime ./tools/link-arm.sh build/starcraft-armv6-dispatch` — esito 0.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch`: **96.756 voci ordinate/univoche; 0 collisioni VA guest/host**.
- `make check`: PASS (freestanding ARMv6/QEMU, integration PE ordinale/Wine + IR/QEMU ARM1176; warning linker `_DllMainCRTStartup@12` nel fixture, test PASS).
- Smoke in `build/asset-sandbox`, `TMPDIR=/dev/shm REASM_TRACE=1 timeout 35 qemu-arm-static -cpu arm1176 -L ../sysroot ../starcraft-armv6-dispatch`: exit 71. Il trace non si ferma più su `0x004D1DBC` e procede attraverso la WndProc; raggiunge poi un altro target non tradotto `0x20170A9C`, `eip chiamante = 0`, ret ARM `0x280` (log `build/qa/run-arm-ir.log`, circa 4015 righe). Questo indirizzo è fuori dagli intervalli VA StarCraft/storm nella mappa (ultimo VA `0x15044C73`): non è prova di un ulteriore case UI e non va aggiunto ciecamente come entry. È il nuovo blocco da investigare.

## Storm.dll — censimento complementare switch (20/11/2026)

IDA aperta in sola lettura sul database di progetto `build/lift-full/ida/storm.lift.dll.i64` (sessione `1adcd661`); nessuna modifica/salvataggio IDB. Verificato `tools/lift-full.sh`: `run_dir()` copia **tutti** i `build/lift-full/*.sci` in ogni run. Non esiste un `storm_code_entry_points.sci` separato: per Storm viene usato `build/lift-full/code_entry_points.sci` condiviso, insieme a `external_procedures.sci` e agli altri SCI.

Censimento globale funzioni in Storm (0x15000000–0x15050000, oltre le aree SDraw/video e MPQ/SFile richieste): **54 switch_info_t**, 332 destinazioni in uscita conteggiate per dispatch, 259 target unici assenti dal SCI entry-point. Tutti i target sono head di istruzione/codice validi IDA. Dei 259 assenti dal SCI, **247 hanno già una definizione `@loc_<VA>` in `storm.ll`** (quindi niente aggiunta necessaria; i case sono comunque nel modulo IR). I restanti 12 sono le destinazioni del dispatch `sub_15042F9B` a `0x15042FFE` (switch 12 casi, 0–11/default). Sono interni alla zona CRT esplicitamente esclusa `0x1503FF06–0x15043BDF`: compaiono in `external_procedures.sci` come `crt_unlifted_*`, mentre nell’IR sono alias `_text`, non definizioni di codice. La stessa lista externalizza `0x1504339F` (jump table); queste entry sono già trattate dal percorso CRT esterno, non sono target liftabili da aggiungere al code-entry SCI.

Conclusione prudente: **nessun target switch Storm da aggiungere** a `code_entry_points.sci`; i 247 mancanti dalla lista sono già presenti come funzioni IR, i 12 residui sono intenzionalmente externalizzati dal CRT. Nessuna rigenerazione Storm eseguita perché non c'è una modifica SCI da cui dipenda; nessun `build-entry-obj.sh`/link eseguito, per non interferire col lavoro concorrente `sc-arm-sdraw`. Non è stato verificato né alterato il bridge.

## Ingresso menu StarCraft (nuovo audit)

IDA in sola lettura: `build/lift-full/StarCraft.orig.exe.i64`, sessione `5fcec25c`. `0x00418ED9` è il case 14 della switch table a `0x00418ED2` dentro `sub_418EB0`; non è una funzione autonoma. La stessa funzione contiene altri due switch ai dispatch `0x00418EEB` e `0x00419086`. Target validato come head di istruzione/codice. La call path sul log è coerente: `sub_419D20` (istruzione `0x419D5B`; chiamate indirette a `[esi+2Ah]` e ripresa dopo call `0x419D8C` a `0x419D8F`) → callback/trampoline `sub_4D4960` → tail jump `sub_418EB0` a `0x4D4984`/`0x4D4996` → dispatch switch che seleziona `0x418ED9`. `sub_4D4960` inizia regolarmente a `0x4D4960`; non è la destinazione non tradotta. Decompilato `sub_419D20` mostra tre invocazioni di callback `[a2+42]` per messaggi 7/10/0Eh.

Censimento IDA di tutti i `switch_info_t` in funzioni `0x410000..0x4F7B18` (CRT FP escluso): **302 dispatch, 2.102 destinazioni**, tutte verificate come code head in IDA (0 target invalidi). **932 target unici** non erano nel SCI; per 634 non risultava né definizione `@loc_*` né alias nel vecchio `StarCraft.ll`. Inoltre, nel range dati `.rdata`/`.data` `0x500000..0x51FFFF` ho identificato **223 valori puntatore unici** che puntano a code head del range guest ed erano assenti dal SCI (tabella di audit `/dev/shm`, nessun file progetto). Questo è il perimetro usato per i callback/puntatori richiesti; non è stata fatta una scansione cieca di valori non-code o dentro istruzioni.

Ho creato backup pre-modifica `build/lift-full/code_entry_points.sci.bak-menu` e aggiunto in un unico batch i **1.155 entry unici** (932 target switch + 223 puntatori dati; insiemi disgiunti). Nessun file runtime, Storm o `external_procedures.sci` è stato modificato.

### Build e verifica menu

- Eseguiti con `TMPDIR=/dev/shm`: `./tools/lift-full.sh srw exe` e `./tools/lift-full.sh ir exe`; `StarCraft.ll` rigenerato (2.381.657 righe), `@loc_418ED9` presente come definizione tradotta.
- Primo `build-entry-obj.sh --no-smoke` ha preso cache vecchia: la mappa ELF non conteneva `0x418ED9`. Rilanciato `--no-cache --no-smoke` per rigenerare la dispatch: 101.800 VA (exe 80.068, Storm 21.727 + voci extra), 12.640 call indiretti riscritti. L’opt ha trovato due dichiarazioni duplicate per `storm_313_arm`/`storm_334_arm` (visibilità hidden vs default) generate nel solo `StarCraft.entry.ll`; ho rimosso le due dichiarazioni duplicate nel file IR generato, mantenendo le dichiarazioni hidden, poi `opt verify`, `opt default<O2>` e `llc` sono passati. Cache `StarCraft.entry.ll` aggiornata con questo IR deduplicato. Nessun sorgente dispatcher/runtime è stato modificato.
- Link effettuato con `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-menu` usando l’oggetto exe appena compilato; hash SHA-256: `29b53a52b78b90e40b288fdfddbfc3aa34867452128d5b29e87fe5bafcefb387`.
- `python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-menu`: **101.800 voci ordinate/univoche, 0 collisioni**. Verifica diretta della tabella ELF/IR: `0x00418ED9` presente al dispatch index 8.629, mappato a host entry `0x180E7D78`.
- `TMPDIR=/dev/shm make check`: **PASS**.
- Micro-smoke 10 s in `build/asset-sandbox`, QEMU ARM1176: crea `SWarClass`, riproduce `blizzard.smk` e avvia `starXintr.smk` (frames `0xD64`); nessun `[arm-dispatch] STOP` in quel tratto. Terminato intenzionalmente dal timeout (exit 137) prima che il secondo filmato finisca, quindi non dimostra l’ingresso runtime nel menu; la presenza di `0x00418ED9` nella dispatch è verificata indipendentemente nel binario.
- Log: `build/qa/run-arm-menu.log`. `runtime/arm/storm_sdraw_arm.c` e `build/lift-full/external_procedures.sci` intatti.
