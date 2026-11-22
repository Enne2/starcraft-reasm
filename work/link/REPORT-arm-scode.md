# Report — interprete ARM SCode (menu7)

## Esito

Implementati nel solo `runtime/arm/storm_sdraw_arm.c` gli opcode incontrati nei due kernel SCode del menu. Il kernel allineato a `0x20178C52` ora prosegue; il test raggiunge poi un ulteriore blit SRCCOPY che esce intenzionalmente con `exit(73)` dal fail-closed di `storm_313_arm_body` perché entra nel ramo non-fast/alignment, non nell’interprete. Quindi **l’obiettivo 30 s nel menu principale non è raggiunto**; non dichiaro il test PASS. Nessun `STOP` o `SIG*` osservato. FPS regolari fino al secondo blit.

## Evidenza e opcode

### Kernel 1 — allineato, entry `0x20178C52`

Alla prima fermata era stato richiesto il dump diagnostico di 256 byte. Acquisito il buffer e disassemblato in un unico input con:

```sh
llvm-mc --disassemble -triple=i386-unknown-linux-gnu
```

La decodifica mostra `mov ecx,[edi]`, `mov edx,[esi]`, byte-register move, `add esi,4`, accessi LUT `[eax+ebp]`, `ror reg32,16`, `mov [edi],ebx`, `add edi,4` e inizializzazione `mov eax,0`. Il dump finisce a metà istruzione, come previsto dal limite di 256 byte. La forma per-pixel si ripete.

### Kernel 2 — allineato, entry runtime `0x20B20101`

Il primo kernel avanzò oltre il suo precedente arresto. Il secondo stop riportò una diversa variante: `mov cl,[edi]`, `mov dl,[esi]`, `mov al,cl`, `mov ah,dl`, `inc esi`, `mov al,[eax+ebp]`, `mov [edi],al`, `inc edi`, `jmp rel32`; il successivo loop si reitera. Questi opcode sono stati implementati. Il log completo del primo run diagnostico, incluso il dump 256 byte, è `build/qa/armfix-menu7-diag-smoke.log`.

### Opcode coperti nell’interprete

- Invariati e già presenti: `C7 C0 imm32` (`mov eax,imm32`), `8A C4`, `8A 26`, `FF C6` (`inc esi`), `8A 04 28` (`mov al,[eax+ebp]`), `66 89 07` (`mov [edi],ax`), `81 C7 imm32` (`add edi,imm32`), `89 0F` (`mov [edi],ecx`), `E9 rel32` (salti interni e continuazioni Storm già riconosciute).
- Aggiunti per il kernel 1: `8B 0F` (`mov ecx,[edi]`), `8B 16` (`mov edx,[esi]`), `8A C1` (`mov al,cl`), `8A E2` (`mov ah,dl`), `81 C6 imm32` (`add esi,imm32`), `8A 1C 28` (`mov bl,[eax+ebp]`), `8A C5` (`mov al,ch`), `8A E6` (`mov ah,dh`), `C1 C9/CA/CB 10` (`ror ecx/edx/ebx,16`), `8A 3C 28` (`mov bh,[eax+ebp]`), `89 1F` (`mov [edi],ebx`).
- Aggiunti per il kernel 2: `8A 0F` (`mov cl,[edi]`), `8A 16` (`mov dl,[esi]`), `88 07` (`mov [edi],al`), `FF C7` (`inc edi`).

I byte-register sono mappati secondo l’encoding x86 (`AL/CL/DL/BL`, poi `AH/CH/DH/BH`); le letture/scritture word/dword restano little-endian. La rotazione di 16 su un registro a 32 bit scambia i due halfword. Nessun `test/cmp/jz/jnz`, flag condizionale o altro opcode non osservato è stato aggiunto. Gli opcode non coperti mantengono `exit(73)`.

La revisione IDA read-only della funzione originale `storm_313` (`0x15036260`) conferma: il fast path richiede ROP `0xCC0020` o `0xF00021`, `(dst|width)&3 == 0` e larghezza `<= 0x280` (`0x15036282..0x150362AE`); altrimenti costruisce il descrittore e chiama `storm_334` a `0x150363BB`. Il wrapper ARM corrente rifiuta questo ramo, come attesta il log finale. L’interprete non è stato esteso con una semantica inventata per quel diverso blit.

## Build e verifica

Build richiesta (TMPDIR su `/dev/shm`):

```text
[entry-obj] runtime: 15 da cache, 1 ricompilati (479 ms)
[entry-obj] link: 372 ms -> /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu7
[entry-obj] tempo totale (runtime): 929 ms
```

La build finale è stata rilinkata da cache, senza ricompilazioni runtime, sempre con `TMPDIR=/dev/shm`; totale 784 ms. Log integrale: `build/qa/armfix-menu7-link.log`.

`TMPDIR=/dev/shm make check`: **PASS**, exit 0. Estratto verbatim:

