# REPORT — flash-speed: catena di build accelerata (misure, non impressioni)

Sessione 20/11/2026 dalle 17:07. Writer runtime unico ARMv6.
Perimetro: `tools/` di build, `build/`, `runtime/arm`. Nessun guest, git, sotto-agenti, repo
originali. Candidati esistenti non sovrascritti: il link di prova produce
`build/starcraft-armv6-speed` (e `-local` per default), mai `-mt`/`-switch2`.

Obiettivo: ciclo di iterazione **<15 s** quando cambia solo il runtime C/S, senza cambiare
semantica del port né esito dello smoke.

---

## 1. Risultato in una tabella

| scenario | prima | dopo | come |
|---|---|---|---|
| catena IR completa, cache fredda | ~170 s (2m49-3m22) | **158,5 s** | opt+llc di exe e storm in parallelo (−7 %, vedi §4) |
| catena IR completa, `--fast` | — | **126,1 s** | `opt default<O1>` + `llc -O1` (−26 %) |
| catena IR completa, **nulla è cambiato** | ~170 s | **0,33-0,51 s** | cache content-addressed per fase |
| **iterazione su runtime C/S** (`--runtime-only`, nulla cambiato) | ~170 s | **1,22-1,30 s** | cache + link 0,33 s + smoke 0,60 s |
| **iterazione su runtime C/S** (1 file `.c` modificato) | ~170 s | **1,40-1,78 s** | 1 oggetto ricompilato (0,31 s) + link + smoke |
| `--ir` (solo codegen, runtime C/S intatto) | — | come «catena IR» | salta runtime/bridge/link |
| `--runtime-only --no-smoke` (link + runtime) | — | **0,64 s** | solo il giro di link, senza lanciare il gioco |

Obiettivo <15 s: **raggiunto** (1,2-1,8 s nel ciclo reale, 0,5 s se cambia solo l'IR e la cache
regge). Il `--fast` da solo **non** basta (−26 %), quindi non è la strada: lo tengo come opzione
misurata per chi vuole provare una modifica al lift senza aspettare la O2 completa.

---

## 2. Cosa è stato implementato (`tools/build-entry-obj.sh`)

1. **Cache content-addressed per fase** in `build/lift-full/entry/.cache/`.
   Chiave = `sha256(mode+flag del comando + input)`, 16 hex. Ogni fase ha il proprio artefatto
   `<nome>.<chiave>`: se c'è, si copia e la fase si salta, con `[cache hit] <fase>` nel log.
   Per non rileggere i `.ll` da 75-131 MB a ogni passo, le fasi a valle usano **la chiave della
   fase precedente** invece del contenuto (`key_of "opt <O2> $DL" "$KEY_VERIFY"`).
   Fasi coperte: `dispatch-ir`, `opt verify`, `opt <O2|O1>`, `llc`, `layout+PE-bytes`,
   `bridge`, e **ogni singolo oggetto del runtime** (13 file).
2. **Modalità dichiarate**: `--ir`, `--runtime-only`, `--fast`, più `--no-cache`, `--no-smoke`,
   `--link-out <path>`, `--check`. Ogni run stampa `fasi eseguite:` e `fasi da cache:` (incluse
   quelle dei due sottoprocessi, via un file di log condiviso) e il tempo totale.
3. **Parallelismo** `opt+llc` di StarCraft e storm in background con `wait` (prima in sequenza).
4. **Rigenerazione condizionata** di `guest_data.ld` + `guest_pe_bytes.o` e di
   `bridge_stubs.S`/`bridge_symbols.c`/`bridge_table.c`: dipendono solo dai PE, dal template del
   linker script e dai `.def`, quindi non si rigenerano se quegli input non cambiano.
5. **Tempi per fase** in `build/qa/timing.jsonl` (una riga JSON per fase: ISO, modalità, durata ms,
   `cache`), incluse le fasi dei sottoprocessi e il totale a muro.

