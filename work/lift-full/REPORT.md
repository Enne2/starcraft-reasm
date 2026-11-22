# Lifting completo StarCraft.exe + storm.dll.orig → ARMv6 (`build/lift-full/`)

Sessione del 19 novembre 2026. Catena verificata: **PE32 x86 → SRW (OUT_LLASM) → `.llasm`
→ llasm → LLVM IR → `opt` → `llc` → oggetto ARMv6** (`arm1176jzf-s`, VFPv2, **senza NEON**).

Entrambe le immagini arrivano oggi all'oggetto con **zero errori** in SRW, in llasm e in
`opt`/`llc`. Oracolo: copie private dei binari e database IDA privati in
`build/lift-full/ida/` (`StarCraft.lift.exe`, `storm.lift.dll`); nessuna sessione IDA
concorrente è stata toccata e nessun originale è stato modificato.

## 1. Esito gate (misurato)

| Gate | StarCraft.exe | storm.dll.orig |
|---|---|---|
| SRW `OUT_LLASM` | exit 0, 0 errori | exit 0, 0 errori |
| import ordinali storm | **135/135** | — |
| istruzioni non tradotte (no-op) | 0 | 0 |
| procedure emesse (`.llasm` procs) | 71.112 (`loc_…`) | 22.107 (21.727 `loc_…` + 380 con nome di export) |
| istruzioni x86 tradotte | 272.946 | 87.969 |
| confini di flag risolti (propagate/require/target) | 114 / 95 / 1 | 31 / 27 / 1 |
| llasm → IR | OK | OK |
| `opt -passes=verify` + `default<O2>` | 0 errori, 0 warning fatali | idem |
| `llc` ARMv6 | 0 errori | 0 errori |
| `.o` | 13.501.416 B, `.text` 0x75C2D0 | 3.750.600 B, `.text` 0x2559C8 |
| attributi ARM | `Tag_CPU_arch: v6`, `Tag_FP_arch: VFPv2` | idem |
| copertura entry di funzione IDA | 5052/5115 = **98,8 %** | 1181/1234 = **95,7 %** (801 `loc_…` + 380 export) |

Le funzioni IDA non liftate dell'EXE sono concentrate nel blocco CRT FP dichiarato esterno
(§4). Per storm il conteggio per nome è sottostimato: 380 delle sue funzioni esportate
prendono il nome dell'ordinale (`ordinal_73746f726d2e646c6c_N`) invece di `loc_…`, quindi il
confronto per nome non le vede: 801 `loc_…` + 380 esportate = 1181 su 1234 (95,7 %); le
rimanenti sono nel blocco CRT. Le 22.488 definizioni IR di `storm.ll` sono 22.107 procedure
più le 381 funzioni `ccc` che llasm genera per le procedure `global`.

## 2. Riproduzione

```sh
cd ~/dev/starcraft-armv6
tools/lift-full.sh build                 # snapshot privato di SRW + llasm in build/lift-full/bin
tools/lift-full.sh srw  exe   && tools/lift-full.sh ir exe   && tools/lift-full.sh obj exe
tools/lift-full.sh srw  storm && tools/lift-full.sh ir storm && tools/lift-full.sh obj storm
# variante "strumento di progetto" (stessa IR, `opt` O2 puro):
tools/compile-arm.sh build/lift-full/StarCraft.ll build/pipeline/StarCraft-official.o
```

Artefatti in `build/lift-full/`:

```
StarCraft.orig.exe  storm.orig.dll            copie private (md5 uguali agli originali)
relocations.csv                               derivato di work/relocs (1 voce rimossa, §5)
*.sci                                         controllo dell'analisi (contenuto in §4)
StarCraft.llasm  storm.llasm                  LLASM (copie da run-*/)
run-exe/  run-storm/                          directory di lavoro SRW (i nomi segNN_*.llinc collidono)
run-exe/seg0*_*.llinc, run-storm/seg0*_*.llinc
StarCraft.ll  storm.ll                        IR (2.132.541 / 675.695 righe)
StarCraft.o   storm.o                         oggetti ARMv6
externals-StarCraft.txt  externals-storm.txt  simboli esterni attesi dal runtime
funcs-exe.txt  funcs-storm.txt                inizi di funzione esportati dai DB IDA privati
pipeline/<img>.{verified,optimized}.ll        IR verificata e ottimizzata
tbyte/test_tbyte                              test host delle conversioni binary80 (§3.4)
srw-*.log llasm-*.log                         log delle esecuzioni
```