```text
PASS: ARMv6 Linux syscall exit(23) under QEMU
PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack bridge; ARMv6/VFPv2 attributes verified.
[make check exit=0]
```

Log integrale: `build/qa/armfix-menu7-check.log`.

## Smoke

Comando eseguito da `build/asset-sandbox`:

```sh
TMPDIR=/dev/shm SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 2s 30s qemu-arm-static -cpu arm1176 -L ../sysroot ../starcraft-armv6-menu7
```

Estratto **verbatim** del run finale:

```text
[storm-scode] kernel fuori fingerprint 2x: interpreter (size1/size2/width/flags)=0x0000004A
0x00002BC5
0x00000140
0x00040000
[storm-blt] ROP/flag NON IMPLEMENTATO: storm_313 non-fast/alignment path ROP=0x00CC0020
 flags=0x00000000
[smoke exit=73]
```

FPS precedenti, verbatim: `GPU 24, 59, 58, 58, 29, 20, 20, 19, 20, 20, 20, 20, 20, 18, 20, 20, 20, 20, 20, 34, 13`. Il processo è terminato subito al ramo blit non-fast, non al timeout; il criterio «30 s nel menu» resta fallito. Log completo (testo grezzo): `build/qa/armfix-menu7-smoke.log`. Il run diagnostico precedente è in `build/qa/armfix-menu7-diag-smoke.log`.

## Artefatto

```text
c5f450681ea9396f4551e5097b1fde9b993618cfe6b7e8c1d9657e9079064dcc  build/starcraft-armv6-menu7
76e025d3a0404d1878ea52f55e43d797ec1e6c2a6f825f154d54f8c40c3d0206  runtime/arm/storm_sdraw_arm.c
```

## Decisioni autonome

Scelta conservativa: non approssimare la variante SRCCOPY non-fast/alignment con una copia lineare. IDA mostra che il binario originale la instrada a `storm_334`; il menu7 aveva mantenuto il fail-closed `exit(73)` e il limite è documentato sopra.

## Follow-up menu8 — ROP generico, IDA e superficie fuori mappa

### Oracolo IDA (sola lettura)

Database `build/lift-full/ida/storm.lift.dll.i64`, sessione `a618425d`.

- Decompilazione e disassemblaggio integrali di `storm_313` (`0x15036260..0x150363C6`) confermano che il fast path (`0x15036282..0x1503631F`) è limitato a ROP `0xCC0020`/`0xF00021`, destinazione e larghezza allineate a 4 byte e larghezza `<=0x280`. Il ramo restante (`0x15036344..0x150363BB`) chiama `sub_15035E40` solo per ROP diversi dai due; poi chiama `sub_150015D0(ROP)` e passa il descrittore/parametri a `storm_334` (`0x150363BB`).
- `sub_15035E40` (`0x15035E40..0x15035E82`) **non clippa né inizializza la superficie**. Normalizza solo tre ROP non standard a PATCOPY `0xF00021`: `0x42` imposta il parametro a `0`; `0x000F0001` lo trasforma in `(*a8 == 0)`; `0x00FF0062` lo imposta a `0xFFFFFFFF`. I ROP SRCCOPY e PATCOPY richiesti non passano da questa funzione.
- `sub_150015D0` (`0x150015D0..0x15001687`) seleziona una cache per `BYTE2(ROP)`; in caso di miss chiama `storm_312(ROP,...)`, quindi `storm_331` per generare/cache-are SCode. `storm_331` (`0x15032480..0x150328D2`) valida e compila l’espressione ROP, costruisce kernel per width/allineamento e copia i template/generati nei buffer. Quindi il fallback non è una chiamata libc `memcpy`/`memset`: esegue il kernel generato da `storm_334`. Per i due codici standard la funzione di raster è rispettivamente source-copy (`0xCC0020`) e pattern-copy (`0xF00021`); il kernel gestisce però il layout/stride effettivo.
- Il loop esterno di `storm_313` prepara `delta = pitch - width` (`0x150362B4..0x150362C3`); il corpo pixel avanza i puntatori per i byte trattati e il loop aggiunge il delta. Di conseguenza la riga successiva è a `base + width + delta`, cioè `base + pitch`.

### Implementazione e risultato

Nel solo `runtime/arm/storm_sdraw_arm.c`, per i due ROP verificati ho aggiunto il percorso row-by-row anche quando larghezza/destinazione non sono allineate o `width>0x280`; copia byte per byte per SRCCOPY, pattern 32-bit ripetuto in little-endian per PATCOPY, con controllo delle regioni per ogni riga e stride `width + (int32_t)delta`. ROP diversi continuano a chiamare `fail73`. La progressione delle righe del fast path è stata allineata al loop IDA (larghezza già consumata dal kernel più delta).

Nel primo smoke menu8 (prima della correzione del passo riga) il controllo di memoria fallì: gli argomenti registrati erano `dst=0x204E0010`, `src=0x205A0010`, `width=0x280`, `rows=0x1E0`, pitch totali `0x280`. L’errore derivava dal calcolo del passo riga descritto nel follow-up seguente; non era un limite reale della mappatura.

