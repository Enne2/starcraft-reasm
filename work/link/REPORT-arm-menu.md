# Report — avanzamento menu ARMv6

## Modifiche e verifica

- `tools/dispatch-ir.py`: gli entry ordinali esportati `313`/`334` ora tail-callano `storm_313_arm`/`storm_334_arm`, oltre alle voci `loc_*` già rimappate. Aggiunti wrapper degli ordinali `454`/`457`/`458`: con `SC_SMACK=0` il #454 emette handle sentinella `0x56000001` e successo, #457 riconosce il sentinel come filmato terminato e #458 come successo; con handle ordinario i wrapper #457/#458 tail-callano gli originali. Il #454 passa all'originale se `SC_SMACK` non è esattamente `0`.
- `runtime/arm/storm_sdraw_arm.c`: `storm_video_skip_enabled()` controlla l'ambiente. Nessuna modifica a `StarCraft.ll`, SCI, file guest, `win32_*`, `com_*` o `smack_*`. Il ring diagnostico non è stato toccato.
- **Decisioni autonome:** modalità conservativa; skip attivo esclusivamente con `SC_SMACK=0`, altrimenti semantica originale. Il test dummy non ha raggiunto i filmati e quindi non dimostra ancora l'efficacia runtime dei wrapper.

IDA in sola lettura su `build/lift-full/ida/storm.lift.dll.i64`: `#454` = `storm_454`, VA `0x1502EA90`, 7 argomenti, `retn 0x1c`; `#457` = `storm_457`, VA `0x1502F500`, 3 argomenti, `retn 0x0c`; `#458` = `storm_458`, VA `0x1502E720`, 1 argomento, `retn 0x04`. Sessione IDA chiusa senza annotazioni. I VA suggeriti per #457/#458 non erano gli entrypoint corretti.

## Build e test

Comando richiesto eseguito con `TMPDIR=/dev/shm`: `./tools/build-entry-obj.sh --no-smoke --link-out build/starcraft-armv6-menu2`. Il primo tentativo ha superato il timeout mentre `opt` elaborava StarCraft; al successivo, `opt` ha usato la cache ma `llc` di StarCraft è stato un cache miss: 90.181 s. `storm` verify/opt/llc hanno usato la cache dopo la generazione IR; oggetto `storm_sdraw_arm.c` ricompilato. Il wrapper dello script in modalità full **non linka** `--link-out`; link effettuato separatamente:

```text
TMPDIR=/dev/shm ENTRY_DIR="$PWD/build/lift-full/entry" RUNTIME_OBJ_DIR="$PWD/build/lift-full/entry/runtime" ./tools/link-arm.sh build/starcraft-armv6-menu2
creato: build/starcraft-armv6-menu2
```

`python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-menu2`:

```text
voci: 101800   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0
```

`TMPDIR=/dev/shm make check`: **PASS**, compresi i test freestanding e `tests/slice/runner.sh`.

Smoke: `SC_SMACK=0 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 2s 25s qemu-arm-static -cpu arm1176 .../build/starcraft-armv6-menu2` in `build/asset-sandbox`; exit **71**, log verbatim completo `build/qa/run-arm-menu2.log`. Non arriva ai filmati: dopo `CreateSoundBuffer #3` si ferma prima su target non mappato `0x00418195` (chiamante `0x00415E2D`, ESP `0x021F9FF8`, EAX `0x00418195`). Non compare `0x2017008C`; ma il crash avviene prima dell'ingresso nel percorso che avrebbe testato tale fix, quindi non lo si considera prova che sia risolto. Non è stato verificato uno skip video <1 s né l'assenza del dialogo.

Estratto letterale del log:

```text
[dsound] CreateSoundBuffer #0x00000003 secondaria, byte 0x00040000, formato 0x56220210
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x00418195
  in mappa  = 0x00000001
  eip chiamante = 0x00415E2D
  ret (ARM) = 0x180CEC4C
  esp       = 0x021F9FF8
  eax       = 0x00418195
```

## Hash SHA-256

```text
b1985bcc42caf866640f84fac93030b148b7d8411cabaac25c4b38effc569ce1  tools/dispatch-ir.py
29f016568f914ad6d45def0d18847d3a4d8d5839b6e9cc11cf65bb76e7495847  runtime/arm/storm_sdraw_arm.c
52ab0dcfec31152a7c2b5469c57539c777087ae61739f74f824fc5786cfe2727  build/starcraft-armv6-menu2
0e150a1da560f30b79bbaee9b4d25aa21356b6a54dc537b36faed71a20cc7573  build/qa/run-arm-menu2.log
```

