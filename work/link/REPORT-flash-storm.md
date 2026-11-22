# REPORT — flash-storm: risoluzione del simbolo UNK `crt_unlifted_15040E07` e avanzamento al menu

Sessione: 20/11/2026, dalle 16:09 Europe/Rome. Proprietario runtime unico ARMv6.
Perimetro e vincoli: `work/link/TASK-flash-runtime.md` (nessun guest, niente git, niente agenti,
niente sudo/install, solo `runtime/arm`, `tools/` di build del port, artefatti `build/`, questo
report). Oracolo: IDA Pro sul binario x86 originale, sessione del progetto.

Punto di partenza (handoff): `work/link/REPORT-flash-crt.md` §9 + `work/link/REPORT-flash-game.md`
(§0.3-0.5: `.rsrc` mappata, `DllMain` di storm parte, stop su `crt_unlifted_15040E07`).

**Stato in una riga (apertura, 16:10):** il candidato `build/starcraft-armv6-storm` (smoke
`build/qa/run-ea616f62.log`, 16:02) si ferma su `[arm-bridge] API NON IMPLEMENTATA:
crt_unlifted_15040E07` con ritorno a `0x187B5E4C`; il corpo SSE2 di ripiego e la voce nella mappa
esistono, quindi l'ipotesi da verificare è che il chiamante non passi affatto dalla mappa VA ma
usi un riferimento diretto al simbolo (rilocazione / pointer fixup).

---

## 1. Chi risolve `crt_unlifted_15040E07`: **il fixup di un puntatore in `.data`**, non la mappa VA

### 1.1 Identità della build (verificata prima di ogni diagnosi)

```
$ ls -l build/starcraft-armv6-storm build/lift-full/entry/storm.entry.o \
        build/lift-full/entry/StarCraft.entry.o build/qa/run-ea616f62.log
-rwxr-xr-x 21204000 20 nov 16.02 build/starcraft-armv6-storm
-rw-r--r--  4800864 20 nov 16.02 build/lift-full/entry/storm.entry.o
-rw-r--r-- 19135896 20 nov 16.02 build/lift-full/entry/StarCraft.entry.o
-rw-r--r--      606 20 nov 16.02 build/qa/run-ea616f62.log
```

Gli oggetti sono quelli del log `build/qa/build-entry-obj.log` (16:02, `--map-extra 0x15040E07=…`
compreso: la riga 65 di `tools/build-entry-obj.sh` è del 15:59, gli oggetti del 16:02) e il
binario è il link delle 16:02 (`build/qa/link-storm.log`). Nessun artefatto obsoleto: la
`--map-extra` **c'è** nella tabella della dispatch e non è lei il problema.

### 1.2 La catena, misurata (non ipotizzata)

1. **Oracolo** (`storm.lift.dll.i64`, IDA, decompilato `sub_1500810F` per intero): è l'`_initterm`
   del CRT della DLL. Cammina la tabella `&unk_150590F0 … < &dword_15059104` e chiama
   `result = (*v2)()` a **0x1500813D** (uscita 0x1500813F). Il contenuto della tabella in immagine
   (IDA, dword a 0x150590F0..0x15059104): `0, 0x15005A49, 0x1500D718, 0x1500DD48, 0x15040E07`.
