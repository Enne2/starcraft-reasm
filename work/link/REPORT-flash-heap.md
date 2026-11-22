# REPORT — flash-heap: produttore del VA `0x00407828` (muro dopo VerQueryValueA)

Sessione: 20/11/2026 dalle 16:23 Europe/Rome. Writer runtime unico ARMv6.
Perimetro: `work/link/TASK-flash-runtime.md`. Nessun guest, git, rete, sotto-agenti, repo originali.
Oracolo: IDA Pro, solo i DB del progetto (`StarCraft.orig.exe.i64` = `fb9d63aa`,
`storm.lift.dll.i64` = `7d2b1db6`).

## 0. Stato di partenza (ereditato, verificato sui file)

* Candidato: `build/starcraft-armv6-sse2crt`, sha256
  `467ce91d8a98eacc3edb4735fdd8d1c1a2bb7ab00aefa60ea4876c8738ef5ca9` (21.218.524 byte, 16:22).
* Catena: `./tools/build-entry-obj.sh` EXIT=0 (3m18), `./tools/link-arm.sh` senza errori,
  `dispatch-ir.py --dump-map` 92935 voci / 0 collisioni, `make check` EXIT=0, smoke arm1176
  EXIT=71 (stop nominato) — log `build/qa/{build-entry-obj-sse2crt,link-storm,make-check-storm}.log`,
  `build/qa/run-sse2crt.log`.
* Muro: `target = 0x00407828`, `in mappa = 1`, `eip chiamante = 0`, `ret(ARM) = 0x005122C8`
  (stringa "internal version unknown"), `esp = 0x021FF964`, ecx=7, edx=0x10, ebx=0x20280056,
  esi=0x005122C8, edi=0x00501ABD, ebp=0x021FF9F0, eflags=0x286.
* Ultima chiamata bridge registrata (trace 16:13): `VerQueryValueA @ 0x18674108` con
  `(0x20280088, 0x005016CC, 0x021FFC44) = 1`; **niente** fra questa e lo STOP.

## 1. Lavoro in corso

1. Decompilare per intero `sub_4DA440` (controllo versione) e il percorso
   `VerQueryValueA → sub_41F1B0 → __vsnprintf → heap`.
2. Identificare il **produttore** del VA 0x00407828 (nessuna occorrenza del dword nell'immagine:
   è calcolato). Candidati da discriminare con evidenza: (a) `ret`/`pop` di un indirizzo di ritorno
   corrotto per squilibrio di pila ABI; (b) target di un salto indiretto calcolato (tabella/switch);
   (c) valore di ritorno di una shim bridge che altera l'ESP emulato.
3. Riparare la semantica IR/ABI alla fonte + test focalizzato; niente entry point dentro
   un'istruzione.

## 2. Gate (ultimo esito valido, 16:22)

| gate | esito |
|---|---|
| `./tools/build-entry-obj.sh` | EXIT=0, `bridge: 324 import, 32 UNK dichiarati` |
| `./tools/link-arm.sh build/…-sse2crt` | creato, `link-errors.txt` vuoto |
| `dispatch-ir.py --dump-map` | 92935 voci, ordinate e univoche, 0 collisioni host/VA |
| `timeout 300 make check` | EXIT=0 (PASS ordinal PE + Wine x86 + IR sotto ARM1176) |
| smoke `qemu-arm-static -cpu arm1176 -L build/sysroot` in `build/asset-sandbox` | EXIT=71 (stop nominato) |

## 3. Handoff / prossimo passo

(aggiornato in fondo alla sessione — vedi §4)

## 1. Produttore del VA 0x00407828 — catena misurata (16:26-16:55)

Strumenti nuovi (diagnostica, nessun cambio di semantica nel percorso normale):
`reasm_indirect_esp` scritto da `runtime/arm/indirect_dispatch.S`; anello a 8 record
`(target, ESP prima dell'arco, [esp], [esp-4])` in `runtime/arm/indirect_dispatch.c`.
Log: `build/qa/run-esp.log` (target), `build/qa/run-esp2.log` (con `[esp-4]`).
Nessun gdb: `qemu-arm-static -d exec -D /tmp/exec.log` (70 MB, 962304 TB) e `llvm-nm`/`llvm-addr2line`
sul binario del port per attribuire i PC host ai proc tradotti.