**Esito precedente (menu2):** fix agli ordinali generato e compilato; build e check passano. Il menu e il percorso skip restavano non verificati per il dispatch STOP a `0x00418195`. L’aggiornamento sotto sostituisce quell’esito.

## Aggiornamento — audit IDA totale e menu3

### Target `0x00418195` e flusso verso EAX

IDA sola lettura, DB `build/lift-full/StarCraft.orig.exe.i64`:

- `0x00415E2B` è `call eax`; `0x00415E2D` è l’istruzione successiva (`jmp short loc_415E61`), quindi il valore registrato come EIP chiamante è la continuazione dopo la call indiretta, non un entry point.
- A `0x00415E18`, `sub_415DC0` carica `eax = [esi+8]`; a `0x00415E2B` chiama quel callback. È un timer callback: IDA decompila `sub_415DC0` come iterazione dei nodi delle liste globali `dword_6D5EBC` / `dword_6D5EC0`.
- Il writer del campo è `sub_416090`: a `0x0041612C` scrive in `[node+8]` il quarto parametro stack (`arg_4`), ricevuto dal caller. Quindi il flusso runtime è **argomento callback del nodo timer → `[esi+8]` → EAX a `0x415E18` → `call eax` a `0x415E2B`**.
- `0x00418195` **non è un instruction head né una funzione IDA**: ricade nel mezzo dell’istruzione `mov ecx,[esi+18h]` iniziata a `0x00418194` (byte `8B 4E 18`; `0x418195` è il byte ModRM `4E`). La scansione degli xref a quell’indirizzo non trova riferimenti diretti. Non è quindi una funzione/entry da aggiungere alla SCI e aggiungerlo rischierebbe di forzare un ingresso sovrapposto nel mezzo di un’istruzione.

Questa è la provenienza certa di EAX fino al campo callback del timer. L’IDB statico non attribuisce `0x418195` a una funzione o a un riferimento statico: resta da identificare quale stato/call-site runtime abbia fornito quel quarto argomento anomalo; il valore non è stato “riparato” aggiungendo un target non valido.

### Audit totale `StarCraft.orig.exe.i64`

Scansione IDAPython dell’intero DB: 335.588 heads visitati; funzioni IDA in `[0x401000,0x4F7B18)` con esclusione del blocco CRT FP: **4.728**, **0** funzioni mancanti nella SCI preesistente. Scansione di tutti gli xref `dr_O` nel DB: **3.678** riferimenti verso code head validi nell’intervallo, **3.011** target unici; **242** target validi mancavano dalla SCI. Tutti i target aggiunti sono verificati con `is_code(target)` e `get_item_head(target)==target`; i 242 target mancanti sono presenti nella nuova `StarCraft.ll` generata. Nessun target del CRT FP externalizzato è stato reintrodotto.

- Backup richiesto: `build/lift-full/code_entry_points.sci.bak-all`.
- Aggiunti in blocco 242 `loc_<VA>` alla SCI; poi eseguiti solo `TMPDIR=/dev/shm ./tools/lift-full.sh srw exe` e `TMPDIR=/dev/shm ./tools/lift-full.sh ir exe`. Nessuna rigenerazione Storm.
- Il totale nel map ELF passa da 101.800 a **102.376** (le voci aggiuntive derivate dall’IR includono la propagazione dei 242 ingressi).

### Cache/declaration e build

`tools/dispatch-ir.py` ora rileva le `declare` già presenti nel modulo prima di aggiungere quelle richieste dalla tabella: la `StarCraft.entry.ll` finale contiene esattamente una dichiarazione ciascuna di `storm_313_arm` e `storm_334_arm`. `tools/build-entry-obj.sh` include ora `StarCraft.ll` e `storm.ll` nella chiave di cache di `dispatch-ir`, oltre allo script, così una variazione SCI/IR invalida gli artefatti derivati. Verifica: `StarCraft.entry.o` ricostruito; hash nuovo `39fd28e45b7c33eafe9fddd7335ea93828df3db9b304a6b57a10896922533087`.

Comando build (TMPDIR sempre `/dev/shm`):

