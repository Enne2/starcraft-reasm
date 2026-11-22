# REPORT — flash runtime (port ARMv6): prima istruzione nativa che lascia il codice tradotto

Avanzamento incrementale. Data: 2026-11-20, 15:3x Europe/Rome. Sessione: `work/link/TASK-flash-runtime.md`.
Perimetro: `runtime/arm`, `tools` di build del port, artefatti `build/`, questo report. Nessun accesso a
guest/`lite.img`/rete QEMU/repo originali (`~/dev/sc-reasm`, `~/dev/m-ht-sr`)/sorgente QEMU condiviso.
Nessun dato proprietario riprodotto: solo indirizzi, hash e meccanismi.

**Stato in una riga:** causa provata (salto indiretto del codice tradotto verso un VA guest non
tradotto, usato come indirizzo ARM) e correzione alla fonte implementata (dispatch VA→entry nel
port, tabella generata dalla IR); **gate di smoke sul candidato nuovo in corso** — esito nella §7.

---

## 0. Gate riprodotto (identità dell'artefatto prima della diagnosi)

```
$ sha256sum build/starcraft-armv6
8785604d5fea62ed52de22dd4ae8fc81d41cbf2bd857aabb8eefa4625f3aec07  build/starcraft-armv6
$ ls -l build/starcraft-armv6        → 8079992 byte, 2026-11-20 13:01
$ cat build/link-errors.txt          → vuoto (nessun simbolo mancante)
$ ulimit -c 0; timeout 60s qemu-arm-static -L build/sysroot build/starcraft-armv6
… [risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
qemu: uncaught target signal 4 (Illegal instruction) - core dumped     → exit 132
```

`CHECKPOINT.md` non è stato modificato (spetta al team QA). Le due copie di core in radice sono
artefatti della sessione precedente, non toccate.

---

## 1. Prima istruzione nativa che lascia il codice tradotto (evidenza)

Ultimi blocchi tradotti (`qemu-arm-static -d in_asm`): `0x8507c → 0x85094 → 0x8509c → 0x850ac →
0x004060c0`. Il blocco `0x850ac` del **nostro** binario (disassemblato dal candidato):

```
  850ac: ldr  r1, [r0, #0x10]   @ r0 = %_cpu* ; +0x10 = _esp
  850b0: ldr  r2, [pc, #0x40]   @ 0x850f8 → word 0x0008507c (indirizzo ARM del punto di ritorno)
  850b4: sub  r3, r1, #4
  850b8: str  r3, [r0, #0x10]   @ _esp -= 4
  850bc: str  r2, [r1, #-4]     @ push del ritorno sullo stack guest
  850c0: ldr  r1, [r0, #0x4]    @ r1 = _cpu._ecx
  850c4: bx   r1                @ ← PRIMA ISTRUZIONE CHE LASCIA IL CODICE TRADOTTO
```

È il codice emesso da LLASM per `tcall <reg>`: `vendor/sr/llasm/llasm.d:2962-2990` (letto, non
modificato) traduce `call reg` / `jmp reg` in

```d
string addr = get_int32_to_ptr(get_parameter_read_value(paramvals[0]), "void(%_cpu*)");
add_output_line("musttail call fastcc void " ~ addr ~ "(%_cpu* %cpu) nounwind");
```

cioè il **valore intero del registro viene usato come puntatore a funzione host**, senza alcuna
traduzione VA guest → indirizzo ARM. Esatto su i386 (codice tradotto ai VA originali), falso qui.

Stato al fault (gdbstub QEMU, `_cpu` letto in memoria; socket: vedi §6):

```
r0 = _cpu = 0x0E000000 ; _ecx = 0x004060C1 (il valore del `bx`) ; _esp = 0x021FFE94
_esi = 0x004A65D0 ; _edi = 0x004A65E0 ; _eflags = 0x00000203
PC = 0x004060C2, stato Thumb ; bt: cpu_llasm_run_raw → loc_404C21 → main (nessun frame host per bordo)
```

## 2. Perché il bit Thumb: `0x004060C1` è un indirizzo x86 *dispari*, non una conversione di flag

Il CHECKPOINT ipotizzava una conversione `_flags` x86 → CPSR. **Refutato**: `_eflags` = 0x203 e nel
runtime non esiste alcuna scrittura di CPSR. Il bit T lo mette l'architettura ARM quando `BX` riceve
un valore dispari. Il valore è dispari perché **il codice x86 ha davvero una funzione che inizia a
un indirizzo dispari**:

- byte dell'immagine originale a `0x004060C0`: `C3` (= `retn`, fine della funzione precedente);
- `0x004060C1`: `68 80 00 00 00` = `push 80h`, poi `call _malloc`, `mov dword_6DD688,eax`,
  `mov dword_6DD684,eax`, `retn` → inizializzatore della CRT (heap/onexit), verificato in IDA
  (§3).