2. **Il port**: nel data blob della DLL il campo che corrisponde a VA **0x15059100** non contiene
   0x15040E07 ma **l'indirizzo host del simbolo esterno** `crt_unlifted_15040E07` (il lift
   rappresenta i puntatori a funzione come `i32 ptrtoint (void(%_cpu*)* @<simbolo> to i32)`,
   scelta dichiarata: «i puntatori a funzioni restano host-address, previsto dalla dispatch»).
   Verifica sul binario (ricerca dei byte dell'indirizzo host in tutte le sezioni):

   ```
   $ llvm-nm build/starcraft-armv6-storm | grep crt_unlifted_15040E07
   00000000189c3990 t crt_unlifted_15040E07
   ricerca del dword 0x189C3990 nel file -> 1 sola occorrenza "dati":
     file 0xE29100 -> .guest_storm_data VA 0x15059100      (= la voce della tabella IDA)
   ```
3. **Il salto**: la `tcall reg` del guest arriva a `reasm_indirect`, che per un target *dentro la
   finestra del .text del port* (`[reasm_text_lo, reasm_text_hi)`) fa `bx` così com'è
   (`runtime/arm/indirect_dispatch.c:93-95`): 0x189C3990 è nel `.text` del port, quindi la
   tabella VA non viene nemmeno consultata. L'atterraggio è lo stub UNK generato dal bridge.
4. **La prova a runtime** (`SC_BRIDGE_TRACE=1`, stesso binario, `ulimit -c 0`):

   ```
   $ SC_BRIDGE_TRACE=1 timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot \
        ./build/starcraft-armv6-storm                      -> exit 3
   …
   HeapAlloc @ 0x187c8158
   crt_unlifted_15040E07 @ 0x187b5e4c        <- EIP guest di ritorno
   [arm-bridge] API NON IMPLEMENTATA:crt_unlifted_15040E07 (ritorno a 0x187B5E4C
   ```

   `llvm-addr2line` su 0x187b5e4c: `loc_1500813F` (`storm.entry.ll`), cioè **il call-site
   dell'`_initterm`**: il chiamante è esattamente quello che l'oracolo indica.

**Conclusione.** La `--map-extra 0x15040E07=__sse2_available_init_storm` copre il caso «qualcuno
salta al *VA* 0x15040E07»; il percorso reale è «qualcuno chiama il puntatore in `.data` 0x15059100»,
che il lift ha già convertito in un **riferimento diretto** al simbolo esterno. Due nomi per lo
stesso VA (`crt_unlifted_15040E07` dalla `.sci`, `__sse2_available_init_storm` dalla mappa) e solo
il secondo aveva una implementazione: il primo atterrava sullo stub UNK. Il difetto non è nella
mappa né nel corpo SSE2 (che è corretto e provato, §1.3): è che **il simbolo del fixup non ha un
corpo**.

### 1.3 Semantica provata (IDA, letta per intero)

`sub_15040E07` (0x15040E07–0x15040ECF, decompilata per intero): salva `dword_1505A03C` nel frame,
azzera vendor/family, testa il bit ID di EFLAGS (`0x15040E23-0x15040E32`), e **solo se** il bit è
commutabile esegue `cpuid` (`0x15040E38`, `0x15040E4B`); poi **azzera sempre**
`dword_1505FC20` (0x15040E54) e `dword_1505FC1C` (0x15040E5B), e ritorna **0** (0x15040EC7).
Sull'ARM1176 non esiste CPUID ⇒ `v2 == v0`, `v13 == 0`, i due flag restano 0 e il ritorno è 0:
è il ramo che l'originale prende su una CPU senza CPUID. ABI: `int __cdecl ()`, **0 argomenti,
nessun `ret N`** (il chiamante poppa). Corpo già presente e corretto in
`runtime/arm/crt_init_arm.c:75-80` (`__sse2_available_init_storm_body`).

---

## 2. Fix applicato al simbolo UNK (meccanismo, non stub) — e verifica

### 2.1 Perché non bastava la mappa, e cosa è stato cambiato

Il simbolo del fixup doveva avere **un corpo**. Il meccanismo previsto dal progetto per gli
import del guest (`runtime/arm/bridge_table.def` → `gen_shims_bridge.py` → `gen_bridge_stubs.py`)
già distingue `ENTRY` (implementazione C con ABI guest) da `UNK` (trampolino che logga e si ferma):
si è usata quella via, con l'ABI guest provata da IDA (cdecl, 0 argomenti, nessun pop del callee):

| file | modifica |
|---|---|
| `runtime/arm/bridge_table.def` | `ENTRY(crt_unlifted_15040E07, 0, imp_crt_unlifted_15040E07)` + commento con VA IDA |
| `runtime/arm/bridge_arm.c` | `imp_crt_unlifted_15040E07()` chiama `__sse2_available_init_storm_body()` (corpo unico, `crt_init_arm.c`, già provato: azzera 0x1505FC20/0x1505FC1C, ritorna 0) |
| `runtime/arm/gen_bridge_stubs.py` | regola di precedenza: una voce **con implementazione** prevale sulla `UNK` generata per lo stesso nome (prima era errore fatale). Serve perché `bridge_shims.def` è generato da `$need - $defined` e può ancora contenere la UNK di un simbolo poi implementato a mano. `SHIM`/`HAND` continua a prevalere su `UNK`; due voci dello stesso tipo restano un errore |
| `runtime/arm/indirect_dispatch.c` | il percorso di STOP stampa anche `ebx/esi/edi/ebp` e le prime 12 parole della pila emulata con il VA guest corrispondente (nessun costo nel percorso normale; nessun gdb) |

Rigenerazione dei tre file generati (comando esatto, nessun repo esterno):

```bash
python3 runtime/arm/gen_bridge_stubs.py --def runtime/arm/bridge_table.def \
  --also build/runtime-arm/game/bridge_shims.def --only-needed build/runtime-arm/game/need.txt \
  --out build/runtime-arm/game/bridge_stubs.S --csym-out build/runtime-arm/game/bridge_symbols.c \
  --table-out build/runtime-arm/game/bridge_table.c
# -> "implementazione prevale su UNK per: crt_unlifted_15040E07"
# -> 324 stub (HAND 1, SHIM 291, UNK 32)   [era UNK 33]
```

Poi solo gli oggetti toccati (nessun `opt`/`llc`: l'IR non cambia) e il rilink:

```bash
CC="clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard \
    --sysroot=build/sysroot -O1 -g -Wall -Wno-unused-parameter -I build/runtime-arm/game -I runtime/arm"
$CC -c -o build/runtime-arm/game/bridge_table.o   build/runtime-arm/game/bridge_table.c
$CC -c -o build/runtime-arm/game/bridge_symbols.o build/runtime-arm/game/bridge_symbols.c
$CC -c -o build/runtime-arm/game/bridge_stubs.o   build/runtime-arm/game/bridge_stubs.S
$CC -c -o build/runtime-arm/game/bridge_arm.o     runtime/arm/bridge_arm.c
cp -f build/runtime-arm/game/{bridge_table,bridge_symbols,bridge_stubs,bridge_arm}.o \
      build/lift-full/entry/runtime/
ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
  ./tools/link-arm.sh build/starcraft-armv6-crtinit
```

### 2.2 Verifica

```
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-crtinit
voci: 92935   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0            -> exit 0
$ ulimit -c 0; timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot \
      ./build/starcraft-armv6-crtinit                              -> exit 3 (stop nominato, più avanti)
[arm-game] bridge: 324 import, 32 UNK dichiarati        <- era 33
[arm-game] DllMain di storm (DLL_PROCESS_ATTACH, hinst=0x15000000)
[arm-game] DllMain di storm -> 1                       <- il DllMain ora RITORNA (prima si fermava)
[arm-game] tabella crypt di storm non standard a 0x2013008C
[arm-game] azzerata: la ricostruisce SFileOpenArchive col seed standard
[reasm-arm] GetModuleHandleA di un modulo non presente: …\local.dll
[reasm-elf] API NON IMPLEMENTATA: …\local.dll (chiamante 0x189F097C) -> stop controllato
```

Il muro `crt_unlifted_15040E07` **non c'è più**; il DllMain di storm completa (ritorna 1) e il CRT
prosegue nel codice del gioco.

## 3. Muro successivo: `LoadLibraryA(<cwd>\local.dll)` — dipendenza reale, non un bug del port

### 3.1 Oracolo (StarCraft.orig.exe.i64)

* `_WinMain@16` (0x4E0AE0) chiama **`sub_4DA7D0`** (0x4DA7D0-0x4DA896) come **prima** azione.
* `sub_4DA7D0`: `GetModuleFileNameA(hInst, buf, 0x104)` → `strrchr(buf,'\\')` → scrive
  `"local.dll"` (stringa a **0x4FFF08**, unico xref 0x4DA812) dopo l'ultimo backslash →
  `hInstance = LoadLibraryA(buf)`; **se fallisce**: `DialogBoxParamA(hInst,0x6A,…)`, `exit(1)`.
* `hInstance` (0x6D5DE4) è usato in 41 punti come `hInst` delle risorse: `sub_41F1D0` fa
  `LoadStringA(hInstance, 3, buf, 16)` e ne ricava l'id di lingua (fallback 1033).
  ⇒ `local.dll` è il **modulo di localizzazione** (stringhe/dialoghi), obbligatorio: il gioco non
  ha un percorso "senza".

### 3.2 Il port lo sa già fare: moduli di sola risorsa dal file

`win32_ui_load_library()` (`build/runtime-arm/game/shim/tools/elf/win32_ui.c:239-295`) apre il PE
dal filesystem (`reasm_open_ci`, case-insensitive), ne legge `.rsrc` e lo registra come modulo
`is_file=1`: le API risorse leggono dal buffer del file, **il codice del modulo non viene eseguito**.
Nessuna modifica al codice è servita: il test smoke va eseguito **con la cwd della sandbox degli
asset** (vincolo TASK §2: asset locali read-only, symlink, cwd annotata).

```bash
cd build/asset-sandbox                     # 48 symlink read-only verso ~/dev/final-starcraft/Starcraft
ln -sfn ~/dev/final-starcraft/Starcraft/Local.dll local.dll   # nome MINUSCOLO: vedi §3.3
ulimit -c 0; timeout 120 qemu-arm-static -cpu arm1176 -L ../../build/sysroot \
   ../../build/starcraft-armv6-crtinit                                     -> exit 71
[risorse] caricato come FILE (sola lettura risorse):
   …/build/asset-sandbox/local.dll -> handle 0x71000001, .rsrc 0x00008000+0x00007218
```

### 3.3 Nota di setup (da verificare, non ancora diagnosticata a fondo)

Con il solo symlink `Local.dll` (nome reale dell'asset) la `reasm_open_ci` **non ha aperto** il
file; con un symlink aggiuntivo di nome `local.dll` sì. Da accertare se è il resolver ad aggirare
i symlink o il caso; per ora la sandbox porta entrambi i nomi. *(Aperto.)*

### 3.4 Muro attuale, con evidenza esatta

Dopo il caricamento di local.dll il gioco esegue il controllo di versione del file
(`sub_4DA440`, IDA: `GetModuleFileNameA` → `GetFileVersionInfoSizeA` → `storm_401` (malloc) →
`GetFileVersionInfoA` → `VerQueryValueA("\\")` → `sub_41F1B0(fmt="Version %d.%d.%d")` →
`storm_403` (free)) e poi si ferma nella dispatch:

```
VerQueryValueA … = 0x00000001
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x00407828      in mappa = 1
  ret (ARM) = 0x005122C8      esp = 0x021FF964
  eax=0 ecx=7 edx=0x10 ebx=0x20280056 esi=0x005122C8 edi=0x00501ABD ebp=0x021FF9F0
  pila: 0x005122C8 0x005122C8 0x20280088 0x00100010 0x00100010 0x01010010 0x01010101 …
```

Fatti misurati:
* `0x00407828` in IDA cade **dentro** un'istruzione (`not ebx` a 0x407827, 2 byte) di
  `___sbh_resize_block` (0x4077A0-0x407A75): non è un entry point né un label SRW
  (`loc_40781E`, `loc_407831`, `loc_407838` esistono; `loc_407828` no).
* Nessuna occorrenza del dword `0x00407828` nell'immagine originale **né** nel binario del port
  (tranne una parola non inizializzata in `.bss`): il valore è **calcolato a runtime**.
* Lo stato dei registri (ecx=7 < 0x20, edx=0x10, ebx=blocco heap 0x20280056) coincide con il punto
  `cmp ecx,20h / jnb` a 0x407814-0x40781C di `___sbh_resize_block`: si è dentro il percorso
  **heap** (resize/alloc) attivato dalla formattazione della stringa di versione.
* La parola sotto il target (`[esp]`) è 0x005122C8 = `"internal version unknown"` (0x5122C8, stringa
  usata da `sub_4DB280`), non un indirizzo di ritorno: la pila emulata non contiene la catena dei
  chiamanti in questo punto.

Prossimo passo indicato: capire **quale** `tcall` produce 0x407828. La via più corta è un anello
diagnostico env-gated nella dispatch (ultimi N target indiretti + `reasm_indirect_target`), oppure
confrontare il frammento `loc_407814`/`loc_40781E` tradotto con il disassemblato IDA per cercare un
`cmov`/`select` che SRW ha fuso (in `loc_407814` c'è già un `;cmovult ecx, 32, tmpcnd, 0, 1` che non
esiste nel binario: è una fusione di SRW).

## 4. Stato per l'handoff (aggiornato a fine sessione)

Artefatti (nessuno sostituisce il candidato in uso `build/starcraft-armv6-storm`):

| file | sha256 | note |
|---|---|---|
| `build/starcraft-armv6-crtinit` | `59d2b8517584db1d2c453df4b52816fdd1157f27ea8d3de4038eb6c4945fd67b` | fix UNK, **senza** i dump diagnostici |
| `build/starcraft-armv6-crtinit3` | — | fix UNK + dump registri/pila nella STOP |

Log: `build/qa/run-crtinit.log` (senza asset), `build/qa/run-crtinit-trace.log`,
`build/qa/run-crtinit-assets*.log` (sandbox asset), `build/qa/bak-storm/` (i tre file generati
precedenti).

---

## 5. Gate e candidato per QA (verificati su build rifatta dai sorgenti)

### 5.1 Ricetta riproducibile completa (una sola catena, ~3m20)

```bash
cd /home/enne2/dev/starcraft-armv6
./tools/build-entry-obj.sh                      # rigenera IR post-processata, opt+llc, layout,
                                                # byte PE, bridge e runtime del port
ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
  ./tools/link-arm.sh build/starcraft-armv6-sse2crt
python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-sse2crt
```

`tools/build-entry-obj.sh` ha un passo nuovo (**2d**) che rigenera tabella e stub del bridge **dai
sorgenti** invece di copiare l'oggetto da `build/runtime-arm/game/obj/`: senza di esso `$OBJ`
(uscita dello stage 8 di `build-runtime-arm.sh`, non rieseguibile qui perché richiede
`~/dev/sc-reasm`) può essere più vecchio della `.def` e **lo stop UNK ricompare** — misurato: primo
giro completo del 16:18 dava ancora «33 UNK» e lo stop `crt_unlifted_15040E07`, pur con il corpo
presente e linkato. Con il passo 2d: `implementazione prevale su UNK per:
crt_unlifted_15040E07`, `324 stub (HAND 1, SHIM 291, UNK 32)`.

### 5.2 Esiti

```
$ ./tools/build-entry-obj.sh                     -> EXIT=0   (3m18s, log build/qa/build-entry-obj-sse2crt.log)
    StarCraft.entry.o 19135896 byte, storm.entry.o 4800864 byte   (identici ai precedenti:
    la rigenerazione dell'IR è deterministica)
$ ... ./tools/link-arm.sh build/starcraft-armv6-sse2crt   -> creato, link-errors.txt VUOTO
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-sse2crt
voci: 92935  primo VA 00401000  ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0            -> EXIT=0
$ timeout 300 make check                                          -> EXIT=0
PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack
      bridge; ARMv6/VFPv2 attributes verified.
$ cd build/asset-sandbox && ulimit -c 0; timeout 120 qemu-arm-static -cpu arm1176 \
    -L ../../build/sysroot ../../build/starcraft-armv6-sse2crt   -> EXIT=71 (stop nominato)
[arm-game] bridge: 324 import, 32 UNK dichiarati
[arm-game] DllMain di storm -> 1
[risorse] caricato come FILE (sola lettura risorse): …/asset-sandbox/local.dll
    -> handle 0x71000001, .rsrc 0x00008000+0x00007218
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target = 0x00407828 … (stato completo in §3.4)
```

### 5.3 Candidato e hash

```
sha256  build/starcraft-armv6-sse2crt   467ce91d8a98eacc3edb4735fdd8d1c1a2bb7ab00aefa60ea4876c8738ef5ca9
        21.218.524 byte, /home/enne2/dev/starcraft-armv6/build/qa/SHA256-sse2crt.txt
log:    build/qa/run-sse2crt.log  (92 righe; sandbox asset, cwd annotata)
```

Il candidato in uso dal worker QA (`build/starcraft-armv6-storm`, `4e85…`/`59d2…` storici) **non è
stato sovrascritto**: `sse2crt` è una copia nuova e distinta. Intermedi della sessione:
`starcraft-armv6-crtinit` (sha256 `59d2b851…`, fix senza dump diagnostici), `-crtinit3`/`-crtinit4`
(aggiunta progressiva dei dump). **Non** avviare un terzo rebuild full in questa sessione: gli
oggetti in `build/lift-full/entry/` sono allineati al candidato `sse2crt`.

### 5.4 Stato dei file generati/toccati (per chi riprende)

Toccati (sorgenti del port): `runtime/arm/bridge_table.def`, `runtime/arm/bridge_arm.c`,
`runtime/arm/gen_bridge_stubs.py`, `runtime/arm/indirect_dispatch.c` (solo percorso di STOP +
anello diagnostico), `tools/build-entry-obj.sh` (passo 2d), questo report.
Rigenerati (derivati, non a mano): `build/runtime-arm/game/{bridge_stubs.S,bridge_symbols.c,
bridge_table.c}` e `build/lift-full/entry/{bridge_stubs.S,bridge_symbols.c,bridge_table.c}`,
`build/lift-full/entry/{StarCraft,storm}.entry.o`, `build/lift-full/entry/runtime/*`,
`build/lift-full/entry/guest_data.ld` + `guest_pe_bytes.*`.
Fallback conservati: `build/qa/bak-storm/` (i tre file generati *prima* del fix),
`build/lift-full/entry/storm.entry.o.pre-sse2crt`, `build/lift-full/entry/*.prev`/`.bak`.
Log marcati `.STALE-OBJ`: `build/qa/run-sse2crt-*assets*.log.STALE-OBJ` = giro in cui lo stop UNK
era tornato per l'oggetto vecchio in `$OBJ` (spiegato in §5.1, non è una regressione del fix).
`CHECKPOINT.md` intatto.

### 5.5 Sandbox asset (per QA)

`build/asset-sandbox/` = 48 symlink **read-only** verso `~/dev/final-starcraft/Starcraft` (nessuna
copia, nessuna modifica degli asset). Contiene anche `local.dll` minuscolo come workaround della
nota §3.3. Serve solo a superare la dipendenza `local.dll`; senza sandbox lo stop è
`LoadLibraryA(<cwd>\local.dll)` (asset mancante), che è il comportamento corretto.

---

## 6. Decisioni autonome

1. **Fix al simbolo, non alla mappa.** Il fixup in `.data` non può essere risolto dalla tabella VA
   (il target è già un indirizzo host): si è data un'implementazione al simbolo che il lift usa.
   La `--map-extra 0x15040E07=…` resta, perché copre l'altro percorso (salto al VA nudo).
2. **Corpo condiviso**, non duplicato: `imp_crt_unlifted_15040E07` chiama
   `__sse2_available_init_storm_body` (già provato da IDA). Nessuna nuova traduzione, nessuno stub
   generico, nessuna disattivazione.
3. **Precedenza HAND/SHIM su UNK** nel generatore del bridge: era l'unico modo di rendere
   riproducibile il passaggio UNK→implementazione senza rigenerare `bridge_shims.def` (che
   richiede `~/dev/sc-reasm`, fuori perimetro).
4. **Passo 2d in `build-entry-obj.sh`**: la catena del candidato non deve dipendere da un `$OBJ`
   prodotto da uno stage non rieseguibile qui. Costo: +1 s.
5. **Sandbox di asset con symlink** invece di copie: vincolo TASK §2 rispettato, cwd annotata.
6. **Dump diagnostici nel percorso di STOP** (registri, 12 parole di pila, anello 16 target):
   nessun costo a runtime normale, nessun gdb, e sono ciò che ha permesso di localizzare il muro
   §3.4. Restano nel candidato.
7. Non è stato aperto nessun DB IDA di altri lavori: usati solo `build/lift-full/ida/storm.lift.dll.i64`
   (sessione `7d2b1db6`) e `build/lift-full/StarCraft.orig.exe.i64` (sessione `fb9d63aa`), entrambi
   del progetto. Nessun salvataggio/rinomina nei DB.

## 7. Handoff — cosa resta, in ordine

1. **Muro `0x00407828`** (§3.4): il valore è calcolato a runtime, cade dentro un'istruzione
   (`not ebx`) di `___sbh_resize_block`, e l'ultimo target indiretto prima dello stop è il ritorno
   della `storm_401` (malloc) a `0x4DA495`; lo stato dei registri combacia con il punto
   `cmp ecx,20h/jnb` di `___sbh_resize_block` (0x407814-0x40781C). Ipotesi da verificare *in
   quest'ordine*: (a) `storm_403` (free) → heap free path, che è la chiamata subito dopo
   `sub_41F1B0`/`__vsnprintf`; (b) una tabella di switch in `.rdata` i cui bersagli sono VA nudi
   non simbolizzati (in quel caso serve il meccanismo `code_entry_points.sci`/`--map-extra`).
   Strumento già pronto: l'anello diagnostico stampa gli ultimi 16 target con il VA corrispondente.
2. **Nota §3.3** (simlink `Local.dll` maiuscolo non aperto da `reasm_open_ci`): capire se è il
   resolver case-insensitive o i symlink; workaround attuale = doppio nome nella sandbox.
3. Proseguire verso il menu: dopo il muro heap ci si aspetta MPQ (`SFileOpenArchive` sugli asset
   della sandbox), GDI/DirectDraw, creazione finestra. I 32 UNK residui sono la worklist: ognuno è
   uno stop *con nome*.
4. Il ramo QA è separato: nessuna modifica fatta lì; la VM non è stata aperta.
