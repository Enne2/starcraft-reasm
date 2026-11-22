# REPORT — flash-crt (port ARMv6): disgiunzione dei VA, entry point CRT, avanzamento al menu

Avanzamento incrementale. Data: 2026-11-20, dalle 15:30 Europe/Rome.
Sessione: questa (proprietario runtime unico), prosecuzione di `work/link/TASK-flash-runtime.md`
e dell'handoff §7-8 di `work/link/REPORT-flash-runtime.md` (**non modificato**: resta integro).
Perimetro: `runtime/arm`, `tools/` di build del port, artefatti `build/`, questo report.
Vincoli rispettati: nessun sotto-agente, niente git/rete/guest, nessuna toolchain o pacchetto
installato sull'host, nessun accesso ai DB IDA di altri lavori, nessun dato proprietario.
`CHECKPOINT.md` intatto (spetta a QA).

**Stato in una riga (inizio sessione):** il link `--image-base=0x18000000` riesce con gli oggetti
già compilati, i quattro `PT_LOAD` sono **fuori** da tutti gli spazi guest e `--dump-map` riporta
**0 collisioni** (era 841): Gate 1 superato sul fronte indirizzi, smoke in corso.

---

## 0. Punto di partenza verificato (artefatti reali, non dichiarazioni)

```
$ ls -l build/lift-full/entry/{StarCraft.entry.o,storm.entry.o}   → 15:27, presenti
$ ls -l build/starcraft-armv6-dispatch                            → 19.822.948 byte, 15:29
$ sha256sum build/starcraft-armv6-dispatch
c2604f22d431ad76b8b27f5986053489a76c7c16f2cf3de20cc1192d00a2df38   (candidato del predecessore)
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch
voci: 92839   primo VA 00401000   ultimo VA 15044C73
host entry il cui indirizzo coincide con un VA guest: 841     → EXIT=3
```

Confermato quindi il residuo lasciato aperto dall'handoff §7.1: la tabella è ordinata e univoca,
ma `.text` del port cade dentro l'immagine x86 e 841 entry host coincidono numericamente con VA
guest.

**Mappa degli spazi di indirizzi (misurata, non dedotta):**

| utilizzatore | intervallo |
|---|---|
| immagine guest StarCraft | `0x00400000`–`0x006EBC28` |
| immagine guest storm | `0x15000000`–`0x1506FFFF` |
| stack guest | `0x02000000` + 4 MB (report `work/runtime-arm`) |
| arena `_cpu` / memoria emulata | `0x0E000000` + 16 MB |
| `VirtualAlloc` (proposte del port) | da `0x20000000` |
| **port attuale (senza base)** | `0x00010000`–`0x00EC9B4C` ← collide con stack guest e immagine |

## 1. Gate 1 — disgiunzione degli spazi a livello di link (fatto)

Fix minimo a `tools/link-arm.sh`: una riga di supporto `LDFLAGS_EXTRA` (array `read -r -a`, nessuna
altra modifica), così il link accetta parametri extra di `ld.lld` **senza rifare opt/llc**:

```bash
read -r -a EXTRA <<< "${LDFLAGS_EXTRA:-}"
"$LLD" ... ${EXTRA[@]+"${EXTRA[@]}"} -L"$LIB" ...
```

`--image-base` è accettato da `ld.lld 23.1.1` (verificato in `--help`), quindi il suggerimento
`-Ttext-segment` dell'handoff non serve. Relink **dagli stessi oggetti** (nessun rilancio di
`opt`/`llc`), 0,8 s:

```
$ ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
    LDFLAGS_EXTRA='--image-base=0x18000000' ./tools/link-arm.sh build/starcraft-armv6-base
creato: build/starcraft-armv6-base           (real 0m0,804s)
sha256: c12842230681828f5c39fb29c3f03e99deb92d80e6d17d37dcc469bd32059bb9
$ cat build/link-errors.txt                  → vuoto
```

`readelf -lW` (estratto):

```
Entry point 0x18126298
LOAD 0x000000 0x18000000 0x18000000 0x116298 0x116298 R   0x10000
LOAD 0x116298 0x18126298 0x18126298 0x9fadf8 0x9fadf8 R E 0x10000
LOAD 0xb11090 0x18b31090 0x18b31090 0x000c8  0x00f70 RW  0x10000
LOAD 0xb11158 0x18b41158 0x18b41158 0x1d9a20 0x3789f4 RW  0x10000
```