Test delle conversioni `tbyte`, indipendente dal resto della catena:

```sh
gcc -O2 -no-pie -o build/lift-full/tbyte/test_tbyte \
    work/lift-full/tools/test_tbyte.c vendor/sr/SR/llasm-support/llasm_float.c -lm
build/lift-full/tbyte/test_tbyte      # PASS: tbyte helpers (0 failures)
```

Copre: `±0`, `1.0`, `3.5`, `1e±300`, `1e308`, `1e-308`, denormale `double`, `+Inf`, `NaN`,
i byte esatti di `1.0` e `+Inf` in formato esteso, e uno pseudo-denormale esteso.

## 3. Che cosa è stato verificato in IDA (VA)

### 3.1 Blocco CRT MSVC finale — EXE (`0x4F7B18`–`0x4FC148`)

Scansione di tutte le 5115 funzioni del DB privato contro i mnemoni implementati in
`SR_full_llasm_instr.c` + `udis86_dep.c`: **405 istruzioni non traducibili in 29 funzioni**,
tutte nel blocco CRT (`fldpi`, `fnclex`, `fstsw`, `fprem/fprem1`, `fscale`, `f2xm1`, `ftst`,
`fxam`, `fsave`, `fstenv/fldenv`, `stmxcsr`, `pushf/popf`, `cpuid`, e 318 istruzioni SSE2 di
`__pow_pentium4`). Il blocco è contiguo e il suo inizio coincide con `__cfltcvt_init`
`0x4F7B18`.

Ingressi dal codice liftato (xref `ida_xrefs` su ogni indirizzo del blocco):

| Procedura | VA | chiamata da |
|---|---|---|
| `__fpmath` | `0x4F7B50` | `__cinit` `0x404E5D`; inoltre il puntatore `__fpmath` è nella tabella di inizializzazione in `.data` `0x51B448` (`dd offset __fpmath`) |
| `__CIpow` | `0x4F7C30` | `0x41DC77`, `0x41DC9C`, `0x41DCC2` (in `sub_41DC20`) |
| `__ftol2` | `0x4F7B70` | 6 chiamate (`0x41DC82/0x41DCA7/0x41DCCD/0x43130A/0x49A9B8/0x4E04D9`) — **resta liftata**: contiene solo x87 supportato e conserva la semantica |
| `_pow` `0x4F7BF0`, `__CIpow_pentium4` `0x4F8400`, `__pow_pentium4` `0x4F8419`, `_has_osfxsr_set` `0x4F82F4`, `___sse2_available_init` `0x4F8329` | — | raggiungibili solo da `__CIpow`/`_pow`: esterni o non disassemblati |

Nessun altro ingresso dal codice liftato verso l'interno del blocco (verifica: grafo degli
xref da ogni istruzione di ogni funzione esterna al blocco → 10 archi, tutti verso
`0x4F7B50`, `0x4F7B70`, `0x4F7C30`).

### 3.2 Blocco CRT — storm (`0x1503FF06`–`0x15043BDF`)

**373 istruzioni non traducibili in 22 funzioni**, tutte nel blocco; le altre 1200 funzioni
della DLL sono traducibili. Ingressi dal codice liftato:

| Procedura | VA | chiamata da |
|---|---|---|
| CRT FPU init (≡ `__fpmath`) | `0x150404C4` | `0x1500811C`; puntatore in `.data` `0x1505B218` (`dd offset sub_150404C4`) |
| `__CIpow` | `0x150406A0` | `0x1502DE15`, `0x1502DE36`, `0x1502DE56` |
| `__ftol2` (≡ EXE) | `0x150404E4` | 14 chiamate (`0x15013B8D`, `0x15013C0F`, `0x1502221A`, `0x15022262`, `0x1502DE7B/DEA0/DEC1`, `0x150332A7/32B1/32BC/32C7`, `0x15034652/465A/4664/466E`) — resta liftata |
| `_aulldiv` | `0x150408F0` | `0x1502BD40` — nessuna istruzione problematica: liftata |
| `___ld12mul`-like | `0x15043BDF` | `0x15043E7C` — nessuna istruzione problematica: liftata |