1. L'arco che si ferma **non è una `ret`**: `[esp-4] = 0x00000007 ≠ target`. È il `jmp [mem]`
   (`load tmp1; tcall tmp1`) di `proc loc_40E17D` = `jmp ds:jpt_40E17D[eax*4]` (IDA **0x40E17D**,
   dentro `__output`, lo stesso `__output` che formatta "Version %d.%d.%d").
2. Codice ARM del port a 0x180924DC: `ldr r2,=<base>; lsl r1,eax,#2; ldr r1,[r1,r2];
   str r1,[@reasm_indirect_target]; b reasm_indirect`. Base letta dal letterale **0x407DC0**
   (non 0x40E8D2), primo dword = **0x00407828** (gli altri 7 = 0x40E184/0x40E1A1/0x40E1ED/
   0x40E22E/0x40E237/0x40E275/0x40E356, **identici a IDA**).
3. Perché 0x407DC0: in `build/lift-full/entry/StarCraft.entry.ll` il codice usa
   `@loc_40E8D2`, che è `private alias i8, getelementptr (i8, bitcast (%__text* @_text), i32 4532)`.
   `@_text` (blob dei byte grezzi di `.text`) è collocato a **VA 0x406C0C**: 0x406C0C + 4532
   (0x11B4) = 0x407DC0 ✓.
4. Perché `0x00407828`: l'entry 0 della tabella non è un VA ma
   `i32 ptrtoint (i8* @loc_40E314 to i32)`, e `@loc_40E314` è **anch'esso un alias in `@_text`**
   (offset 3100): 0x406C0C + 3100 = **0x407828** ✓. Il lift ha simbolizzato il puntatore a codice
   come puntatore ai *byte grezzi* del codice, che nel blob sono compattati e non hanno alcun
   rapporto numerico con il VA.
5. Le altre 7 entry restano VA nudi (0x40E184 …), quindi la dispatch le risolverebbe con la mappa
   VA→entry — **ma quei blocchi non sono liftati affatto**.

## 2. Causa radice: i `jump table` interni a `.text` non vengono seguiti dal lift

Oracolo IDA (`fb9d63aa`, `ida_nalt.get_switch_info` su ogni funzione): **338 siti di switch**,
1256 bersagli distinti di codice; **solo 72** sono proc nel nostro IR. **1184 blocchi di codice
non sono liftati** (nessuno di essi è in `code_entry_points.sci`, che elenca solo gli *start* di
funzione IDA). `__output` 0x40E17D è il primo che il gioco incontra.

Prossimo passo (in corso): dichiarare i bersagli di jump table come code entry point (stessa
sorgente-oracolo IDA già usata per `code_entry_points.sci`), rigenerare `srw`/`ir` con
`tools/lift-full.sh` (i tool sono nel vendor del progetto, `build/lift-full/bin`), poi
`build-entry-obj.sh` + link + gate.

## 3. Fix applicato: i bersagli dei jump table sono code entry point (oracolo IDA)

`build/lift-full/code_entry_points.sci` conteneva solo gli *start* di funzione IDA (6203 voci):
i blocchi `case` di uno switch non sono start di funzione, e SRW **non segue i jump table interni
a `.text`** (tabelle a indirizzo non allineato, es. `jpt_40E17D` a 0x40E8D2). Misura con l'oracolo
(`ida_nalt.get_switch_info` su tutte le 5115 funzioni IDA di StarCraft.exe):

| dato | valore |
|---|---|
| siti di switch riconosciuti da IDA | 338 |
| bersagli distinti di codice | 1256 |
| già presenti come proc nel nostro IR | 72 |
| **mancanti (blocchi non liftati)** | **1184** |
| mancanti già elencati in `code_entry_points.sci` | 0 |