Tutti e quattro i segmenti stanno in `0x18000000`–`0x18EB9B4C`: **disgiunti** da immagine guest
(`0x00400000+`), storm (`0x15000000+`), stack guest (`0x02000000+`), arena (`0x0E000000+`) e dal
punto in cui `VirtualAlloc` inizia a proporre (`0x20000000`). Controllo anche per sezione
(`readelf -SW`): `.text` a `0x18126298`, nessuna sezione allocata sotto `0x18000000`.

Invariante post-link:

```
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-base
voci: 92839   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0        → EXIT=0
```

**841 → 0.** Il candidato con base alta è idoneo sul fronte indirizzi; resta da confermare che
l'eseguibile parta davvero a `0x18000000` sotto `qemu-arm-static` (PIE/loader a parte: è un
`EXEC` con entry `0x18126298`, quindi l'indirizzo è assoluto).

### 1.1 Lo smoke del base alto rivela il difetto successivo (dati guest non ai VA guest)

```
$ ulimit -c 0; timeout 60 qemu-arm-static -cpu arm1176 -L build/sysroot ./build/starcraft-armv6-base
[arm-game] entry: start() del CRT a 0x00404C21
[arm-cpu] SIGSEGV
[arm-cpu] fault pc 0x181542FC  lr 0x18AE8AD4
[arm-cpu] fault address 0x0050C650
[arm-cpu] guest eip 0x18AE78D0  esp 0x021FFE98  eax 0x0050C650  edx 0x0000000A      → exit 9
```

`0x181542FC` è `loc_405ED0+0x1C` (simbolo dal binario), cioè codice tradotto che scrive su un
**VA guest assoluto**: la IR di `loc_405ED0` contiene `%4 = add i32 %3, 5293648` (= `0x0050C650`)
e poi `store i32 %7, i32* inttoptr(i32 %4)`. Il codice liftato non indirizza i dati tramite
simboli ma con **costanti assolute** (e con gli alias `@loc_<VA>`, 5757 alias, 28981 usi nel solo
modulo exe).

Nel candidato precedente lo scrivere in `0x0050C650` "funzionava" per caso: `.text` del port
stava a `0x00136298`, e `0x0050C650` cadeva **dentro** il nostro `.text` (leggibile) → nessun
fault, ma dati sbagliati. Con la base alta la stessa scrittura è onesta: pagina non mappata.

Causa: `tools/link-arm.sh` linka `StarCraft.entry.o`/`storm.entry.o` e basta. I tre blob dati del
lift (`@_data`, `@_rdata`, `@_text`, che `llasm` emette come `%__data/%__rdata/%__text` con dentro
i **byte originali del PE**) finiscono nel `.data`/`.rodata` del port, a VA del port. La pipeline
storica li collocava ai VA del PE con `guest_layout.ld` (INSERT) — ma quello script collocava
anche il `.text` delle immagini a `0x08000000`/`0x10000000`, cosa che qui non serve e che
colliderebbe con l'arena `0x0E000000`.

**Correzione alla fonte (minima, riproducibile).** Tre pezzi:

1. `tools/build-entry-obj.sh`: `llc ... --data-sections` per entrambe le immagini, così i tre blob
   restano sezioni distinte (`--data-sections` = stesse sezioni già previste dal layout storico).
   Verificato sulle dimensioni attese dal layout: `.data..L_data` `0x1D1694`, `.rodata..L_rdata`
   `0xB8BC`, `.rodata..L_text` `0x2F608` (exe) — identiche a `guest_layout.ld`.
2. `$ENTRY_DIR/guest_data.ld` generato da `build-entry-obj.sh`: **solo** i segmenti dati ai VA
   originali, più i simboli `reasm_text_lo/hi`:

```
SECTIONS {
  reasm_text_lo = ADDR(.text);
  reasm_text_hi = ADDR(.text) + SIZEOF(.text);
  .guest_exe_tdata   0x00406C0C : { …/StarCraft.entry.o(.rodata..L_text) }
  .guest_exe_rdata   0x004FE5D0 : { …/StarCraft.entry.o(.rodata..L_rdata) }
  .guest_exe_data    0x0050C000 : { …/StarCraft.entry.o(.data..L_data) }
  .guest_storm_tdata 0x150062D8 : { …/storm.entry.o(.rodata..L_text) }
  .guest_storm_rdata 0x150453C4 : { …/storm.entry.o(.rodata..L_rdata) }
  .guest_storm_data  0x15059000 : { …/storm.entry.o(.data..L_data) }
} INSERT AFTER .bss;
```

   Le 486 relocazioni `R_ARM_ABS32` dentro i dati restano risolte dal linker: quelle verso
   funzioni `loc_*` diventano indirizzi host (dentro `.text` del port) e la dispatch le accetta
   come indirizzi propri (§3); quelle verso i blob dati diventano VA guest, perché i blob *sono* ai
   VA guest.

3. `tools/link-arm.sh`: se `ENTRY_DIR/guest_data.ld` esiste, aggiunge
   `--section-start=.text=0x18000000 -T guest_data.ld` (variabile `IMG_BASE`). `--image-base` è
   stato **scartato**: `ld.lld` rifiuta sezioni a indirizzo *minore* della base
   (`error: section '.guest_exe_tdata' address (0x406c0c) is smaller than image base`), e serve
   invece avere i dati proprio lì.

Layout risultante (`readelf -lW`, PT_LOAD allocati):

```
LOAD 0x000000 0x00010000 R     (header/ELF, 0x4F9E8C byte — nessun VA guest)
LOAD 0x4FC000 0x0050C000 RW    .guest_exe_data
LOAD 0x6D62D8 0x150062D8 R     tdata+rdata storm
LOAD 0x729000 0x15059000 RW    .guest_storm_data
LOAD 0x740000 0x18000000 R E   .text del port (0x9FA4A4 byte)
LOAD …        0x18A0A4A4 R     .rodata del port (tabelle della dispatch)
LOAD …        0x18ADEBD8 R E   .init/.fini/.plt
LOAD …        0x18AEF530 RW    .got/.dynamic
LOAD …        0x18AFF5F8 RW    .data/.bss del port (0x003B4 file / 0x19F38C mem)
```

Nessun segmento cade più in `0x00400000-0x006EBC28` (immagine), `0x02000000+` (stack guest),
`0x0E000000+` (arena). `--dump-map`: 0 collisioni (vedi §1).

### 1.2 Lo smoke con i dati ai VA originali: niente più SIGSEGV, stop *nominato*

```
$ ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
    ./tools/link-arm.sh build/starcraft-armv6-data          → 0,5 s, link-errors.txt vuoto
$ ulimit -c 0; timeout 60 qemu-arm-static -cpu arm1176 -L build/sysroot ./build/starcraft-armv6-data
[arm-game] entry: start() del CRT a 0x00404C21
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000      ← prima non si arrivava qui
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x004060C1     (1ª voce della tabella _initterm a 0x0050C4C0)
  in mappa  = 0x00000000
  eip chiamante = 0x00404E80     ← dentro il ciclo di _initterm del CRT
  ret (ARM) = 0x18021278
  ecx       = 0x004060C1
  codice ARM del port: 0x18000000 ... 0x189FA4A4
                                                                     → exit 71
```

Cioè: il CRT legge davvero la sua tabella ai VA guest (i dati ora sono lì), l'entry non è liftata e
la dispatch lo dice con nome e chiamante, invece di diramare alla cieca. **Questo è il punto di
partenza del Gate 2.**

### 1.3 Difetto trovato e corretto nel frame della dispatch (finestra del codice del port)

Prima correzione della finestra "indirizzo proprio del port": la versione del predecessore usava
`__executable_start`/`etext`. Con i dati guest mappati ai VA originali quei simboli **non
delimitano più** il nostro codice (misurato: `__executable_start=0x00010000`,
`etext=0x18ADF530`), quindi qualunque VA guest cadeva nella finestra e veniva **diramato
direttamente**. Sintomo esatto, misurato prima del fix:

```
[arm-cpu] fault pc 0x004060C0  lr 0x189C283C
[arm-cpu] fault address 0x004060C0        ← salto *diretto* a un VA guest
```

Il valore `0x004060C0` non era in tabella (non è una funzione liftata) e la finestra larga lo ha
accettato come "indirizzo nostro": esecuzione a un VA guest. Fix: la finestra viene dai simboli
**dello script di link** `reasm_text_lo = ADDR(.text)` / `reasm_text_hi = ADDR(.text)+SIZEOF(.text)`
= `0x18000000`–`0x189FA4A4`, cioè esattamente il `.text` del port. Dopo il fix lo stesso valore
produce lo STOP nominato di §1.2.