### 3.3 Puntatori in `.data` che entrano nel blocco storm (tabella `0x1505B28C`)

`0x1505B28C` contiene `0x15041A41`, `0x15041D4D`, `0x15041D52`, `0x15041B81`: sono
*etichette interne* del cluster pow/SSE2, non inizi di funzione. Nessuna istruzione liftata
salta lì (solo riferimenti dati), quindi sono dichiarate procedure esterne inattive (§4.2).

### 3.4 x87 a 80 bit (`tbyte`)

- EXE: 101 istruzioni `fld/fstp tbyte` in 11 funzioni, **tutte dentro il blocco CRT** →
  diventano dati.
- storm: 96 in 13 funzioni, di cui **56 in `sub_1503F817`** e 1 in `sub_15013B30`.
  Catena reale: `0x15016CAB` → `sub_15013B30` → `0x15013C09` → `sub_1503F817`. Il backend
  LLASM non aveva alcun supporto per `tbyte`: prima di questo lavoro l'emitter **non
  emetteva nulla** per `fld/fstp tbyte` (istruzione silenziosamente persa). Ora c'è
  conversione esplicita (§6.4).

### 3.5 Istruzioni che il backend non copriva, presenti in codice di gioco

| Istruzione | EXE | storm | Note |
|---|---|---|---|
| `repe cmpsd` | `0x408EED` (+ 4 siti in `0x4178C4`, `0x4AAD5C`, `0x4AAEF6`, 16 in `0x4B1B87…`, 2 in `0x4B237F`) | `0x1500Bxxx` | solo forma `F3 A7`; prima: *unknown instruction* |
| `bt/bts m32, r32` | `0x408DCD`, `0x408DE8`, `0x408FAD`, `0x408FC1` | `0x1500B38D`, `0x1500B3A8` | prima: nessun output |
| `jecxz` | (non raggiunto) | `0x15006849` | prima: *unknown instruction* |
| `test/and r32, imm` con immediato **rilocato** | `0x4C4811` (`test edx, offset 629268`) | `0x15012BE8` (`and ebx, offset 1505EB80`) | prima: nessun output |
| `test r32, m32` | ramo `UD_OP_MEM` vuoto nell'emitter | idem | implementato; nelle due immagini la forma osservata è quella con immediato rilocato |

### 3.6 Confine di flag `0x407939` (EXE)

`0x407939` è `jge loc_407A6E`, richiede `SF|OF` (maschera `0x30`). Unico xref:
`0x4077DE` `jle loc_407939`, preceduto da `cmp esi, ecx` a `0x4077D0`. Il `jle` consuma
`ZF|SF|OF`, quindi il `cmp` materializza già `SF|OF` sul percorso che arriva a `0x407939`:
l'asserzione `loc_407939,0x30,0x30` in `instruction_flags.sci` è **provata**, non un
mascheramento. Gli altri confini della stessa classe sono stati risolti nel codice (§6.2).

### 3.7 Bersagli di fixup che sono dati, non codice

| Indirizzo | Evidenza IDA |
|---|---|
| `0x406C50`, `0x406DDC` | base di tabella dentro `_memcpy` `0x406BF0`: `0x406C3D` è `jmp dword ptr ds:[eax*4+0x406C50]`, `0x406C4C` è `jmp ds:jpt_406C4C[ecx*4]` (7 byte) → `0x406C50` cade dentro quell'istruzione |
| `0x4B0032` | `.data` `0x50E176` vale `0x004B0032`; IDA lo rende `loc_4B0030+2`, cioè dentro `cmp [eax+20h], cx` a `0x4B0030` |
| `0x150062C0`, `0x1500644C`, `0x1500B480`, `0x1500B60C` | stesso schema in `_memcpy` storm `0x1500B420` |

## 4. File `.sci` (in `build/lift-full/`)

### 4.1 `external_procedures.sci`