- La tabella `_initterm` della CRT (VA guest `0x0050C4C0`, xref `_cinit` a `0x00404E62`) contiene
  esattamente quattro puntatori: `0x4060C1`, `0x406AA1`, `0x40D8C6`, `0x4F8329`.
- Nel binario attuale quella tabella è nei dati dell'immagine tradotta e conserva i **VA x86**
  (`readelf` di `.data` a offset 0x4C0 del blob guest: `0x4060C1, 0x406AA1, 0x40D8C6, 0x4F8329`).
- `bx` su `0x004060C1` → PC `0x004060C0` **nel nostro `.text`** (che copre `0x00080C50-0x00486040`,
  e quindi *si sovrappone* all'immagine x86 `0x00400000-0x006DE000`), stato Thumb, byte
  `ea ff 8c 8a` (= `b 0x3e92f0` in ARM) decodificati come Thumb → SIGILL.

Da qui: la diagnosi del CHECKPOINT («`0x4060C0` è il `retn` di `__onexit_lk`») identificava un
indirizzo che *coincide* ma non è la causa; il chiamante è `_cinit` → `_initterm` → puntatore della
tabella, e la causa è l'assenza di traduzione per i salti indiretti *e* il buco di lifting su 4
inizializzatori.

## 3. Oracolo IDA (database aperto sul *nostro* esemplare, `build/lift-full/StarCraft.orig.exe`)

Sessione IDA `3e5dfe4a` creata da questa sessione (nessun'altra sessione del progetto toccata; la
sessione `LIBDAY.EXE` presente non è stata usata). Disassemblato:

| VA | IDA | istruzioni |
|---|---|---|
| `0x4060C1` | nessun nome, non definita come funzione | `push 80h; call _malloc; test eax,eax; pop ecx; mov ds:6DD688,eax; jnz; push 18h; pop eax; retn` |
| `0x406AA1` | `___initmbctable` (non definita come funzione) | `cmp ds:6DD68C,0; jnz; push 0FFFFFFFDh; call __setmbcp; mov ds:6DD68C,1; xor eax,eax; retn` |
| `0x40D8C6` | nessun nome | `mov eax, ds:6DD300; test eax,eax; …` |
| `0x4F8329` | `___sse2_available_init` | `push ebp; mov ebp,esp; sub esp,20h; …` |
| xref `0x50C4C0` | — | `0x00404E62` in `__cinit` |

Sono quattro inizi di funzione reali che il lifting non ha coperto: `code_entry_points.sci` è
costruito come «tutti gli inizi di funzione IDA **meno il blocco CRT**» (`work/lift-full/REPORT.md`
§4.5), e il runtime i386 li eseguiva perché il codice stava ai VA originali. Su ARM non esistono
finché non si dichiarano come codice.

## 4. Correzione alla fonte (implementata in questa sessione)

Due parti, entrambe riproducibili da script; nessuno stub silenzioso, nessuna disattivazione.

### 4.1 Dispatch dei salti indiretti (`runtime/arm` + `tools/dispatch-ir.py`)

- `tools/dispatch-ir.py` (nuovo, 3 s sulle due IR):
  - porta le procedure `loc_*` da `private` a `hidden` (necessario perché la tabella le referenzi
    da un altro modulo: stesso motivo già documentato in `work/runtime-arm/REPORT.md` §14.5);
  - riscrive **ogni** salto indiretto con target dinamico:
    `musttail call fastcc void %N` → `store i32 %T, i32* @reasm_indirect_target` +
    `musttail call fastcc void @reasm_indirect(%_cpu* %cpu)` (il valore viaggia in memoria perché
    l'ABI `musttail` impone firma identica a quella del chiamante);
  - emette la tabella ordinata VA→entry di **92839** voci (exe 71111 + storm 21727 + 1 extra),
    come due array paralleli `[N x i32]` / `[N x ptr]` (LLVM 22 rifiuta struct letterali dentro un
    inizializzatore di array: verificato con `llvm-as`);
  - lascia intatti i 137+75 siti in cui il target è **già un simbolo** (`inttoptr(ptrtoint(@Sym))`):
    sono indirizzi ARM e il `bx` diretto è corretto e più veloce. Conteggio: 11250 riscritti.
- `runtime/arm/indirect_dispatch.S` (nuovo): `reasm_indirect` risolve e **salta** (`bx`), non
  chiama: nessun frame host per bordo, come i bridge stub.
- `runtime/arm/indirect_dispatch.c` (nuovo): cache diretta 4096 voci su `target>>2`, ricerca
  binaria sulla tabella, poi fallback «indirizzo del nostro codice» (`__executable_start`..`etext`)
  per i ritorni (`store i32 ptrtoint(@loc_XXXX)` emesso da LLASM) e per i bridge stub; qualunque
  altro valore → `reasm_indirect_unresolved()`: **stop dichiarato** con target, VA del chiamante
  (reverse-map), `esp`, `eax/ecx/edx`, `eflags`, uscita 71. Mai un salto a caso.
- `runtime/arm/game_main_arm.c`: chiama `reasm_dispatch_init()` prima di entrare nel guest.
- `tools/build-entry-obj.sh` (nuovo): riproduce in uno script la catena IR → oggetti che il 20/11
  era stata eseguita a mano (`CHECKPOINT.md` §4-5) e vi inserisce `dispatch-ir.py`; artefatti in
  `build/lift-full/entry/` (i vecchi `build/lift-full/{StarCraft.entry.o,storm.o}` restano come
  fallback).
- `tools/link-arm.sh`: `ENTRY_DIR`/`RUNTIME_OBJ_DIR` opzionali; il glue (`game_glue.c`) ora viene
  ricompilato nel repo (`build/link/game_glue.o`) invece di dipendere da `/tmp/game_glue.o`.

Invariante verificabile: dopo il link `tools/dispatch-ir.py --dump-map <elf>` legge la tabella dal
binario e controlla che nessun indirizzo di entry host coincida con un VA guest (unico caso in cui
l'ordine di risoluzione potrebbe mascherare un errore); esce 3 se la collisione esiste.

### 4.2 Buco di lifting (4 inizializzatori della CRT) — **prossimo passo, non ancora applicato**

Aggiungere a `build/lift-full/code_entry_points.sci` (input riproducibile, copiato nei run-dir da
`tools/lift-full.sh`):

```
loc_4060C1
loc_406AA1
loc_40D8C6
loc_4F8329
```

poi `tools/lift-full.sh srw exe && tools/lift-full.sh ir exe` (~rigenera `build/lift-full/StarCraft.ll`
e i `.llinc`), quindi `tools/build-entry-obj.sh` e il link. Con la tabella in essere queste quattro
voci diventano risolvibili e il gioco supera `_initterm`; senza, il nuovo runtime **si ferma
dichiarando** `target = 0x004060C1, eip chiamante = 0x00404E62` — cioè il difetto diventa un
messaggio invece di un SIGILL.

---

## 5. Comandi (esatti)

```bash
cd ~/dev/starcraft-armv6
./tools/build-entry-obj.sh --check                      # input e strumenti
./tools/build-entry-obj.sh                              # IR -> oggetti (+ tabella + dispatch)
ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
  ./tools/link-arm.sh build/starcraft-armv6-dispatch    # link del candidato nuovo
sha256sum build/starcraft-armv6-dispatch
python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch
ulimit -c 0
timeout 90s qemu-arm-static -cpu arm1176 -L build/sysroot build/starcraft-armv6-dispatch
```

Artefatti grandi: `build/lift-full/entry/{*.ll,*.o}` (intermedi) e `build/entry-obj.log`.
Lo `/tmp` di questa macchina è al ~80 % con materiale di altri progetti: **non** scrivere lì gli
intermedi (un primo tentativo è fallito con `EDQUOT`).

## 6. Note operative

- **gdbstub**: QEMU user 10.2.2 **non** accetta `unix:path` (testato: `Failed to bind socket …
  No such file or directory`, exit 1). Le fermate sono state fatte con `-g tcp:127.0.0.1:<porta>`
  solo su loopback, chiudendo il PID esatto avviato (`kill $!`), mai `pkill -f`.
- Trace limitati: `-d in_asm` (~3 MB) per gli ultimi blocchi; `-d in_asm,cpu` produce ~88 MB e
  tronca la coda, non è affidabile per il punto di fault (la fermata precisa la dà il gdbstub).
- `ulimit -c 0` in tutte le prove.

## 7. Gate del candidato nuovo (esito del 20/11, 15:29)

```
$ ENTRY_DIR=$PWD/build/lift-full/entry RUNTIME_OBJ_DIR=$PWD/build/lift-full/entry/runtime \
    ./tools/link-arm.sh build/starcraft-armv6-dispatch
creato: build/starcraft-armv6-dispatch            → LINK_EXIT=0
-rwxr-xr-x 1 enne2 enne2 19822948 20 nov 15.29  build/starcraft-armv6-dispatch
sha256: c2604f22d431ad76b8b27f5986053489a76c7c16f2cf3de20cc1192d00a2df38
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch
voci: 92839   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 841     → EXIT=3
```

**Primo sintomo del candidato nuovo: il link riesce e la tabella è corretta e ordinata, ma
l'invariante di non-ambiguità FALLISCE su 841 voci** (es. VA guest `0x0045B692` = indirizzo della
entry host `0x004013B0`). Non è un difetto del dispatch: è la **sovrapposizione di spazi di
indirizzi** già segnalata in §3 del primo report — il `.text` del port
(`0x00080C50-0x00486040`) cade dentro l'immagine x86 (`0x00400000-0x006DE000`), quindi 841
indirizzi del nostro codice coincidono numericamente con VA di funzioni guest. In questo stato un
ritorno (`store i32 ptrtoint(@loc_XXXX)`) che capita su uno di quei 841 valori verrebbe risolto
come chiamata alla funzione guest sbagliata: **non è accettabile come candidato QA**.

Prova non ancora eseguita (per non lasciare build orfane a fine turno): lo smoke del binario
`-dispatch`. Atteso, con lo stato attuale: nessun SIGILL (il salto indiretto ora è dichiarato) e
stop con `[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto` per `0x004060C1`
(i quattro inizializzatori CRT non sono ancora liftati, §4.2), oppure un esito più avanti se il
percorso evita quella tabella.

### 7.1 Passo successivo (piccolo, verificabile)

Rendere i due insiemi disgiunti **alla fonte**, spostando il codice del port fuori dai VA guest, e
riprovare il controllo:

```bash
# candidato: finestra libera misurata (arena 0x0E000000+16MB, stack guest 0x02000000+4MB,
# immagini 0x00400000-0x006EBC28 e 0x15000000-0x1506FFFF, VirtualAlloc propone da 0x20000000)
LDFLAGS_EXTRA='-Ttext-segment=0x18000000'   # da aggiungere alla riga ld.lld di tools/link-arm.sh
python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch   # atteso: 0 collisioni
```

Se `ld.lld` non accetta `-Ttext-segment`, l'equivalente va verificato con `-Ttext=0x18000000`
(controllando con `readelf -SW` che *nessuna* sezione finisca nei range guest). Solo dopo: smoke e
poi i 4 entry point (§8).

## 8. Handoff compatto (per la sessione successiva)

**Provato**
1. Il fault è un salto indiretto (`bx` su `_cpu._ecx` = `0x004060C1`) emesso da `tcall reg`
   (LLASM) verso un VA guest non tradotto; bit Thumb = bit0 di un indirizzo x86 dispari reale.
2. La tabella `_initterm` a `0x0050C4C0` contiene 4 inizializzatori CRT non liftati
   (`0x4060C1, 0x406AA1, 0x40D8C6, 0x4F8329`); IDA li vede come codice (uno con nome
   `___initmbctable`, uno `___sse2_available_init`), ma non come funzioni definite.
3. L'ipotesi CPSR/flag del CHECKPOINT è falsa.

**Ipotesi non ancora verificate**
- Che dopo i 4 inizializzatori la CRT prosegua fino a `_WinMain@16`: il prossimo stop potrebbe
  essere l'allocatore diviso (`HeapAlloc`→glibc vs `VirtualAlloc`→arena, §14.6 del report
  `work/runtime-arm`).
- Che la sovrapposizione VA (`.text` del port `0x80C50-0x486040` ⊂ immagine x86
  `0x400000-0x6DE000`) non crei altri casi ambigui: mitigata dall'ordine di risoluzione
  (tabella prima) e dal controllo `--dump-map`, da rendere parte del gate.

**File toccati in questa sessione**
`runtime/arm/indirect_dispatch.{h,c,S}` (nuovi), `runtime/arm/game_main_arm.c` (init dispatch),
`tools/dispatch-ir.py` (nuovo), `tools/build-entry-obj.sh` (nuovo), `tools/link-arm.sh`
(`ENTRY_DIR`/`RUNTIME_OBJ_DIR`, glue ricompilato), questo report. `CHECKPOINT.md` intatto.

**Fix/test più piccolo successivo**
0. **(prima di tutto, §7.1)** spostare il testo del port a `0x18000000` nel link e riportare
   `--dump-map` a 0 collisioni: finché sono 841 il candidato non è idoneo a QA (un ritorno può
   essere scambiato per un VA guest).
1. `printf 'loc_4060C1\nloc_406AA1\nloc_40D8C6\nloc_4F8329\n' >> build/lift-full/code_entry_points.sci``
2. `./tools/lift-full.sh srw exe && ./tools/lift-full.sh ir exe` (verificare che compaiano
   `define … @loc_4060C1(` e soci in `build/lift-full/StarCraft.ll`)
3. `./tools/build-entry-obj.sh && ENTRY_DIR=… RUNTIME_OBJ_DIR=… ./tools/link-arm.sh build/starcraft-armv6-dispatch`
4. `python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-dispatch` (atteso: 92843 voci,
   0 collisioni) → smoke `timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot …`