## 2. Gate 2 — entry point CRT non liftati: resi persistenti nella pipeline

### 2.1 Oracolo IDA (sessione propria sul progetto, DB `build/lift-full/StarCraft.orig.exe.i64`)

Tabella `_initterm` a `0x0050C4C0` (16 dword letti dal DB, non dall'IR):

```
0x0050C4C0: 0x00000000      0x0050C4C4: 0x004060C1
0x0050C4C8: 0x00406AA1      0x0050C4CC: 0x0040D8C6
0x0050C4D0: 0x004F8329      0x0050C4D4: 0x00000000
```

Verifica che siano **codice** (IDA, `decode_insn` + nomi):

```
0x004060C1  push 0x80 ; call … ; test … ; pop …        (label ___onexitinit, dentro il corpo
                                                        della funzione che parte a 0x00406041)
0x00406AA1  cmp … ; jnz … ; push … ; call …            (funzione ___initmbctable, già liftata)
0x0040D8C6  mov eax, ds:6DD300 ; test eax,eax ; …      (label ___initstdio, corpo da 0x40D840)
0x004F8329  push ebp ; mov ebp,esp ; sub esp,0x20 …    (funzione ___sse2_available_init)
```

Stato nella `code_entry_points.sci` (input SRW, 6167 voci ordinate): `loc_406AA1` **c'era già**;
mancavano `0x4060C1`, `0x40D8C6`, `0x4F8329`. Il `STOP` di §1.2 su `0x004060C1` era quindi
esattamente la prima voce della tabella, non un caso isolato.

### 2.2 Perché `___sse2_available_init` non si può liftare (e cosa si fa invece)

Decompilato per intero (IDA `0x4F8329`-`0x4F83E9`): legge EFLAGS e prova il bit ID, poi esegue
`cpuid` **due volte** (IDA `0x4F835A`, `0x4F836D`), e in base a `HIBYTE(EDX)` bit 2 (= bit 26 di
EDX, SSE2) e al vendor scrive `0x006DC2F0` / `0x006DC2EC`. Nessun caso `cpuid` esiste in
`vendor/sr/SRW/udis86_dep.c` (tabella `flags_needed`/`flags_modified` → `FL_UNKNOWN`) né in
`vendor/sr/llasm/llasm.d`; primo tentativo, misurato:

```
$ ./tools/lift-full.sh srw exe
Error: unknown flags - 0 - 1012589 (0x4f836d) - cpuid
SRW exe: exit=6
```

Non ho aggiunto `cpuid` a SRW+llasm (sarebbe una feature nuova in due componenti vendor). Ho usato
il meccanismo che la pipeline già prevede per le procedure non traducibili
(`external_procedures.sci`, stesso trattamento di `__CIpow`):

```
build/lift-full/external_procedures.sci:
loc_4F8329, __sse2_available_init
runtime/arm/crt_init_arm.c:            void __sse2_available_init(_cpu *cpu)
    *0x006DC2F0 = 0; *0x006DC2EC = 0; cpu->_eax = 0;
```

Semantica **provata**, non scelta per comodità: il ramo "CPUID non disponibile" dell'originale
lascia entrambi i flag a 0 e ritorna 0 (`0x4F8376`, `0x4F837D`, `0x4F83E9`); su ARM1176 CPUID non
esiste, e il flag `__sse2_available` serve al CRT MSVC per scegliere le varianti SSE2 del *proprio*
`memcpy`/`memset`, che il lift non sa comunque tradurre (`movaps`/`movdqa`): a 0 quelle varianti non
vengono mai selezionate. Il file cita indirizzo per indirizzo ogni riga della decompilazione.

L'entry `0x4F8329` entra comunque nella **tabella della dispatch** come voce esplicita
(`--map-extra 0x4F8329=__sse2_available_init`): la tabella VA→entry è l'unico posto in cui i salti
indiretti si risolvono, quindi un'implementazione C esterna deve esserci, altrimenti si torna a uno
STOP.

### 2.3 Modifiche persistenti (sorgenti della pipeline)

| file | modifica |
|---|---|
| `build/lift-full/code_entry_points.sci` | +`loc_4060C1`, +`loc_40D8C6` (ordinate; backup in `.sci.bak`) |
| `build/lift-full/external_procedures.sci` | +`loc_4F8329, __sse2_available_init` (con la motivazione in commento) |
| `runtime/arm/crt_init_arm.c` | **nuovo**: implementazione con citazioni IDA |
| `tools/build-entry-obj.sh` | `llc --data-sections`; genera `guest_data.ld`; `--map-extra 0x4F8329=__sse2_available_init`; compila `crt_init_arm.c` |
| `tools/build-runtime-arm.sh` | `crt_init_arm.c` nelle due liste di compilazione (coerenza della pipeline storica) |
| `tools/link-arm.sh` | `--section-start=.text=$IMG_BASE` + `-T guest_data.ld` quando `ENTRY_DIR` li fornisce |
| `runtime/arm/indirect_dispatch.c` | finestra del codice del port da `reasm_text_lo/hi` (§1.3) |

Rigenerazione verificata (nessun "riuso" del vecchio lift):

```
$ ./tools/lift-full.sh srw exe     → SRW exe: exit=0, StarCraft.llasm 384 righe + seg*.llinc (15,3 MB code)
$ ./tools/lift-full.sh ir  exe     → IR exe: StarCraft.ll 2.133.092 righe (72.323.310 byte)
$ grep -c '^define .*@loc_4060C1(' build/lift-full/StarCraft.ll   → 1
$ grep -c '^define .*@loc_40D8C6(' build/lift-full/StarCraft.ll   → 1
```

### 2.4 表 CRTs `0x0050C000`: 34 inizializzatori non liftati (batch, non uno alla volta)

Il primo `_cinit` completato ha portato al secondo STOP:

```
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x004FC150     eip chiamante = 0x00404EAE     (exit 71)
```

`0x00404E50` è `_cinit` (IDA): dopo la tabella a `0x0050C4C0` chiama `_atexit(0x405967)` e poi
**itera `0x0050C000`-`0x0050C4BC` chiamando ogni voce non nulla** (`(*v3)()`, IDA `0x004EAA`).
Quella tabella ha 303 dword, 302 non nulle: sono gli inizializzatori di modulo, ed era inutile
scoprirli uno STOP alla volta. Misura fatta sull'IR appena rigenerata: **268 già liftati, 34
mancanti**, tutti nel cluster `0x004FC150`-`0x004FD560` (subito dopo il blocco FP esternalizzato
`0x4F7B18`-`0x4FC148`).

I 34 VA sono quindi entrati in `build/lift-full/code_entry_points.sci` in un colpo solo (6203
voci), e la rigenerazione li definisce tutti:

```
$ ./tools/lift-full.sh srw exe && ./tools/lift-full.sh ir exe
IR exe: StarCraft.ll 2.134.758 righe
$ python3 - <<…confronto tabella IDA vs @loc_ definiti…
nuove entry: 34  definite: 34  ancora mancanti: 0
```

### 2.5 Un difetto vero trovato dallo smoke: la `ret` del guest dopo un'implementazione C

Con `__sse2_available_init` in tabella lo smoke è **peggiorato in modo informativo**: niente più
STOP, ma `start()` è tornato al main senza consumare il sentinella:

```
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
[arm-game] l'entry e' tornata senza consumare il sentinella (pila sbilanciata)   → exit 3
```

Causa (oracolo: il traduttore, non un'ipotesi). Una chiamata guest è `PUSH <ritorno>; tcall`, e
una `ret` è `POP tmp1; tcall tmp1` (`build/lift-full/run-exe/seg01_code.llinc`, righe `;call` e
`;ret`). La dispatch raggiunge l'entry con `bx` (diramazione, non chiamata): un corpo C che
finisce con `ret` torna al chiamante **host** della dispatch, lasciando sullo stack emulato
l'indirizzo di ritorno mai consumato. È lo stesso contratto che `arm_bridge_common_c` rispetta per
gli import (pop del ritorno + salto), e che mancava per le procedure esterne.

Fix minimo, generico, in `runtime/arm/indirect_dispatch.S`: trampolino `reasm_extern(r0=%_cpu*,
r1=fn)` che chiama la C e poi **emula la `ret` del guest** (pop del ritorno dallo stack emulato →
`reasm_indirect_resolve` → `bx`), senza lasciare frame host sull'arco guest. `crt_init_arm.c`
espone solo il corpo (`__sse2_available_init_body`); l'ingresso `__sse2_available_init` è il
trampolino, così la voce di tabella è indistinguibile da una procedura tradotta.

---

## 9. Handoff (sessione che chiude qui: 20/11, 15:52) — stato, resta, comandi esatti

### 9.1 Risultato corrente (build e smoke appena eseguiti, nessuna build orfana)

```
$ ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
    ./tools/link-arm.sh build/starcraft-armv6-dispatch
creato: build/starcraft-armv6-dispatch   (21.036.140 byte, 15:50, link-errors.txt vuoto)
sha256 ff0fa176ac44be2a83a47b061696fd7ce2d0366aa8fd33c408b0500ea012cfc9
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch
voci: 92934   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0          → exit 0
$ ulimit -c 0; timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot ./build/starcraft-armv6-dispatch
                                                                 → exit 9 (SIGSEGV)
```

Log del candidato per QA: **`build/qa/run-ff0fa176.log`** (+ `build/qa/SHA256-ff0fa176.txt`);
storico dei due tentativi precedenti in `build/qa/run-5a6f06ad.log`, `build/qa/run-500faa8d.log`.

Avanzamento misurato rispetto al predecessore (che si fermava a SIGILL sul primo `bx`):

```
[arm-game] entry: start() del CRT a 0x00404C21
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
[arm-cpu] SIGSEGV
[arm-cpu] fault pc 0x189FBB9C  lr 0x002DE000
[arm-cpu] fault address 0x006DE00C
```

Cioè: la tabella `_initterm` a `0x0050C4C0` **e** i 302 inizializzatori di modulo a `0x0050C000`
sono passati (nessun STOP della dispatch), il CRT è entrato in `_cinit` completo e il port ha
cominciato a leggere la sezione risorse. Il fault non è più né un salto sbagliato né uno stub
silenzioso: `pc 0x189FBB9C` = `res_entries+0xA8` (funzione C del runtime, walker delle risorse) che
esegue `ldrh r4, [r3, #12]` con `r3 = 0x006DE00C`, cioè **il primo byte dopo la sezione `.rsrc`
dell'immagine, che non è mappata**.

### 9.2 Cosa resta (in ordine, senza aprire altro)

1. **Mappare `.rsrc` (e in generale le sezioni PE non-dati) ai VA originali.** Oggi `guest_data.ld`
   colloca solo i tre segmenti dati del lift: `.guest_exe_data` finisce a `0x006DD694`, mentre
   `.rsrc` inizia a `0x006DE000`. Nel layout *vecchio* quei VA cadevano dentro il `.text` del port
   (leggibile) e il walker leggeva silenziosamente i nostri byte: era una maschera, non un
   funzionamento. Fix naturale, coerente con §1.1: estrarre dal PE le sezioni non coperte dai blob
   (`--out-rsrc` in `runtime/arm/gen_game_layout.py` o un piccolo estrattore che scriva un oggetto
   `-I binary` per sezione) e aggiungerle a `guest_data.ld` con i loro VA; poi `readelf` deve
   mostrare un LOAD che copre `.rsrc`, e `--dump-map` deve restare a 0 collisioni.
   *Nota*: `game_main_arm.c` mappa solo la prima pagina (header) di ogni immagine via
   `k_pe_*_hdr`; le pagine risorse non sono mai state fornite.
2. **Ripetere lo smoke** e, se compare un nuovo STOP della dispatch, aggiungere il VA a
   `code_entry_points.sci` (meccanismo di §2) oppure in `external_procedures.sci` + corpo C con
   `reasm_extern` (§2.2/§2.5) se l'istruzione non è traducibile.
3. **Avanzare verso il menu**: dopo `.rsrc` ci si aspetta `_WinMain@16` (`0x004E0AE0`, già nella
   tabella come `loc_4E0AE0`), MPQ, `CreateWindow`. I 33 UNK dichiarati (`arm_bridge_unk_count()`)
   sono la worklist: ognuno è uno stop *con nome* quando viene chiamato.
4. **Gate QA**: quando lo smoke arriva almeno a creare la finestra (SDL dummy non basta come prova
   GUI), copiare il candidato con hash in `build/` e passare il log a QA; `make check` va eseguito
   qui (§9.3, non ancora fatto in questa sessione).

### 9.3 Comandi esatti (rilink in 0,5 s; rifare opt/llc solo se cambia l'IR)

```bash
cd /home/enne2/dev/starcraft-armv6
# solo rilink (IR e oggetti già compilati)
ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
  ./tools/link-arm.sh build/starcraft-armv6-dispatch
python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch        # atteso exit 0
ulimit -c 0; timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot ./build/starcraft-armv6-dispatch

# se cambiano i sorgenti del runtime (C/asm) senza toccare l'IR: 3 righe, ~2 s
clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard \
  --sysroot=build/sysroot -O1 -g -I build/runtime-arm/game -I runtime/arm \
  -c -o build/lift-full/entry/indirect_dispatch.o runtime/arm/indirect_dispatch.c
#   (idem indirect_dispatch_asm.o da indirect_dispatch.S, crt_init_arm.o da crt_init_arm.c)

# se cambia la lista di entry point (build/lift-full/code_entry_points.sci): ~90 s totali
./tools/lift-full.sh srw exe && ./tools/lift-full.sh ir exe
# poi, se cambia l'IR: opt+llc (~4 min) e layout aggiornato
./tools/build-entry-obj.sh          # rigenera anche build/lift-full/entry/guest_data.ld

# regressioni del port
make check
```

### 9.4 File toccati in questa sessione

`tools/link-arm.sh` (LAYOUT/`IMG_BASE`, `LDFLAGS_EXTRA`), `tools/build-entry-obj.sh`
(`llc --data-sections`, generazione di `guest_data.ld`, `--map-extra 0x4F8329=…`, `crt_init_arm.c`),
`tools/build-runtime-arm.sh` (lista sorgenti), `runtime/arm/indirect_dispatch.{c,S,h}`
(finestra `reasm_text_lo/hi`, `reasm_extern`, ingresso `__sse2_available_init`),
`runtime/arm/crt_init_arm.c` (nuovo), `build/lift-full/code_entry_points.sci` (+35 VA, backup
`.sci.bak`), `build/lift-full/external_procedures.sci` (+`loc_4F8329`), `build/lift-full/entry/*`
(oggetti rigenerati; fallback `.prev` degli oggetti precedenti), `build/qa/*` (log + hash), questo
report. `CHECKPOINT.md` intatto; `work/link/REPORT-flash-runtime.md` intatto.

### 9.5 Sessione IDA

Usata (sola lettura, nessun salvataggio) `build/lift-full/StarCraft.orig.exe.i64` = il DB del
progetto; le altre sessioni IDA non sono state toccate. Evidenze IDA citate per indirizzo in §2.1,
§2.2 e §9.1 (`_cinit` = 0x004E50, tabella inizializzatori 0x0050C000, `___sse2_available_init` =
0x4F8329). `0x004FC150`…`0x004FD560`: 34 inizializzatori scoperti dalla tabella e liftati (§2.4).

### 9.6 Decisioni autonome

- `--image-base=0x18000000` **scartato** a favore di `--section-start=.text=0x18000000` + script
  `INSERT`: `ld.lld` rifiuta sezioni a indirizzo minore della base, e i dati guest devono stare ai
  VA originali (§1).
- `cpuid` **non** aggiunto a SRW/llasm; `___sse2_available_init` esternalizzata con semantica
  provata da IDA (§2.2). Se in futuro servisse un'altra procedura con `cpuid`, la strada pronta è
  la stessa, non nuove istruzioni nel traduttore.
- Dati guest presi dai blob del lift (byte del PE) invece di rigenerarli dai file: nessun accesso
  ad asset esterni, e i 486 puntatori a funzioni restano host-address come previsto dalla dispatch.
- Finestra "codice del port" ristretta a `.text` (§1.3) invece di `__executable_start`/`etext`:
  quei simboli ora coprono anche VA guest.

### 9.7 `make check` (eseguito a fine sessione)

```
$ timeout 240 make check          → EXIT=0
PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack
      bridge; ARMv6/VFPv2 attributes verified.
```

Nessun FAIL. Il check copre il ponte CPU/stack guest su ARM1176 e gli attributi ARMv6/VFPv2, quindi
le modifiche di questa sessione (link, dispatch, trampolino `reasm_extern`) non hanno rotto le
regressioni del port.