```
loc_4F7B50, __fpmath
loc_4F7C30, __CIpow
loc_150404C4, __fpmath
loc_150406A0, __CIpow
loc_15041A41, crt_unlifted_15041A41      (e le altre 20 etichette del §3.3)
…
```
Le procedure esterne non import vengono dichiarate in **`extern.llinc`** (llasm legge da lì i
simboli esterni), perché SRW scrive come `proc … external` solo gli import del PE.

### 4.2 `ignored_areas.sci`

**Non creato** (equivalente a vuoto): nessun intervallo è stato dichiarato dato "morto". Il blocco CRT non è un'area
ignorata ma codice **non raggiunto**: gli ingressi sono esterni, quindi SRW non lo
disassembla e i suoi byte restano dati. Il contenuto originale resta quindi ispezionabile.

### 4.3 `instruction_flags.sci`

```
loc_407939,0x30,0x30
```
Unica asserzione, motivata in §3.6.

### 4.4 `fixup_do_not_interpret_as_code.sci`

I 7 indirizzi del §3.7. Serve a non creare un confine di regione dentro un'istruzione.

### 4.5 `code_entry_points.sci` (meccanismo nuovo, §6.5)

6167 entry: **tutti** gli inizi di funzione dei due DB IDA privati, meno il blocco CRT di
ciascuna immagine. Senza questo file SRW segue solo il flusso dal PE entry point e le
rilocazioni esterne: si fermava a 17.569 procedure (24 % delle funzioni IDA dell'EXE). Con il
file si arriva a 71.112 procedure (98,8 % delle entry IDA) e la IR resta priva di errori.

## 5. `relocations.csv` derivato

Copia di `work/relocs/relocations.csv` con **una sola voce rimossa**:
`0x50E176,0x4B0032,` (31241 → 31240). Motivo: il dword a `0x50E176` vale `0x004B0032`, che
in IDA è `loc_4B0030+2`, cioè *dentro* `cmp [eax+20h], cx`; non è un indirizzo di codice e
la voce produceva un `daddr loc_4B0032` senza etichetta definita. L'originale in
`work/relocs/` non è stato toccato (resta valido come export conservativo).

## 6. Modifiche al codice (tutte generalizzabili, in `vendor/sr/`)

`vendor/sr/SRW/` (SRW, backend OUT_LLASM) e `vendor/sr/SR/llasm-support/` (libreria runtime
delle macro LLASM). `vendor/sr/llasm/` (llasm) **non è stato modificato**.

1. **Output non-istruzione dentro una regione** (`SR_full_llasm.c`): un buco dati in mezzo a
   una regione viene saltato invece di terminare la scansione; un output senza stringa (slot
   creato da una entry dichiarata) chiude la regione. Prima: crash su `output->str[0]`.
2. **Flag attraverso i salti** (`SR_full.c`, `SR_full_llasm.c`, `SR_defs.h`): nuova passata
   che marca i bersagli di salto (`output_data.jump_target`), nuovo campo
   `region_data.flags_required` con i flag vivi all'ingresso di regione, e unione della
   richiesta del bersaglio nell'accumulatore del salto. La scansione lineare di SRW non vede
   i flag che attraversano un salto: senza questo, `cmp`/`test` non materializzavano CF/SF/OF
   su quei percorsi (lift silenziosamente errato). Log: `[flags-jump-require]`,
   `[flags-jump-target]`. Sulle due immagini: 114 propagazioni di regione, 95 richieste al
   salto e 1 ingresso da salto nell'EXE; 31 / 27 / 1 in storm. Il vecchio stop su
   `0x15005DAA` (maschera `0x8`) è ora risolto per propagazione: `from=0x15005daa
   to=0x15005da6 mask=0x8`.
3. **Confini di procedura** (`SR_full.c`, `SR_full_llasm_instr.c`): ogni inizio di regione
   diventa una procedura etichettata; se l'istruzione successiva non è un'istruzione (dati) la
   procedura si chiude senza `tcall`. Elimina "Proc inside proc" / "Instruction outside proc".
4. **`repe/repne cmpsw|cmpsd`** (`SR_full_llasm_instr.c`, `udis86_dep.c`,
   `llasm_cmps.c/.llinc`): la forma a 32 bit non sta nel valore di ritorno a 32 bit usato per
   `cmpsb`, quindi il runtime esegue il ciclo e **scrive direttamente i flag aritmetici**
   (`CF/PF/AF/ZF/SF/OF` nel layout di `llasm_cpu.h`).
5. **`test`/`and`/`or`/`xor` con memoria o con immediato rilocato** (`SR_full_llasm_instr.c`,
   `SR_full_llasm_instr_helper.h`): rami `UD_OP_MEM` implementati (erano vuoti) e nuovo
   `SR_llasm_helper_and_32_label` per l'immediato che è un indirizzo (llasm non accetta una
   label dove vuole una costante: si carica in un temporaneo e si combina).