Aggiunte al file le 1256 voci `loc_<VA>` (tabella a 4 byte, shift 0, non indiretta/sottrattiva;
l'intestazione del file documenta la provenienza). Rilift:

```
$ time ./tools/lift-full.sh srw exe   -> SRW exe: exit=0 (2,1 s)
$ time ./tools/lift-full.sh ir  exe   -> IR exe: StarCraft.ll (2.243.958 righe, 4,2 s)
proc: 71206 -> 75027 (+3821, i blocchi case + le loro continuazioni)
```

Effetto sull'entry 0 della tabella (`build/lift-full/StarCraft.ll`, blob `@_text`):

```llvm
; PRIMA: i32 ptrtoint (i8* @loc_40E314 to i32)          -> alias nei byte grezzi = 0x00407828
; DOPO : i32 ptrtoint (void(%_cpu*)* @loc_40E314 to i32) -> indirizzo dell'ingresso tradotto
```

`@loc_40E314` non è più un alias in `@_text` ma un `define private fastcc void @loc_40E314(%_cpu*)`:
il valore nel campo della tabella è l'indirizzo dell'ingresso ARM, che `reasm_indirect_resolve`
accetta con la regola 2 (dentro `[reasm_text_lo, reasm_text_hi)`). Le altre 7 entry restano VA nudi
(0x40E184 … 0x40E356) e ora si risolvono con la mappa VA→entry, perché quei blocchi sono liftati.
Nessun entry point è stato inventato dentro un'istruzione: sono tutti start di blocco che IDA
riconosce (`jmp ds:jpt_40E17D[eax*4]`, casi 0-7).

## 4. Muro successivo: `CreateThread` con corpo guest (misurato, non ipotizzato)

Con il fix il gioco supera `__output` e arriva a storm che carica l'MPQ. Sequenza dal trace
(`SC_BRIDGE_TRACE=1`, `build/qa/run-switch-trace-tail.log`):

```
CreateEventA(0,0,0) = 0x5e000002
CreateThread(0, 0, fn=0x188A0404, arg=0)
[arm-cpu] SIGSEGV  fault pc 0x188A0408  fault address 0x10
```

`0x188A0404` = `loc_15016C10` (storm). Codice ARM: `188a0404 push {r4,r5,r11,lr};
188a0408 ldr r4,[r0,#0x10]`. Lo shim del progetto (`win32_shim.c`, `th_tramp`) chiama
`fn(arg)` con l'**ABI C**, ma un ingresso tradotto ha ABI `fastcc void(%_cpu*)`: r0 viene
letto come `_cpu*` → deref di 0x10. Non è un VA sbagliato: è il **confine ABI** fra codice
tradotto e shim.

Tentativo valutato e **scartato con misura**: eseguire il corpo del thread in modo sincrono dentro
`CreateThread` (sullo stack emulato, ABI guest). Non è un'alternativa: il corpo di storm aspetta un
evento che il thread *chiamante* segnala dopo la CreateThread. Misurato: attesa in loop
`WaitForSingleObject(0x5e000002, 250 ms)` -> `WAIT_TIMEOUT` all'infinito (smoke EXIT=124).
Artefatto del tentativo: `build/starcraft-armv6-thread` (non candidato).

Stato attuale: **stop nominato** (`build/qa/run-switch2.log`, EXIT=72), nessun crash:
`imp_CreateThread` in `runtime/arm/bridge_arm.c` dichiara cosa manca invece di lasciare che lo shim
chiami un ingresso tradotto con l'ABI sbagliata.

## 5. Cosa serve davvero per i thread (analisi per chi riprende)

Il port è single-threaded **per contratto** (`runtime/arm/indirect_dispatch.h`): due stati globali
impediscono due thread tradotti in parallelo.

1. `cpu_llasm_current()` -> `g_cpu_for_signal` (globale): gli shim leggono gli argomenti dal `_cpu`
   sbagliato. Serve una variabile `__thread`.
2. `reasm_indirect_target` (globale, `runtime/arm/indirect_dispatch.c`) è scritto dall'IR *subito
   prima* della `musttail call @reasm_indirect`: con due thread il target di A può essere
   sovrascritto da B fra lo `store` e la `ldr` dentro `reasm_indirect.S`. **Questo è il punto
   delicato**: il valore deve diventare stato per-thread. Via pulita e in perimetro: un campo in
   `_cpu` (es. indice 13, dopo `_st_result`) scritto dall'IR — `tools/dispatch-ir.py` già riscrive
   ogni `tcall reg`, quindi sa emettere `getelementptr %_cpu, %cpu, 0, 13; store` al posto di
   `store ... @reasm_indirect_target`, e `reasm_indirect.S` legge `[r0,#<offset>]`. In alternativa
   `__thread` sul globale, ma l'IR lo indirizza in modo assoluto (non TLS), quindi non basta.
3. `g_jb[MAX_DEPTH]`/`g_depth` in `guest_call_arm.c` -> `__thread` (un `jmp_buf` per thread).
4. `cpu_llasm_init` mappa lo stack emulato a un VA **fisso** (`ARM_STACK_BASE`): serve una variante
   con base per thread (es. `ARM_STACK_BASE + k*stride`, dentro `ARM_STACK_MAX`).
5. Allocatore dell'arena (`arm_alloc_low`) e `arm_teb_*`: il TEB emulato è unico (fs: fisso) →
   `LastError` condiviso fra thread. Da valutare: TEB per thread o accettarlo e documentarlo.
6. Gli handle dei thread creati dal port devono restare riconoscibili da `WaitForSingleObject`
   (`imp_WaitForSingleObject` oggi delega allo shim `WaitForSingleObject__c` per eventi/UI).

Ordine consigliato: (2) per primo, con un test che lancia due thread che fanno `ret` in loop e
verifica che il target non si mescoli; poi (1)+(3)+(4); infine collegare `CreateThread`.

> **Aggiornamento (stessa sessione, §9):** i punti 1-4 e 6 sono implementati e il gioco arriva alla
> finestra. Gli offset reali dei due campi in `_cpu` sono **136** (`_ind_target`) e **140**
> (`_ind_esp`), non 144/148: su ARM i puntatori sono 4 byte, quindi la coda dello struct è più
> corta. Gli offset sono vincolati da `_Static_assert` in `runtime/arm/cpu_llasm.h`.

## 6. Gate e candidato (build canonica, 17:15-17:20)

Ricetta (una sola catena; il lift ora è necessario perché i jump table non erano coperti):

```bash
cd /home/enne2/dev/starcraft-armv6
./tools/lift-full.sh srw exe        # 2,1 s   (legge code_entry_points.sci esteso)
./tools/lift-full.sh ir  exe        # 4,2 s
./tools/build-entry-obj.sh          # 3m22    -> EXIT=0 (log build/qa/build-entry-obj-switch2.log)
ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
  ./tools/link-arm.sh build/starcraft-armv6-switch2      # link-errors.txt vuoto
python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-switch2
```

| gate | esito |
|---|---|
| `./tools/build-entry-obj.sh` | EXIT=0, StarCraft.entry.o 19.900.344 byte, `324 stub (HAND 3, SHIM 289, UNK 32)` |
| link | `creato: build/starcraft-armv6-switch2`, `link-errors.txt` vuoto |
| dispatch | **96756 voci** (era 92935; +3821), primo VA 00401000, ultimo 15044C73, ordinate/univoche, **0 collisioni** |
| `make check` | **EXIT=0** (`build/qa/make-check-switch2.log`) |
| smoke arm1176 + asset sandbox | **EXIT=72**, stop nominato `CreateThread` (`build/qa/run-switch2.log`) |

Candidato per QA (**non sovrascrive** `build/starcraft-armv6-sse2crt` né `-storm`):

```
sha256 fd3a250a5ae1c678c37c894b0ccb04ae361159e30eb0f4bb08e2084f8f532559  build/starcraft-armv6-switch2
        21.229.xxx byte, hash in build/qa/SHA256-switch2.txt, mappa in build/qa/dispatch-switch2.txt
```

Intermedi della sessione (per bisect): `build/starcraft-armv6-esp` (diagnostica arco/ESP),
`-switch` (`f824f1e3…`: jump table liftati, ancora SIGSEGV su CreateThread, primo muro visibile),
`-thread` (tentativo sincrono, deadlock), `-switch2` (candidato).
Fallback conservati: `build/qa/bak-ir-1629/` (IR e `.llinc` *precedenti* al rilift: 84 MB),
`build/lift-full/entry/*.prev|*.bak`, la versione precedente di `code_entry_points.sci`.

File toccati (sorgenti del port): `runtime/arm/indirect_dispatch.c/.S` (anello diagnostico con ESP e
`[esp]`/`[esp-4]` + `reasm_indirect_esp`), `runtime/arm/guest_call_arm.c` (estrazione di
`guest_call_at`, nessun cambio di semantica), `runtime/arm/bridge_arm.c` (`imp_CreateThread` =
stop nominato, `imp_WaitForSingleObject` delega allo shim), `runtime/arm/bridge_table.def`
(2 ENTRY), `runtime/arm/gen_bridge_stubs.py` (`HAND_WINS_OVER_SHIM` con motivazione), questo report.
Generati: `build/lift-full/code_entry_points.sci` (+1256 voci), `StarCraft.ll`, `run-exe/*.llinc`,
`build/lift-full/entry/*`. `CHECKPOINT.md` intatto, nessun DB IDA salvato o rinominato.

## 7. Handoff — prossimo passo, in ordine

1. **Thread guest** (§5): il punto 2 (`reasm_indirect_target` per-thread via campo `_cpu`) è il
   prerequisito; senza, qualunque threading reale è una corsa silenziosa. Test focalizzato prima
   del gioco: due thread, `ret` in loop, nessun target incrociato.
2. Poi (1)+(3)+(4) di §5 e il collegamento di `CreateThread` (la machinery di chiamata guest esiste:
   `guest_call_at` in `guest_call_arm.c`, con l'argomento pushiato e la sentinella).
3. Dopo i thread ci si aspetta: `SFileOpenArchive`/MPQ sugli asset della sandbox, GDI/DirectDraw,
   creazione finestra, primo menu. I 32 UNK residui sono la worklist: ogni UNK è uno stop *con nome*.
4. Ramo QA: nessuna modifica; VM non aperta; niente input desktop simulato.

## 8. Decisioni autonome

1. **Rilift dai sorgenti invece di un rattoppo sulla dispatch**: il VA 0x407828 non è un VA da
   mappare, è un *indirizzo del blob dati* che il lift usa come puntatore a codice. Mapparlo
   avrebbe nascosto il difetto; i blocchi case non erano nemmeno tradotti.
2. **Nessun entry point dentro un'istruzione**: le 1256 voci aggiunte sono start di blocco
   riconosciuti da IDA (`get_switch_info`), non indirizzi scelti per far passare il test.
3. **Thread: stop nominato invece di corsa o deadlock.** Il tentativo sincrono è stato misurato e
   scartato (deadlock con l'evento di storm); lo shim che chiama un ingresso tradotto con l'ABI C è
   stato sostituito da un rifiuto dichiarato. Nessun finto successo.
4. **`HAND` del port prevale sullo shim solo per CreateThread/WaitForSingleObject**, con lista
   esplicita nel generatore e motivazione nel commento: per tutti gli altri il criterio resta
   "implementazione del progetto = shim".
5. Diagnostica (anello con ESP) mantenuta nel percorso di STOP: nessun costo nel percorso normale
   (3 store per arco indiretto), ed è ciò che ha permesso di distinguere `ret` da `jmp [mem]`.

## 9. Thread guest: risolto (thread host + `_cpu` per thread), e muro successivo

Il punto §5.2 (`reasm_indirect_target` globale) è stato risolto come indicato: il target dell'arco
indiretto è ora **`_cpu._ind_target`** (offset 136), scritto dall'IR che `tools/dispatch-ir.py`
riscrive e letto da `runtime/arm/indirect_dispatch.S`. Stato per-thread completo:

| stato | prima (globale) | ora |
|---|---|---|
| target dell'arco indiretto | `reasm_indirect_target` | `_cpu._ind_target` @136 |
| ESP dell'arco (diagnostica) | `reasm_indirect_esp` | `_cpu._ind_esp` @140 |
| `_cpu` corrente | `g_cpu_for_signal` | `__thread g_cpu_current` (+ fallback per il signal handler) |
| `jmp_buf`/profondità chiamata guest | globali | `__thread` |
| stack emulato | VA fisso `ARM_STACK_BASE` | `cpu_llasm_init_at(cpu, size, base)`: un intervallo per thread |
| `g_in_guest`/`g_stop` | globali | `__thread` |

Il tipo `%_cpu` del `.ll` è allungato da `dispatch-ir.py` (`{ i32x12, [8 x double], i64, i64, i8*, i8*,
i32, i32 }`) e `runtime/arm/cpu_llasm.h` verifica gli offset con `_Static_assert` (136/140) — se le
due definizioni divergono il build **non compila**. `CreateThread`/`WaitForSingleObject` sono
`HAND` del port (lo shim chiamerebbe il puntatore con l'ABI C): il primo crea un thread host con
`_cpu`+stack emulato propri (`arm_guest_thread_start` in `guest_call_arm.c`), il secondo fa
`pthread_join` sui nostri handle e delega allo shim (`WaitForSingleObject__c`) per eventi e finestre.
Serve `__aeabi_read_tp` (la glibc del sysroot non lo esporta più): `runtime/arm/tls_arm.S`,
`mrc p15,0,r0,c13,c0,3` (ARM1176 = ARMv6K).

Esito (log `build/qa/run-mt.log`), **prima volta che il gioco arriva alla finestra**:

```
[risorse] caricato come FILE (sola lettura risorse): …/local.dll -> handle 0x71000001
[dlg] template=0x6A text="<unreadable LPARAM>"
[host] init: video driver = x11
[host] finestra creata
[arm-cpu] SIGBUS  fault pc 0x18112B1C  fault address 0x00010011
[arm-cpu] guest eip 0x71000001  esp 0x021FF928
```

### 9.1 Muro attuale: le patch dei callback guest NON sono negli shim precompilati

`llvm-addr2line 0x18112B1C` -> **`dlg_elf_wndproc`** (`build/runtime-arm/shim-port/tools/elf/dlg_elf.c:181`),
che finisce con `ret=p?p(h,msg,wp,lp):0;` e salta a `loc_420980` con `r0 = hwnd/handle`, mentre il
proc tradotto si aspetta `%_cpu*` in r0 (`ldrd r2,r3,[r0,#16]` -> fault 0x10011).

`runtime/arm/patches/0002-guest-callbacks.patch` **contiene** la correzione (chiamare
`arm_guest_call4(...)` sotto `#if defined(__arm__)`), ma gli oggetti precompilati non la contengono:

```
$ grep -c arm_guest_call4 build/runtime-arm/shim-port/tools/elf/win32_ui.c   -> 0
$ llvm-nm build/runtime-arm/shim-port/obj/win32_ui.o | grep -c arm_guest_call4  -> 0
$ llvm-nm build/runtime-arm/shim-port/obj/dlg_elf.o  | grep -c arm_guest_call4  -> 0
```

Cioè: gli `obj/*.o` degli shim vengono da un build in cui lo stage 8.4
(`tools/build-runtime-arm.sh`, `patch -p1 < patches/0002-…`) **non ha applicato** la 0002. Non è
ricompilabile qui: manca `reasm_elf.h` (sta in `$TOOLKIT/runtime/elf` di sc-reasm, fuori perimetro).
Prossimo passo per chi ha accesso a sc-reasm: rifare lo stage 8.4 (o solo `dlg_elf.c`+`win32_ui.c` con
le patch applicate) e rilanciare `tools/link-arm.sh`; in alternativa dichiarare `GetWindowLongA`
(`DWL_DLGPROC`) come `HAND` del port con trampolino C->guest, ma è un giro più fragile.

> **RISOLTO senza toccare gli shim** (stessa sessione, §11): la via interna c'era ed è più pulita
> della patch. `DialogBoxParamA` è un `HAND` del port: il proc ci arriva *prima* dello shim, e allo
> shim passiamo un trampolino C-ABI che lo invoca con `arm_guest_call4`. Il dialogo ora si apre,
> riceve `WM_INITDIALOG`, disegna e ritorna regolarmente.

## 10. Stato finale della sessione (gate e candidati)

| candidato | sha256 | smoke (asset sandbox, arm1176) |
|---|---|---|
| **`build/starcraft-armv6-mt`** (da consegnare a QA) | `377e32c678397a28a06814b3e556088b7e7a271c1f4f6986499be2aa11270f8d` (20.844.204 byte) | **finestra X11 creata**, poi SIGBUS in `dlg_elf_wndproc` §9.1 (EXIT=9) |
| `build/starcraft-armv6-switch2` (conservativo) | `fd3a250a5ae1c678c37c894b0ccb04ae361159e30eb0f4bb08e2084f8f532559` | stop nominato `CreateThread` (EXIT=72) |
| `build/starcraft-armv6-switch` (intermedio) | `f824f1e371a5d5365ebc9116790119185dd69548f440cbc157c1d7f38d6e1e67` | SIGSEGV su CreateThread (EXIT=9) |

Gate sull'ultima build canonica (`build/qa/build-entry-obj-final.log`, `make-check-mt.log`,
`dispatch-mt.txt`, `run-mt.log`): `build-entry-obj.sh` EXIT=0 (2m49), link senza errori,
**96756 voci di dispatch, 0 collisioni**, `make check` EXIT=0, smoke = finestra creata.
Nessun processo lasciato attivo (i `qemu-system-arm` visibili sono la VM del ramo QA, non aperta qui).
Log del candidato: `build/qa/run-mt.log`, `build/qa/dispatch-mt.txt`, `build/qa/make-check-mt.log`,
`build/qa/build-entry-obj-final.log`, hash in `build/qa/SHA256-mt.txt`.

Il crash handler ora **autoidentifica** la procedura: oltre al pc host stampa `procedura tradotta:
entry VA 0x00420980  offset nel proc 0x00000000` (scansione lineare della tabella della dispatch sul
percorso di crash). Senza, il muro §9.1 richiedeva `llvm-addr2line`.
Nota di layout: allungando `_cpu` (offset 136/140) il TEB emulato si è spostato da 0x0E000088 a
0x0E000090 — è un indirizzo dell'arena, non un contratto.

File toccati in questa seconda parte: `runtime/arm/cpu_llasm.h/.c` (struttura `_cpu` + TLS +
`init_at`), `runtime/arm/indirect_dispatch.h/.c/.S` (target per-thread), `tools/dispatch-ir.py`
(campo `_ind_target` + tipo `%_cpu` allungato), `runtime/arm/guest_call_arm.c` (thread guest),
`runtime/arm/bridge_arm.c` (CreateThread/WaitForSingleObject), `runtime/arm/tls_arm.S` (nuovo),
`tools/build-entry-obj.sh` (compila `tls_arm.S`), `runtime/arm/patches/` invariato.

### 10.1 Decisioni autonome (seconda parte)

1. **Target dell'arco indiretto in `_cpu`, non `__thread` sul globale**: l'IR indirizza i simboli in
   modo assoluto, quindi una variabile TLS non sarebbe raggiungibile dal codice tradotto. Il campo
   in `_cpu` è per-thread *e* indirizzabile dall'IR; gli offset sono verificati da `_Static_assert`.
2. **`HAND_WINS_OVER_SHIM` limitato a CreateThread/WaitForSingleObject**, con la motivazione nel
   generatore: per gli altri resta valida la regola "lo shim è l'implementazione del progetto".
3. **`__aeabi_read_tp` fornito dal port** (`tls_arm.S`): la glibc del sysroot non lo esporta più;
   l'istruzione è quella canonica glibc per ARM, su ARM1176 (v6K) esiste.
4. **Nessuna patch binaria agli shim precompilati**: il muro §9.1 si chiude rigenerandoli dalle
   patch del progetto; un rattoppo sull'oggetto non sarebbe riproducibile.

## 11. §9.1 risolto sul confine giusto: `DialogBoxParamA` è un `HAND` con trampolino C→guest

La via «rifare gli shim» non serviva. Il difetto non è nel *valore* del puntatore, è che il
DialogProc tradotto ha ABI `fastcc void(%_cpu*)` e lo shim lo chiama con l'ABI C. Il port possiede
il confine in cui quel puntatore entra: `DialogBoxParamA` (import del gioco, 5 argomenti, era
`SHIM`). Ora è un `HAND` (come `CreateThread`):

* `runtime/arm/bridge_table.def`: `ENTRY(DialogBoxParamA, 5, imp_DialogBoxParamA)`.
* `runtime/arm/gen_bridge_stubs.py`: `DialogBoxParamA` in `HAND_WINS_OVER_SHIM` (motivo scritto lì).
* `runtime/arm/bridge_arm.c`: `imp_DialogBoxParamA` prende il proc del guest (VA guest **o**
  ingresso host: `arm_guest_call4` gestisce entrambi) e allo shim passa uno di 8 trampolini
  C-ABI statici; ogni trampolino ricorda il proprio proc e lo invoca con
  `arm_guest_call4(proc, h, msg, wp, lp)` — che salva/ripristina l'`esp` e usa un `jmp_buf` per
  livello, quindi le chiamate annidate (un DialogProc che manda messaggi) restano corrette.
  Slot LIFO perché i dialoghi sono modali: un dialogo annidato prende lo slot successivo.
  «Nessuno slot libero» = si passa allo shim senza trampolino e lo si **dichiara** su stderr.

Nessuna patch binaria agli oggetti precompilati, nessuna ricompilazione degli shim: il giro di
`--runtime-only` costa 1,4 s (`work/link/REPORT-flash-speed.md`).

Esito misurato (`build/qa/run-last.log`, candidato `build/starcraft-armv6-dlg`,
sha256 `99a5f498c09ac427bb673d209fb17ac17b7875afa6b96428b47f85165fefa82b`):

```
[dlg] template=0x6A text="<unreadable LPARAM>"
[host] init: video driver = x11
[host] finestra creata
[dlg] control=0x1 / 0xFFFF / 0xFFFF / 0x3E8
[dlg] WM_INITDIALOG delivered; entering modal loop
[host] present: percorso GPU (renderer + texture)
[dlg] EndDialog hwnd=0x10001 result=2
[dlg] destroyed; DialogBoxParamA returns 2
```

Il SIGBUS è sparito: il DialogProc **gira davvero** (WM_INITDIALOG, creazione dei controlli,
frame presentato per la GPU), il dialogo si chiude con `result=2` e il valore torna al guest.

### 11.1 Muro nuovo: `call [tabella appaddex]` verso `0x00427714` (dentro un'istruzione)

Subito dopo il ritorno del dialogo il guest entra nel *runner dei distruttori* `sub_4207B0`
(`Starcraft\SWAR\lang\gds\appexi.cpp`) e va in STOP:

```
target    = 0x00427714        eip chiamante = 0x004207F1   esp = 0x021FF928
```

Fatti IDA (DB `StarCraft.orig.exe.i64`):

* `sub_4207B0` (`0x4207b0-0x420828`) è un `atexit`-like: `v1 = dword_6D5DD8` (tabella di **32
  dword**, `sub_4F6100` la alloca con `storm_401(128,…)`, file `appaddex.cpp`), indice
  `word_519F64` (init `0x1F` = 31, quindi 32 slot) che scende a 0; `v3 = *(v1 + 4*v2)`, e
  `v3(a1)` è una **call indiretta con argomento in ecx** (IDA la decompila `__thiscall`).
* `0x00427714` **non è un ingresso**: cade dentro `call sub_488900` a `0x427712` (5 byte), su un
  `cdq`; non è un target di switch (`ida_nalt.get_switch_info` non lo riporta) e il dword
  `0x00427714` **non esiste nell'immagine** (ricerca byte esaustiva: 0 occorrenze).
* quindi il valore non è «un VA non liftato» (come `0x407828` §2), ma un **valore registrato nella
  tabella** che non è un puntatore a funzione utilizable: o un registrante ha scritto un indirizzo
  di *dato*, o il valore è il risultato di un calcolo che nel port diverge (es. `ptrtoint` di un
  alias `@loc_…`, che nel lift punta al blob dati e non al codice — la stessa classe di §2).

Diagnosi già pronta nel log (anello degli ultimi archi indiretti, §4): il chiamante
`0x004207F1` è *dentro* `sub_4207B0` (a metà del `for`, subito dopo `v3(a1)`), coerente con la
lettura della tabella al giro precedente.

Prossimo passo (per chi continua): strumentare la registrazione (`sub_4F6100`, 12+ chiamanti) per
sapere **quale** slot e **quale** sito ha scritto il valore, invece di ipotizzare; nel dubbio la
tabella va letta a runtime (`dword_6D5DD8` + indice) nel momento dello STOP.

### 11.2 Nota: dopo il dialogo lo smoke non esce, va in timeout

Con l'attuale candidato il processo non termina: lo STOP di §11.1 stampa tutto e poi il processo
resta vivo (misurato: 15 s con 20 s di CPU, thread in `nanosleep`/`futex`: main in attesa timer,
i worker di llvmpipe, i thread guest). `reasm_indirect_unresolved` finisce con `_exit(71)`, quindi
il `_exit` **non arriva a termine**: è il primo caso in cui lo stop avviene con thread guest vivi e
GPU/Mesa attiva. Effetto pratico: lo smoke di `--runtime-only` costa il timeout (`--smoke-timeout`,
default 25 s) invece di uscire subito. Il log è comunque completo e leggibile: per iterare conviene
`--smoke-timeout 12` oppure `--no-smoke`.
