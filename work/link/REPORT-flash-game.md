# REPORT — flash-game: mappatura dei byte immagine PE, avanzamento verso `_WinMain@16`

Proprietario runtime unico (sessione 20/11 15:52). Perimetro: `work/link/TASK-flash-runtime.md`;
punto di partenza: `work/link/REPORT-flash-crt.md` §9 (handoff). Oracolo: IDA sul binario x86
originale + i PE originali del lift; il binario prevale sulla documentazione, compresa quella dei
predecessori (che qui viene corretta con i numeri).

## 0. Gate 1 — `.rsrc` non mappata: fatto verificato e sua correzione

### 0.1 Cosa dice il PE originale (numeri, non deduzioni)

Input del lift, hash verificati identici alle copie di lavoro:

```
sha256 build/lift-full/StarCraft.orig.exe  ad6b58b27b8948845ccfa69bcfcc1b10d6aa7a27a371ee3e61453925288c6a46
sha256 build/lift-full/storm.orig.dll      706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25
```

Tabella sezioni (parser PE, `tools/` non modificato):

```
StarCraft.exe  ImageBase 0x00400000  SizeOfImage 0x002EC000  SizeOfHeaders 0x1000
  .text   RVA 0x00001000  VirtualSize 0x000FCF85  RawSize 0x000FD000  chars 0x60000020
  .rdata  RVA 0x000FE000  VirtualSize 0x0000D52C  RawSize 0x0000E000  chars 0x40000040
  .data   RVA 0x0010C000  VirtualSize 0x001D1694  RawSize 0x00010000  chars 0xC0000040
  .rsrc   RVA 0x002DE000  VirtualSize 0x0000DC28  RawSize 0x0000E000  chars 0x40000040
  DataDirectory[2] (rsrc) RVA 0x002DE000 size 0x0000DC28  -> VA 0x006DE000
storm.dll      ImageBase 0x15000000  SizeOfImage 0x00069000
  .text   RVA 0x00001000  VirtualSize 0x00043C75  RawSize 0x00044000
  .rdata  RVA 0x00045000  VirtualSize 0x00013BCE  RawSize 0x00014000
  .data   RVA 0x00059000  VirtualSize 0x00007FD8  RawSize 0x00003000  chars 0xC0000040
  .rsrc   RVA 0x00061000  VirtualSize 0x000026A0  RawSize 0x00003000  -> VA 0x15061000
  .reloc  RVA 0x00064000  VirtualSize 0x000046BA  RawSize 0x00005000
```

**Correzione della §9.1 del predecessore.** Il fault address `0x006DE00C` **non** è «il primo byte
dopo la sezione `.rsrc`»: `.rsrc` comincia a VA `0x006DE000` e la sua VirtualSize è `0xDC28`, quindi
`0x006DE00C` è a **+12 byte dentro la sezione** (la pagina `0x006DE000` è la prima delle 14 pagine
della sezione, che finisce a `0x006EBC28`; il RawSize `0xE000` porta la sezione a `0x006EC000`, che
è anche `ImageBase+SizeOfImage`). Il vecchio testo sbagliava anche `r3`: l'istruzione è
`ldrh r4,[r3,#12]` con `r3 = 0x006DE000` (base della directory radice) **+12 = 0x006DE00C**, che è
esattamente `p+12` di `res_entries()` (`build/runtime-arm/game/shim/tools/elf/win32_ui.c:338-344`:
`*count = *(unsigned short*)(p+12) + *(unsigned short*)(p+14)`). La causa quindi non è «un indirizzo
di 12 byte oltre la sezione»: è che **nessuna `PT_LOAD` del port copre `0x006DE000`**.

Stato del binario precedente (`build/starcraft-armv6-dispatch`, sha256 `ff0fa176…`):