### Build/check/smoke

Build richiesta completata con `TMPDIR=/dev/shm`, link runtime-only in 759 ms (377 ms link). `TMPDIR=/dev/shm make check`: **PASS**, exit 0. Log integrali verbatim:

- `build/qa/armfix-menu8-link.log`
- `build/qa/armfix-menu8-check.log`
- `build/qa/armfix-menu8-smoke.log`
- `build/qa/armfix-menu8-trace.log` (REASM_TRACE diagnostico, contiene argomenti e mappa rifiutata)

Estratto letterale smoke:

```text
[storm-blt] dst/width/row=0x2052BF10
0x00000280
0x000000F3
[storm-blt] ROP/flag NON IMPLEMENTATO: destination surface non scrivibile ROP=0x00CC0020
 flags=0x00000000
[smoke exit=73]
```

Non si osserva `STOP` o `SIG*`, ma il processo termina con exit 73 prima di FPS/menu: il criterio 30 secondi **non è soddisfatto**.

### SHA-256 menu8

```text
de2d076a500a974e7f1a69b2b3cd5b1fbfa8fab86c61ae383f96a3b12e98bcd8  build/starcraft-armv6-menu8
13142b3e13216659cfaa91bd7af4c02aa79e0be9783cd8f4629692f45ee46d19  runtime/arm/storm_sdraw_arm.c
```

### Decisioni autonome

L’analisi IDA conferma il significato SRCCOPY/PATCOPY e il pitch totale. In questa prima prova l’errore di stride ha prodotto il rifiuto di una riga che doveva restare nella superficie; la correzione e l’esito aggiornato sono documentati sotto.

## Follow-up menu8 — correzione pitch totale e test 30 s

### Correzione

La revisione del chiamante C ha chiarito che `a[4]`/`a[5]` nel wrapper ARM sono già i pitch totali. Il codice IDA (`storm_313` `0x150362B4..0x150362C3` e `0x1503639B..0x150363AA`) sottrae `width` solo per creare il delta da aggiungere dopo che il kernel ha avanzato il puntatore dentro la riga. Nel nostro C i puntatori `dst`/`src` originali restano invece alla base della riga, mentre `d`/`s` sono copie locali: il passo corretto tra le basi di due righe è quindi il pitch totale, senza aggiungere di nuovo `width`.

Applicate le due correzioni in `runtime/arm/storm_sdraw_arm.c`:

```c
int64_t nd=(int64_t)(uint64_t)dst+(int32_t)dst_pitch;
int64_t ns=(int64_t)(uint64_t)src+(int32_t)src_pitch;
```

Conferma IDA: l’originale calcola `delta = a6 - width` / `a7 - width` (`0x150362B4..0x150362C3`, `0x1503639B..0x150363AA`), ma nel percorso C i parametri ricevuti sono i pitch completi. La correzione elimina il doppio conteggio di width.

### Build e verifica

Build `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-menu8`: exit 0, 759 ms complessivi, link 377 ms. `TMPDIR=/dev/shm make check`: **PASS**, exit 0. Log verbatim: `build/qa/armfix-menu8-link.log`, `build/qa/armfix-menu8-check.log`.

### Smoke e limite residuo

Comando richiesto eseguito con timeout 30 s. Il blit non si ferma più al controllo di superficie, i kernel SCode già coperti proseguono e si registrano FPS fino a 187. Non compaiono nuovi opcode SCode o nuovi errori ROP. Allo scadere il processo entra però nel cleanup/exit e il dispatcher stampa lo STOP guest già noto:

```text
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x0041CAC8
  in mappa  = 0x00000001
  eip chiamante = 0x00404F26
[smoke exit=124]
```

Il run non soddisfa quindi il gate stretto richiesto (`exit 137`, zero STOP): `timeout` ha restituito 124 e il log contiene lo STOP al termine del test. Nessun `exit(73)` o `SIG*` riportato. Ultimi FPS verbatim: `GPU 120`, `185`, `185`, `187`, `182`, `185` fps. Log integrale: `build/qa/armfix-menu8-smoke.log`.

### SHA-256 menu8 dopo correzione

```text
e0688bc0cf7e508262afe0e6f7c0cc19773c6fac4df546ca2572e7b992f76fcc  build/starcraft-armv6-menu8
bf7288d0eebd7d2bd6adfafbeace517bf413387c88a465a3542f2805b9e09291  runtime/arm/storm_sdraw_arm.c
```

### Decisioni autonome

Il pitch è stato corretto secondo IDA e il controllo memoria resta attivo. Non ho aggiunto il target `0x0041CAC8`: il precedente audit IDA lo colloca dentro un’istruzione e non è un code head valido. Il gate 30 s è stato eseguito, ma l’esito letterale non è PASS a causa dello STOP/exit 124 durante la chiusura.