Nessuna modifica alla semantica: gli stessi comandi (`opt`, `llc` con gli stessi flag), lo stesso
`guest_data.ld`, lo stesso ordine di link. Cambia *quando* una fase viene eseguita, non *cosa* fa.

---

## 3. Prova di equivalenza (criterio di accettazione)

| prova | esito |
|---|---|
| binario linkato dalla nuova catena vs candidato QA `-mt` | **identico byte per byte**: `377e32c678397a28a06814b3e556088b7e7a271c1f4f6986499be2aa11270f8d` (entrambi) |
| oggetti/artefatti vs build precedente (36 file, `build/qa/before-hashes.txt`) | identici (l'unica riga in più è `entry/tls_arm.o`, residuo di un mio bug poi corretto e rimosso) |
| `dispatch-ir.py --dump-map` | **96756 voci**, primo VA `00401000`, ultimo `15044C73`, ordinate e univoche, **0 collisioni** |
| `make check` | **EXIT=0** (`PASS: ordinal PE + Wine x86; … ARMv6/VFPv2 attributes verified.`) |
| smoke `-mt` equivalente (`build/qa/run-mt.log` vs `run-fast.log`, indirizzi host normalizzati) | stesso punto di stop: stesso proc tradotto `VA 0x00420980` offset 0, `fault address 0x00010011`, `guest eip 0x71000001`, `esp 0x021FF928`; differiscono solo 2 righe della catena euristica sullo stack |
| smoke del binario prodotto da `--runtime-only` | `[host] finestra creata` + SIGBUS in `dlg_elf_wndproc` (EXIT=9), come il candidato |

Ripetibilità della cache: cancellati **tutti** gli oggetti (`entry/*.o` e `entry/runtime/*.o`) e
rilanciato `--runtime-only`, la catena li ripristina dalla cache in 1,2 s e rilinka **lo stesso**
`377e32c6…`. La cache non è quindi un «forse»: è ricostruibile e simmetrica (anche il ritorno da
`--fast` a O2 ripristina gli artefatti O2 originali, verificato).

---

## 4. Perché il parallelismo rende poco (misura, non opinione)

Fasi del primo run freddo (`build/qa/speed-cold.log`, due catene in parallelo):

| fase | StarCraft | storm |
|---|---|---|
| `opt verify` | 3 056 ms | 869 ms |
| `opt default<O2>` | 75 627 ms | 11 247 ms |
| `llc` | 74 858 ms | 14 074 ms |
| `dispatch-ir` (condiviso) | 3 519 ms | — |
| layout+PE-bytes / bridge | 397 ms / 195 ms | — |

Totale a muro 158,5 s contro 170 s in sequenza: **−7 %**, perché l'immagine exe (154 s di CPU) è
dominata da sola (legge di Amdahl: lo storm occupa 26 s che si nascondono sotto l'exe). Il
parallelismo resta perché è gratis e non cambia l'esito, ma il guadagno vero viene dalla cache.

Nota: `dispatch-ir` costa 3,5 s (non 2 s come nel conteggio iniziale) perché riscrive 11 858
call-site su due moduli; è comunque una fase sola, in cache.

---

## 5. `--fast`: misurato, non adottato come default

`--fast` = `opt -passes='default<O1>'` + `llc -O1` (verify invariato). Log `build/qa/speed-fast.log`:
StarCraft 58 290 + 67 328 ms, storm 7 714 + 11 679 ms → **126,1 s** (−26 % rispetto a 158,5 s).
Lo stop **non cambia**: stessi valori di §3 (`VA 0x00420980`, fault `0x00010011`, eip
`0x71000001`), dispatch map identica.

Motivo per cui non è il default: −26 % non cambia la vita (2m06 contro 2m38) e il codice prodotto è
meno ottimizzato (cambia il pc host dentro il proc — `0x1810E2F8` invece di `0x18112B1C`, stesso
`entry VA 0x00420980` con offset 0 — e cambiano 2 righe della catena euristica sullo stack). Serve invece come strumento
per *iterare su una modifica del lift*, e in quel caso conviene ricordarsi che l'`llc` resta la fase
dominante: 67 s su 126.

---

## 6. `llvm-split` dell'IR exe in 4 moduli: valutato e **NON adottato**

Misura fatta davvero (non stimata), sull'IR ottimizzato O2 (`StarCraft.optimized.ll`, 131 MB):

```
llvm-split -j 4 -o /tmp/split4.ll <optimized.ll>        39,4 s  ->  4 moduli da 10-12 MB
llc (4 moduli in parallelo, stessi flag)                10,8 s  ->  4 oggetti (25,4 MB totali)
confronto: llc su modulo unico                          74,9 s  ->  1 oggetto (18,9 MB)
```

Quindi: 39,4 + 10,8 = 50,2 s contro 74,9 s → **−25 s** sul run freddo (158 → ~134 s), a fronte di:

* **il link si complica davvero**: `guest_data.ld` seleziona le 6 sezioni dati guest *per nome di
  oggetto* (`$P/StarCraft.entry.o(.rodata..L_text)`, …). Con 4 oggetti per immagine la sezione
  finisce in *uno* dei quattro (scelto da llvm-split in base al contenuto, non stabile), e i nomi
  di sezione sono gli stessi per exe e storm: servirebbe un `EXCLUDE_FILE(...)` per ogni sezione,
  cioè una regola che dipende da dove llvm-split ha messo il blob — fragile per costruzione.
* **il binario non sarebbe più identico** al candidato QA: la prova di equivalenza di §3 (byte per
  byte) cadrebbe e andrebbe rifatta da capo sullo stop, che è l'unica cosa che conta.
* **gli oggetti crescono** (25,4 MB contro 18,9 MB): le costanti private vengono duplicate nei
  moduli.
* Non aiuta il caso dominante: quando l'IR è in cache queste fasi non girano affatto.

Verdetto: **non adottato**, come da indicazione («meglio una catena semplice e affidabile»). Se in
futuro servisse, la strada pulita è separare i blob dati in un oggetto dedicato *prima* del lift
(così la selezione per nome di sezione non dipende più da llvm-split), non piegare il linker script.

---

## 7. Come si usa (comandi esatti)

```bash
cd /home/enne2/dev/starcraft-armv6

# ciclo di iterazione su runtime/arm/*.c|*.S  (1-2 s: ricompila, rilinka, smoke)
./tools/build-entry-obj.sh --runtime-only

# solo IR/codegen (dispatch-ir + opt + llc + layout), runtime e link intatti
./tools/build-entry-obj.sh --ir

# candidato completo (default invariato); con cache: 0,5 s se nulla è cambiato
./tools/build-entry-obj.sh

# prova di una modifica al lift senza pagare la O2 completa (esito smoke invariato, misurato)
./tools/build-entry-obj.sh --fast

# forzare tutto (equivalente al vecchio comportamento)
./tools/build-entry-obj.sh --no-cache
```

`--runtime-only` collega `build/starcraft-armv6-local` e lancia lo smoke in
`build/asset-sandbox` (log `build/qa/run-last.log`, con `[smoke] exit=N` in coda). Opzioni:
`--no-smoke`, `--smoke-timeout <s>` (default 25), `--link-out build/<nome>`. Tempi per fase in
`build/qa/timing.jsonl`.

**Attenzione al costo dello smoke quando il gioco non esce**: dal §11 di
`REPORT-flash-heap.md` il candidato con il fix dei dialoghi arriva a un nuovo muro (stop su
`0x00427714`) e il processo **non termina da solo** (§11.2 di quel report): lo smoke paga il
timeout (25 s). In quel caso l'iterazione è 1,5 s + 25 s; per restare sotto i 15 s usare
`--smoke-timeout 12` o `--no-smoke` + smoke manuale. I numeri di §1 (1,2-1,4 s) sono misurati sul
candidato `-mt`, il cui smoke esce subito (SIGBUS, exit 9).

La cache occupa **550 MB** (`build/lift-full/entry/.cache`, 31 artefatti: i `.ll` intermedi sono
grandi). Si azzera con `rm -rf build/lift-full/entry/.cache` (nessun rischio: tutto è rigenerabile,
la prima esecuzione dopo paga la catena fredda).

Una trappola trovata e corretta (la scrivo perché è facile ricaderci): lo smoke fa
`cd build/asset-sandbox`, quindi un `--link-out` **relativo** diventa irraggiungibile e qemu esce 1
in 3 ms senza stampare nulla. Ora `LINK_OUT` viene reso assoluto subito dopo il parsing.

---

## 8. Decisioni autonome

1. **Default invariato** (catena completa con `opt<O2>`): il candidato da consegnare a QA resta
   quello di sempre, e la cache è trasparente. `--fast` è opt-in.
2. **Cache per singolo oggetto del runtime** (chiave = sorgente + *tutti* gli header): un `.c`
   toccato ricompila 1 file (0,31 s), un header ricompila tutto (sicuro, 0,9 s). Scelta
   conservativa sul caso pericoloso.
3. **Chiavi a catena invece del contenuto dei `.ll`** a valle della prima fase: senza, ogni run
   avrebbe pagato ~0,5 s di sha256 su 131 MB × 3 fasi.
4. **`llvm-split` non adottato** (§6) e **`--fast` non promosso a default** (§5): misurati, motivati.
5. **Nessun candidato sovrascritto**: i link di prova usano nomi nuovi (`-speed`, `-local`,
   `-fast`, `-dispatch`). `build/starcraft-armv6-mt` resta il candidato QA con lo stesso hash.
6. **La catena di build non tocca la semantica**: nessun flag di compilazione nuovo, nessuna
   modifica a `runtime/arm/*.c|h|S`, `tools/dispatch-ir.py`, `runtime/arm/gen_pe_image_bytes.py`,
   `tools/link-arm.sh`. Per l'accelerazione l'unico file modificato è `tools/build-entry-obj.sh`
   (e la prova è il binario **byte-identico** di §3).
7. **Timeout dello smoke**: default 25 s (`--smoke-timeout`) invece dei 180 s fissi. Il gioco può
   non uscire da solo (§8 nota), e un timeout di 3 minuti rende inutilizzabile il ciclo. Il log
   resta completo perché lo STOP stampa tutto prima.
8. **Il fix dei dialoghi (§9) è lavoro funzionale, non di build**: tocca
   `runtime/arm/bridge_arm.c`, `runtime/arm/bridge_table.def`, `runtime/arm/gen_bridge_stubs.py`,
   non la catena. Non è promosso a candidato QA: il suo smoke non esce (timeout) e il muro dopo è
   nuovo. Va validato a parte.

## 9. Fatto anche il muro funzionale (con la catena nuova)

Con `--runtime-only` (1,4 s per giro) ho chiuso il muro §9.1 di `REPORT-flash-heap.md` **senza
toccare gli shim**: `DialogBoxParamA` è ora un `HAND` del port che passa allo shim un trampolino
C-ABI verso `arm_guest_call4` (dettagli e misure in `REPORT-flash-heap.md` §11). Il dialogo si apre,
riceve `WM_INITDIALOG`, presenta un frame e ritorna `result=2` al guest; il SIGBUS non c'è più.
Candidato: `build/starcraft-armv6-dlg` (`99a5f498…`), dispatch 96756 voci / 0 collisioni,
`make check` EXIT=0.

Il muro è ora un altro (§11.1 di quel report: `call [tabella appaddex]` verso `0x00427714`, che
non è un ingresso di funzione). Il candidato QA `-mt` resta intatto e con lo stesso hash: il fix
dei dialoghi non è ancora promosso a candidato perché il suo smoke finisce in timeout (§8 nota) e
perché va prima capito il nuovo stop.