```
$ readelf -lW build/starcraft-armv6-dispatch | grep LOAD
LOAD 0x010000 0x18000000 ... R E   port .text
LOAD 0xad6c0c 0x00406c0c 0x103280 R      exe tdata+rdata
LOAD 0xbdc000 0x0050c000 0x1d1694 RW     exe data
LOAD 0xdb62d8 0x150062d8 0x0507b0 R      storm tdata+rdata
LOAD 0xe09000 0x15059000 0x007fd8 RW     storm data
```

`0x006DE000` cade nel buco fra `0x006DD694` (fine `.guest_exe_data` = fine VirtualSize di `.data`)
e `0x150062D8`: non mappato → SIGSEGV host su una lettura normale del C dello shim.

### 0.2 Regola del modello che il port deve rispettare

Su Windows l'immagine è presente all'`ImageBase+RVA` **per ogni sezione, per
`ceil(VirtualSize/0x1000)` pagine**, con i byte del file fino a `RawSize` e zeri oltre. Quindi non
basta `.rsrc`: restano fuori anche le parti di `.text`/`.rdata` che il lift non ha toccato
(il lift colloca solo i byte *referenziati*, es. exe `.text` `0x00406C0C..0x00436C14` su
`0x00401000..0x004FDF85`). Una lettura del guest in quei buchi oggi legge il `.text` del *port*
(layout vecchio) o va in SIGSEGV: entrambe le cose sono maschere, non funzionamento.

*(sezioni successive aggiornate durante la sessione)*

### 0.3 Fix riproducibile: i buchi del PE collocati ai VA originali

Nuovo strumento, unica fonte di verità: **`runtime/arm/gen_pe_image_bytes.py`**.

* perimetro dei byte già coperti: lo legge dallo script di link in ingresso (righe
  `.guest_<img>_<tag> <VA> : { <obj>(<sezione>) }`) + `llvm-readelf -SW` per la dimensione — nessun
  elenco duplicato, nessuna assunzione sull'IR;
* estensione di ogni sezione PE: `ImageBase + min(page_align(max(VirtualSize,RawSize)), SizeOfImage)`
  — la regola di Windows, non un'approssimazione: `.rsrc` copre `0x006DE000..0x006EC000`;
* per ogni buco: `.incbin` di un blob (byte di file fino a `RawSize`, **zeri oltre**) in una sezione
  `.guest_<img>_<sez>_<n>` con i flag presi da `IMAGE_SCN_MEM_*` (`ax` per `.text`, `aw` per
  `.data`, `a` altrimenti);
* riscrive lo script di link con **tutte** le righe `.guest_*` ordinate per VA;
* emette la tabella `k_pe_gaps` {VA, size, nome, flag} + `k_pe_gaps_n`, registrata dal runtime in
  `arm_va_is_mapped`: le regioni note al memory layer sono esattamente quelle linkate.