```text
TMPDIR=/dev/shm ./tools/build-entry-obj.sh --no-smoke --link-out build/starcraft-armv6-menu3
[entry-obj] dispatch-ir: 5009 ms
[entry-obj] StarCraft:opt-verify: 3543 ms
[entry-obj] StarCraft:opt-default<O2>: 83377 ms
[entry-obj] StarCraft:llc: 95303 ms
[entry-obj] [cache hit] storm:opt-verify
[entry-obj] [cache hit] storm:opt-default<O2>
[entry-obj] [cache hit] storm:llc
[entry-obj] runtime: 16 da cache, 0 ricompilati (336 ms)
[entry-obj] tempo totale (full): 187922 ms
[entry-obj] ok (artefatti in build/lift-full/entry/)
```

Come nel menu2, la modalità full stampa il comando link ma non lo esegue; link eseguito esplicitamente:

```text
TMPDIR=/dev/shm ENTRY_DIR="$PWD/build/lift-full/entry" RUNTIME_OBJ_DIR="$PWD/build/lift-full/entry/runtime" ./tools/link-arm.sh build/starcraft-armv6-menu3
creato: build/starcraft-armv6-menu3
```

Verifiche: `python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-menu3` → `voci: 102376`, ordinate e univoche `si`, collisioni `0`; `TMPDIR=/dev/shm make check` → **PASS**.

### Smoke menu3

Comando: `cd build/asset-sandbox && TMPDIR=/dev/shm SC_SMACK=0 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 2s 25s qemu-arm-static -cpu arm1176 -L /home/enne2/dev/starcraft-armv6/build/sysroot /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu3`. Log completo: `build/qa/run-arm-menu3.log`; timeout/kill dopo 25 s, exit **137**. Importante: **non si ripresenta il STOP a `0x00418195`**, e non sono comparsi STOP, API Win32/UNK o SCode error nei 25 s. Il log arriva a `CreateSoundBuffer #3` e poi resta senza ulteriori messaggi: non prova da solo l’ingresso nel loop visibile del menu (`PeekMessageA`/`[fps]` non loggati). Con `SC_SMACK=0` il percorso prosegue oltre il precedente target; l’assenza dei filmati è coerente coi wrapper, ma il log non dà un timestamp separato che misuri il salto in <1 s.

Estratto verbatim del log:

```text
[arm-game] entry: start() del CRT a 0x00404C21
[arm-game] DllMain di storm (DLL_PROCESS_ATTACH, hinst=0x15000000)
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
[arm-game] DllMain di storm -> 1
[risorse] caricato come FILE (sola lettura risorse): local.dll -> handle 0x71000001, .rsrc 0x00008000+0x00007218
[com] SetDisplayMode 0x028001E00x00000008
[host] init: video driver = dummy
[host] finestra creata
[com] CreateSurface caps=0x00000200 size=0x028001E0 PRIMARY
[host] present: percorso GPU (renderer + texture)
[dsound] DirectSoundCreate -> IDirectSound (mixer host)
[dsound] SetCooperativeLevel flags=0x00000002
[host] audio aperto: driver = dummy, 0x00005622 Hz, canali 0x00000002
[dsound] CreateSoundBuffer #0x00000001 PRIMARIA, byte 0x00000000, formato 0x56220210
[dsound] CreateSoundBuffer #0x00000002 secondaria, byte 0x00008380, formato 0x56220110
[dsound] CreateSoundBuffer #0x00000003 secondaria, byte 0x00040000, formato 0x56220210

[smoke] exit=137
```

### Hash SHA-256 aggiornati

```text
1cdef366455d7b5cee02f8e5b8ce0ff6840b1035bb82d1d51540b7749642e446  build/lift-full/code_entry_points.sci
7fb608ed8363b93219dd0d50b8295eb9576d89b6dd01b3ca39c5af731e6156f6  build/lift-full/code_entry_points.sci.bak-all
eff6fd29c5e7a15cc1cb9b0e443de6e24eaed3a3fb5673a7662d5f4dbbae3c75  build/lift-full/StarCraft.ll
39fd28e45b7c33eafe9fddd7335ea93828df3db9b304a6b57a10896922533087  build/lift-full/entry/StarCraft.entry.o
bd54eb50ef5bedaa4a6944441a47121ec4e4a43914aa6db4559ea77043860ce9  build/starcraft-armv6-menu3
50c4280a7c92214fc9c89f91aefda9a3bd6fffd384f892a078c61834884808ac  build/qa/run-arm-menu3.log
b1985bcc42caf866640f84fac93030b148b7d8411cabaac25c4b38effc569ce1  tools/dispatch-ir.py
963c8bcd94d0f47c30f5f4651a73294416a0e95f87e056554d0a652b4dc625aa  tools/build-entry-obj.sh
```