6. **`bt/bts m32, r32`** (`SR_full_llasm_instr.c`): indice di bit → dword della stringa di bit
   → byte/bit, con `CF` calcolato e, per `bts`, bit impostato e ri-scritto.
7. **`jecxz/jcxz`** (`SR_full_llasm_instr.c`, `SR_full.c`, `SR_full_llasm.c`): `ctcallz` sulla
   condizione `ecx`, marcatura del bersaglio e richiesta di flag propagata.
8. **x87 a 80 bit** (`SR_full_llasm_instr.c`, `llasm_float.c/.llinc`, `llasm.llinc`): nuove
   macro `FLD_TBYTE`/`FSTP_TBYTE` e conversioni IEEE 754 binary80 ↔ binary64 in C
   (`x87_fld_tbyte`, `x87_fstp_tbyte`). La pila x87 emulata è a `double` (`%_cpu` di llasm:
   `[8 x double]`), quindi `long double` è rappresentato con precisione `double` — scelta
   esplicita e dichiarata, non silenziosa. Conversioni verificate dal test host
   `work/lift-full/tools/test_tbyte.c` (PASS, comando in §2).
9. **`nop`** (`SR_full_llasm_instr.c`): la macro `NOP` di llasm è vuota e llasm rifiuta una
   procedura senza istruzioni; l'emitter emette ora una vera `mov tmp7, 0` (eliminata da
   `opt`).
10. **Istruzioni tradotte a vuoto** (`SR_full_llasm_instr.c`): la lista bianca è chiusa
    (`test`, `cmp`, `nop`, `wait`, `prefetch*`, cioè solo effetti sui flag); qualunque altro
    caso resta **errore**. Ogni caso della lista bianca è loggato
    (`Warning: instruction translated as no-op … 0xVA`) per restare verificabile: sulle due
    immagini il log finale è vuoto.
11. **Indirizzo interno a un'istruzione** (`SR_full_llasm_instr.c`): l'emitter emette
    `mov tmpadr, loc_X` + `add tmpadr, tmpadr, delta` invece di `loc_X[delta]`, che llasm non
    accetta; il messaggio diagnostico diventa un warning (il caso è gestito).
12. **Import nel backend LLASM** (`SR_output.c`): vengono dichiarati **tutti** gli import (con
    deduplica). Il backend LLASM non ha tracciamento d'uso: un import riferito solo da una
    parola dati (tabella di puntatori in `.text`) restava non dichiarato e llasm rifiutava
    `daddr <import>`.
13. **Parsing `.sci`** (`SR_source_code_information.c`): le righe che non iniziano con `loc_`
    vengono saltate (prima l'indirizzo restava **non inizializzato** e un commento con una
    virgola creava una procedura esterna fantasma). Nuovo file `code_entry_points.sci`
    (§6.5) con loader dedicato: inserisce entry + `bound begin` solo per sezioni di codice.
14. **Nuovi accessor** (`judy_helpers.*`, `container_helpers.*`): `section_region_list_FindEntryEqual`
    e inizializzazione dei nuovi campi.

### 6.5 Perché `@llvm.compiler.used`