`tools/build-entry-obj.sh` la chiama subito dopo aver scritto `guest_data.ld` (passo 2c);
`tools/link-arm.sh` mette `guest_pe_bytes.o` fra gli `IMG_OBJS` quando esiste; `game_main_arm.c`
registra le regioni (simboli `weak`: se l'oggetto non è nel link il ciclo non gira e lo dice).

Due trappole trovate e risolte (misurate, non ipotizzate):

1. **Ordine delle righe = offset di file.** Con le righe dei buchi aggiunte *in coda* (dopo le
   sezioni di storm a `0x150...`), lld assegnava gli offset di file in quell'ordine e il VA non
   tornava più con l'offset: primo tentativo = file da **367.690.344 byte** per una `PT_LOAD`
   `0x006DE000` con `filesz 0x1497B000` (236 MB di zeri fra le due immagini). Da qui la
   riscrittura ordinata per VA.
2. **Confini di segmento.** Anche in ordine, lld fonde in un'unica `PT_LOAD` le sezioni con gli
   stessi flag, *allungando il file* per coprire il salto VA fra l'ultima sezione R
   dell'eseguibile (`0x006DE000..0x006EC000`) e la prima di storm (`0x15001000`) — 236 MB di zeri.
   Marcare i buchi di `.text` come eseguibili (`ax`, che è ciò che dice la sezione PE:
   `0x60000020` = `MEM_EXECUTE`) separa i segmenti *nel punto giusto*: il file resta 21,2 MB.

### 0.4 Verifica del link (candidato `94acc76e…`)

```
$ ENTRY_DIR=…/entry RUNTIME_OBJ_DIR=…/entry/runtime ./tools/link-arm.sh build/starcraft-armv6-rsrc
creato: build/starcraft-armv6-rsrc        (link-errors.txt vuoto)
sha256 94acc76e9dd8b552c0180e9d7a7045dd4921275e54beb623e806fc71ed8aa27e   21.201.512 byte

$ readelf -lW build/starcraft-armv6-rsrc | grep LOAD   (solo i segmenti dell'immagine guest)
LOAD 0xae1000 0x00401000 0x05c0c R E     exe .text 0x00401000..0x00406C0C (buco)
LOAD 0xae6c0c 0x00406c0c 0x2f1dc R       exe .tdata (lift)
LOAD 0xb15de8 0x00435de8 0xc8218 R E     exe .text 0x00435DE8..0x004FE000 (buco)
LOAD 0xbde000 0x004fe000 0x0e000 R       exe .rdata 0x004FE000..0x0050C000 (buco+lift+buco)
LOAD 0xbec000 0x0050c000 0x1d2000 RW     exe .data 0x0050C000..0x006DE000 (lift+coda)
LOAD 0xdbe000 0x006de000 0x0e000 R       **exe .rsrc 0x006DE000..0x006EC000**   <- il gate
LOAD 0xdd1000 0x15001000 0x052d8 R E      storm .text (buco)
… (catena storm completa fino a 0x15069000, .reloc compreso)
```

Nessuna `PT_LOAD` supera i 1,9 MB, nessuna padding oltre le sezioni: i segmenti sono contigui.

```
$ python3 tools/dispatch-ir.py --dump-map build/starcraft-armv6-rsrc
voci: 92934   primo VA 00401000   ultimo VA 15044C73
ordinate e univoche: si
host entry il cui indirizzo coincide con un VA guest: 0        -> exit 0

$ cmp .rsrc del candidato vs .rsrc del PE originale -> identici (57.344 byte)
$ directory radice: NumberOfNamedEntries=1 NumberOfIdEntries=7 -> 8 voci
```

Log del candidato: **`build/qa/run-94acc76e.log`**, hash `build/qa/SHA256-94acc76e.txt`,
`build/qa/link-errors-94acc76e.txt` (vuoto).

### 0.5 Smoke (arm1176) — `.rsrc` non è più un muro, il primo muro è *nominato*

```
$ ulimit -c 0; timeout 90 qemu-arm-static -cpu arm1176 -L build/sysroot \
      ./build/starcraft-armv6-rsrc                                 -> exit 3
[arm-game] runtime ARMv6 — immagini tradotte, import statici
[arm-game] byte PE ai VA originali: 13 regioni
[arm-game] header PE exe a 0x00400000
[arm-game] header PE storm a 0x15000000
[arm-game] cpu=0x0E000000   TEB=0x0E000088  PEB=0x0E001088
[arm-game] bridge: 324 import, 33 UNK dichiarati
[arm-game] entry: start() del CRT a 0x00404C21
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
[win32] OutputDebugStringA: SMemAlloc() : error 125: ERROR #125 (0x8510007d)
[win32] MessageBoxA:  / This application has encountered a critical error: ERROR #125 …
[reasm-arm] GetModuleHandleA di un modulo non presente: dbghelp.dll
[reasm-elf] API NON IMPLEMENTATA: dbghelp.dll  (chiamante 0x189F0674) -> stop controllato
```

Nessun SIGSEGV, nessuno stub silenzioso: la directory risorse è stata letta (è lì che si fermava
il predecessore), il CRT è arrivato a `_WinMain@16` e il gioco ha eseguito il proprio percorso di
errore — `SMemAlloc()` fallisce. Prossimo passo: perché (oracolo IDA su SMemAlloc + il chiamante).