Le procedure generate sono `private fastcc` (llasm le rende private salvo `proc … global`);
nel modello SR + llasm il grafo è tenuto in vita dalla entry global e dai `tcall`. Qui non
esiste ancora un runtime che registri il dispatcher per indirizzo, quindi **`GlobalDCE` di
`opt -O2` cancella l'intera immagine**: `tools/compile-arm.sh` (O2 puro) sull'EXE produce un
oggetto di **460 byte, zero funzioni** — nessun errore, ma nessun codice. Su storm lo stesso
strumento conserva 1,9 MB, perché le funzioni esportate sono `protected` e quindi non
eliminabili. `tools/lift-full.sh obj` aggiunge perciò `@llvm.compiler.used` con tutte le
procedure prima di `opt`: l'oggetto contiene il codice tradotto (13,5 MB / 3,75 MB). Entrambe
le varianti sono verdi (nessun errore di `opt`/`llc`).

### 6.6 File toccati

| File | Righe | Contenuto della modifica |
|---|---|---|
| `vendor/sr/SRW/SR_full_llasm.c` | +~90 | flag per regione (`flags_required`), unione al salto, accettazione del bersaglio, buchi dati, guardia output |
| `vendor/sr/SRW/SR_full_llasm_instr.c` | +~330 | `cmpsw/cmpsd`, `test/and` con memoria e con immediato rilocato, `tbyte`, `bt/bts m32,r32`, `jecxz/jcxz`, `nop`, no-op in lista bianca, confini di procedura, `loc_X+delta` |
| `vendor/sr/SRW/SR_full_llasm_instr_helper.h` | +~70 | `SR_llasm_helper_and_32_label` |
| `vendor/sr/SRW/SR_full.c` | +~75 | marcatura dei bersagli di salto; etichetta a ogni inizio di regione |
| `vendor/sr/SRW/SR_source_code_information.c` | +~55 | guardia `sscanf` su tutte le righe `.sci`; loader `code_entry_points.sci` |
| `vendor/sr/SRW/SR_output.c` | +~30 | dichiarazione di tutti gli import LLASM con deduplica |
| `vendor/sr/SRW/SR_defs.h`, `judy_helpers.*`, `container_helpers.*` | +~20 | `region_data.flags_required`, `output_data.jump_target`, `section_region_list_FindEntryEqual` |
| `vendor/sr/SRW/udis86_dep.c` | +2 | flag di `cmpsw`, `cmpsd` |
| `vendor/sr/SR/llasm-support/llasm_float.c` / `.llinc` | +~230 | conversioni binary80 e `x87_fld_tbyte`/`x87_fstp_tbyte` |
| `vendor/sr/SR/llasm-support/llasm_cmps.c` / `.llinc` | +~110 | `x86_repe/repne_cmpsw/cmpsd` con calcolo dei flag |
| `tools/lift-full.sh` | nuovo | driver: build privata, SRW per immagine, llasm, `compiler.used`, `opt`, `llc` |
| `work/lift-full/tools/test_tbyte.c` | nuovo | test host delle conversioni |

Nessuna modifica a `vendor/sr/llasm/` (llasm), a `work/relocs/`, agli originali o ad altre
aree del repository. Nessuna operazione git.

## 7. Simboli esterni attesi dal runtime

Elenco completo: `build/lift-full/externals-{StarCraft,storm}.txt`.

| Classe | StarCraft.o | storm.o | Chi li fornisce |
|---|---|---|---|
| import Win32 (KERNEL32/USER32/GDI32/…) | 230 | 235 | shim ABI ARM esistenti/da scrivere |
| ordinali `storm.dll` (`ordinal_73746f726d2e646c6c_N`) | **135** | 0 | `storm.o` (o bridge) |
| helper runtime llasm-support `x86_*`/`x87_*` | 33 | 48 | `vendor/sr/SR/llasm-support/*.c` compilato per ARMv6 |
| CRT esterni `__fpmath`, `__CIpow` | 2 | 2 | da implementare (x87 init e `pow`), ABI ospite su stack emulato |
| `crt_unlifted_*` (21) | 0 | 21 | **stub inattivi**: mai chiamati dal codice liftato |
| `llvm.*` | 0 | 0 | — |

I due helper nuovi (`x87_fld_tbyte`, `x87_fstp_tbyte`) compaiono fra gli `x87_*` usati da
`storm.o` (138 trasferimenti `tbyte` liftati), quindi il contratto con il runtime include la
conversione binary80.

## 8. Limiti dichiarati e prossimi passi

1. **Semantica dei flag su CFG non lineare.** SRW analizza i flag in modo lineare lungo le
   regioni. La propagazione ai salti (§6.2) copre i salti in avanti e quelli all'indietro
   tramite richiesta al salto; restano possibili casi in cui il produttore è in una regione
   emessa *prima* del salto che la raggiunge. Sulle due immagini l'esecuzione è pulita
   (`0` errori, `0` no-op), ma non esiste ancora una verifica differenziale: il passo
   successivo è un test x86-vs-ARMv6 su funzioni campione, come già fatto per `storm #494`
   in `work/slice/`.
2. **`long double` = `double`.** Scelta del backend (pila x87 emulata a `double`), dichiarata
   al punto 8 del §6. Per il codice di StarCraft/storm la perdita è di 11 bit di mantissa ed è
   deterministica; non è una traduzione bit-esatta di `tbyte`.
3. **Blocco CRT esterno.** `__fpmath`, `__CIpow` e il cluster pow/SSE2 devono essere
   implementati nel runtime. Fino ad allora il codice che li chiama (`__cinit` per l'init FPU,
   `sub_41DC20` per `pow`) non può essere eseguito.
4. **ABI ospite.** I target esterni sono dichiarati `fastcc void @f(%_cpu*)` da llasm; il
   runtime deve leggere/scrivere argomenti e risultato sullo stack emulato secondo la
   convenzione x86 (`stdcall`/`cdecl`) di ciascuna procedura: l'arità non è deducibile dal
   solo `.ll` e va presa da IDA (per `__ftol2` è `ret` senza immediato; per `__fpmath` un
   argomento a `[esp+4]`).
5. **Import solo-dati.** Tutti gli import sono dichiarati; quelli mai chiamati dal codice
   liftato (solo tabella di puntatori) non hanno ancora uno shim.
6. **`.reloc` di storm e CSV dell'EXE.** La copertura delle rilocazioni resta quella dei
   rispettivi strumenti (`.reloc` reale per storm, CSV conservativo per l'EXE). Una voce del
   CSV è stata rimossa (§5) e non è stato fatto alcun tentativo di provare la completezza.
7. **`work/runtime-arm/` è vuoto**: non esiste ancora un runtime che carichi i due oggetti e
   il dispatcher per indirizzo ospite. Il passo successivo naturale è la dispatch table
   `VA → simbolo` generata dai `.llasm` (le procedure sono già tutte etichettate `loc_VA`).

## Decisioni autonome

- Nessuna richiesta di parere: le scelte di seguito sono state prese e documentate qui.
- **Esternalizzare invece di implementare x87/SSE2 del CRT**: era la strada indicata dal
  task; `__ftol2` è rimasto liftato perché interamente traducibile (fidelità maggiore).
- **`long double` → `double`**: coerente con il tipo `%_cpu` di llasm, che non ha slot a 80
  bit. L'alternativa (implementare binary80 nello stack emulato) cambierebbe il contratto di
  llasm e la struttura `%_cpu`, fuori dal perimetro di questa sessione.
- **`instruction_flags.sci` con una sola riga** (`loc_407939`) invece di allargare il file:
  gli altri confini di regione (EXE: 95 richieste al salto + 114 propagazioni; storm: 27 + 31)
  sono risolti nel codice, non con asserzioni.
- **`code_entry_points.sci`** è un meccanismo nuovo di SRW: senza di esso il lifting si
  fermava a un quarto delle funzioni IDA. È opt-in (file assente = comportamento invariato),
  quindi non cambia il comportamento storico per gli altri utilizzatori di SRW.
- **`relocations.csv` derivato** in `build/lift-full/` con una voce rimossa: l'originale in
  `work/relocs/` resta intatto; la rimozione è motivata da IDA (§5).
- **`@llvm.compiler.used`** applicato solo come post-elaborazione di build
  (`tools/lift-full.sh`), non nel `.llasm`: la IR generata da llasm resta quella che SRW/llasm
  producono.
- Non è stato eseguito il gioco né alcun codice originale tradotto: nessuna esecuzione fuori
  dagli harness.
