# Fix ARM: `WaitForMultipleObjects` — esito e prove

## Modifica effettuata
In `build/runtime-arm/shim-port/tools/elf/win32_shim.c`, `WaitForMultipleObjects` ora, sotto `__arm__`, usa direttamente `WaitForSingleObject`; non dereferenzia più l'indirizzo guest `0x15045264` come puntatore host. Il percorso x86 è invariato. Aggiunto inoltre il riconoscimento weak degli handle thread nell'intervallo `[0x72000000, 0x73000000)`, delegando a `arm_guest_thread_join` se disponibile, sia per wait-all sia wait-any. In `runtime/arm/bridge_arm.c` la funzione è dichiarata `int arm_guest_thread_join(uint32_t handle)`; l'oggetto compilato contiene il simbolo weak (`w arm_guest_thread_join`).

## Build e verifica
- Ricompilato solamente `build/runtime-arm/shim-port/obj/win32_shim.o`, target ARMv6 hard-float (`armv6-unknown-linux-gnueabihf`, `-march=armv6zk -mfpu=vfp -mfloat-abi=hard`, sysroot `build/sysroot`, include SDL/toolkit/runtime, `-DSC_ELF_REAL_GDI=1`). Compilazione riuscita con due warning preesistenti su variabili inutilizzate (`absolute`, `g_modname`). Log: `/dev/shm/armfix-compile.log`.
- `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-menu4` completato; link in 365 ms, totale 717 ms. Log verbatim: `build/qa/armfix-link.log`.
- `TMPDIR=/dev/shm make check` **PASS** (ARMv6/VFPv2 attributi verificati). Log verbatim: `build/qa/armfix-check.log`.

## Smoke e punto di arresto
Eseguito il comando richiesto, da `build/asset-sandbox`, con `TMPDIR=/dev/shm`, `SC_SMACK=0`, SDL dummy e timeout 35 s. Il processo **non** va più in SIGSEGV su `0x00056F24`: procede al caricamento e all'attesa dei blocchi GRP. La diagnostica `REASM_TRACE=1` documenta wait da 63 handle evento `0x5E...` e timeout `0x102` durante il polling (comportamento previsto dal loop GRP). Non è stato osservato un handle thread `0x72...` in questi gruppi.

Non raggiunge il menu principale nel tempo disponibile: compare `Could not load "glue\\mainmenu\\Exit.smk"`, poi il dispatcher si arresta sul target non tradotto `0x0041CAC8`, chiamato da `0x00404F26`; uscita smoke 71. È uno STOP di codice guest non tradotto, non una nuova API Win32 o un altro difetto banale in `win32_shim.c`; non ho allargato il perimetro aggiungendo entry guest. Nessuna SIGSEGV nel run da 35 s.

Il run diagnostico breve è stato terminato dal sistema (`exit 137`, log 1,26 MB) durante il tracing ripetuto degli eventi; la traccia contiene comunque le chiamate WFMO e conferma che il fix supera il fault iniziale.

## Estratti verbatim dei log

Compilazione:
```text
build/runtime-arm/shim-port/tools/elf/win32_shim.c:151:36: warning: unused variable 'absolute' [-Wunused-variable]
  151 |     char resolved[512], comp[256], absolute[512], cwd[260];
      |                                    ^~~~~~~~
build/runtime-arm/shim-port/tools/elf/win32_shim.c:336:13: warning: unused variable 'g_modname' [-Wunused-variable]
  336 | static char g_modname[] = "StarCraft.exe";
      |             ^~~~~~~~~
2 warnings generated.
```

Link:
```text
[entry-obj] modalità=runtime  cache=/home/enne2/dev/starcraft-armv6/build/lift-full/entry/.cache
[entry-obj] runtime del port -> /home/enne2/dev/starcraft-armv6/build/lift-full/entry/runtime
[entry-obj] bridge: tabella e stub rigenerati dai sorgenti
[entry-obj] [cache hit] bridge
[entry-obj] runtime: 16 da cache, 0 ricompilati (278 ms)
creato: /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu4
[entry-obj] link: 365 ms -> /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu4
[entry-obj] tempo totale (runtime): 717 ms
```

Check:
```text
PASS: ARMv6 Linux syscall exit(23) under QEMU
PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack bridge; ARMv6/VFPv2 attributes verified.
```

Smoke:
```text
[arm-game] entry: start() del CRT a 0x00404C21
[arm-game] DllMain di storm -> 1
[com] SetDisplayMode 0x028001E00x00000008
[host] finestra creata
[dsound] *** PRIMA CHIAMATA REALE a DirectSoundCreate (chiamante 0x18A4EF0C) ***
[fps] GPU 25 fps, viewport 640x480, finestra 640x480
[win32] MessageBoxA: ERROR / Could not load "glue\mainmenu\Exit.smk"
[arm-dispatch] STOP: salto indiretto a un indirizzo non tradotto
  target    = 0x0041CAC8
  in mappa  = 0x00000001
  eip chiamante = 0x00404F26
  ret (ARM) = 0x18020508
[smoke exit=71]
```

Estratto traccia wait:
```text
[evt] Wait handle=0x5E00007D ms=0x00000030 result=0x00000102
[wfmo] gruppo NON completato n=0x0000003f ret=0x00000102
[evt] Wait handle=0x5E000054 ms=0x00000032 result=0x00000000
[evt] Wait handle=0x5E000055 ms=0x00000032 result=0x00000000
```
Log integrali: `build/qa/armfix-smoke.log` e `build/qa/armfix-trace.log` (la traccia è stata troncata dall'uccisione a 137; sopra estratto letterale).

## SHA-256
```text
f86499965c668068181e61d46bb1de4affac748acfc3f4352c7980f6d99e86f2  build/starcraft-armv6-menu4
817f4a88269dad81af8ca0d6b862f327ebf091d4a1f8e9c3f6f7c6b248991fb5  build/runtime-arm/shim-port/obj/win32_shim.o
ac9b78f9545eeb29a837df07dbeed3efff47df8221038c1e6716abf4d6c38f7c  /dev/shm/armfix-compile.log
4fc97e964af252d5067172cb58a1e275069f15a4570f101bdd6b026ac56a985c  build/qa/armfix-link.log
3d060a05aad75c28ae3d2e60b732bcb173f86db0df3b6085adeaaa630252260e  build/qa/armfix-check.log
91b2726a43894858ce3cbea1db9e085c55e19e5690b1a269d5f09c5b0d4fc482  build/qa/armfix-smoke.log
0892cd82617a091e4d1e40eeb7faee81024305ab5ea7a7c070b9ea9542577341  build/qa/armfix-trace.log
```

## Follow-up menu5 — skip intro senza disattivare libsmacker

### Diagnosi
- Confermato da codice: `smack_elf.c:572` rendeva `active()` falso con `SC_SMACK=0`, quindi `SmackOpen` falliva anche per i video del menu. Non ho modificato `smack_elf.c` né `active()`.
- Il tracing di menu4 mostra `GetFileAttributesA(...\\smk\\blizzard.smk)` e `...\\smk\\starXintr.smk`, seguiti da `SmackOpen` con handle: questi video passano dall'ordinale Storm #454, che apre il percorso tramite `storm_268` e poi consegna l'handle a `SmackOpen`. Quindi il filtro va applicato in #454, non in `SmackOpen`.
- Su StarCraft originale, IDA read-only: `0x00404F24` in `_doexit` chiama un callback indiretto preso dalla catena di exit; il target osservato `0x0041CAC8` è dentro `sub_41CA00` ma cade nel mezzo dell'istruzione iniziata a `0x0041CAC6` (`mov dword ptr xmmword_6D63F4+8, ecx`). Non è un code head valido da aggiungere a `code_entry_points.sci`; è coerente con un callback corrotto/mid-instruction durante il percorso fatale del precedente `MessageBox`, non con una funzione cleanup indipendente. Nessuna modifica SCI. Database StarCraft e Storm chiusi senza annotazioni.

### Fix e build
`runtime/arm/storm_sdraw_arm.c`: il controllo di skip ora richiede `SC_SKIP_INTRO=1` e il primo argomento guest di #454 deve contenere il componente `smk\\` (case-insensitive). Gli asset `glue\\mainmenu\\*.smk` non sono saltati. `SC_SMACK` resta attivo per `libsmacker`; `tools/dispatch-ir.py` è stato ripristinato al contenuto precedente per mantenere compatibilità col wrapper già generato e col link `--runtime-only`.

- Link: `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-menu5`, 1.057 ms totali; 1 solo oggetto runtime ricompilato.
- `TMPDIR=/dev/shm make check`: **PASS**.
- Smoke 30 s: eseguito con `SC_SKIP_INTRO=1`, `SC_FPS_LOG=1`, SDL dummy, timeout richiesto. Nessun `MessageBoxA` e nessun errore su `Exit.smk`; libsmacker apre/decodifica almeno nove entry SMK del menu. FPS registrati inizialmente 26/60/58/58/31 e poi 18–20. **Non supera l'accettazione “30 secondi senza crash”**: termina con exit 73 nel blitter, `aligned SCode variant not yet implemented`, flags `0x00040000`. È un blocco distinto dallo skip e richiede analisi del kernel SCode allineato; non ho inventato un fast path né alterato la semantica di disegno.
- La prova `REASM_TRACE=1` è stata lasciata correre 12 s e uccisa dal timeout (137); tracing rallenta il guest e non arriva alle aperture SMK entro quel limite. La traccia precedente menu4 identifica le intro `smk\\` e le chiama tramite il percorso di apertura video.

### Log verbatim (estratti)

Build:
```text
[entry-obj] runtime: 15 da cache, 1 ricompilati (516 ms)
creato: /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu5
[entry-obj] link: 461 ms -> /home/enne2/dev/starcraft-armv6/build/starcraft-armv6-menu5
[entry-obj] tempo totale (runtime): 1057 ms
```

Check:
```text
PASS: ARMv6 Linux syscall exit(23) under QEMU
PASS: ordinal PE + Wine x86; generated LLASM IR executed under ARM1176 through CPU/guest-stack bridge; ARMv6/VFPv2 attributes verified.
```

Smoke, estratto letterale:
```text
[dsound] CreateSoundBuffer #0x0000000C secondaria, byte 0x00011200, formato 0x56220210
[fps] GPU 0 fps, viewport 640x480, finestra 640x480
[smack] SmackOpen(arg=0x0000000B, flags=0x000FF000) tipo=HANDLE
[smack] header: sig=SMK2 w=0x000000B8 h=0x00000080 frames=0x00000032
[smack] filmato pronto: 0x000000B8x0x00000080 frame=0x00000032
...
[smack] SmackOpen(arg=0x00000012, flags=0x000FF000) tipo=HANDLE
[smack] header: sig=SMK2 w=0x000000CC h=0x00000084 frames=0x00000014
[smack] filmato pronto: 0x000000CCx0x00000084 frame=0x00000014
[storm-blt] ROP/flag NON IMPLEMENTATO: aligned SCode variant not yet implemented ROP=0x00000000
 flags=0x00040000
[smoke exit=73]
```
Log integrali: `build/qa/armfix-menu5-link.log`, `build/qa/armfix-menu5-check.log`, `build/qa/armfix-menu5-smoke.log`, `build/qa/armfix-menu5-trace.log`, `build/qa/armfix-menu4-path-trace.log`.

### SHA-256 menu5
```text
16123a19eff49e77d28199837ee6774682e72a51241fc32f9c90d5515a7e1baa  build/starcraft-armv6-menu5
6a341543d0312942df3f944f97518a7e907bfd6a9c9babe0caf52a46f0a759a0  build/lift-full/entry/runtime/storm_sdraw_arm.o
1c0166e821c079cfdeecd868d9af35ae5fef5be412e5c2dff338e99278c0b289  runtime/arm/storm_sdraw_arm.c
28014a08accc166cdd09463f328d4086ff319bd5c0eee359a9c508e0b28d45a6  build/qa/armfix-menu5-link.log
1bcf70bf2a1f8306a8cc1ffa9bbda9f575992c578b2b7f01101cdfa053396912  build/qa/armfix-menu5-check.log
62e2b001c3932a15432b5ac83c1d03c999f7abb0569cffc9b27f0bd4cc5e6722  build/qa/armfix-menu5-smoke.log
3c3312d86370b8b688cc5ddf49bc481cbb707a4c2ac4bf5ee351eb209b096307  build/qa/armfix-menu5-trace.log
```

## Decisioni autonome
Scelta conservativa: introdotto `SC_SKIP_INTRO=1` separato da `SC_SMACK=0`, così il decoder resta disponibile al menu. Non aggiungo il target `0x0041CAC8` alla mappa perché IDA lo colloca dentro un'istruzione, e non implemento una variante blitter non ancora dimostrata: il prossimo stop è documentato e la verifica menu5 non è dichiarata PASS.

## Follow-up menu6 — ramo allineato `storm_334` (2026-11-21)

### Evidenza IDA read-only
Database usato: `build/lift-full/ida/storm.lift.dll.i64`, sessione `516f2df2`, chiusa senza annotazioni. Decompilazione e disassemblaggio integrali di `storm_334` (`0x15030AB0..0x15030D2A`) e del dispatcher `sub_15030D30`/`loc_15030D50` (`0x15030D30..0x15030D9D`).

Nel ramo `a1[2]&0x40000` (`0x15030AD4..0x15030C4E`): `width=a2[2]` (`[a2+8]`); `align=a2[6]&3` (`[a2+0x18]`); nuova chiave `a1[3]=(4*width)^align` (`0x15030ADD..0x15030B03`). `prefix=(4-align)&3`, limitato a width; `remain=width-prefix`, `q=remain>>2`, `r=remain&3` (`0x15030B06..0x15030B42`). Se `prefix||r`, seleziona `a1[4]=a1[prefix+7]`, patcha l'ultimo rel32 di buffer `a1[5]/a1[6]` verso `a1[q+19]`, quello di `a1[17]/a1[18]` verso `a1[r+13]`, e il buffer aggiuntivo `a1[11]/a1[12]` verso il loop. Se non ci sono prefix/remainder, seleziona `a1[q+19]` e patcha `a1[17]/a1[18]` direttamente al loop. Il loop è `loc_15030D50` se `a1[2]&0x4000000`, altrimenti `sub_15030D30` (`0x15030BC3..0x15030C4A`). Nel caso osservato sono quindi coinvolti i campi `a1[3..18]` e tre code buffer distinti.

A `0x15030CF6..0x15030D2A`, IDA carica `ECX=a2[3]`, `ESI=a2[5]`, `EDI=a2[4]`, `EBX=a1[4]`; salva nello stack locale `[entry, rows, a2, a2[5], a2[4]]`; poi carica `EDI=a2[6]`, `ESI=a2[7]`, `EBP=a2[8]`, `ECX/EDX/EBX=a2[9..11]`, quindi `jmp eax`. `sub_15030D30` decrementa rows, aggiunge i due stride e ricicla; `loc_15030D50` biforca su `rows&1`, e per la riga dispari usa `a2+0x34`/`a2+0x30`. Entrambi terminano all'epilogo comune `0x15030D80..0x15030D9D`, che salva ECX/EDX/EBX in `a2[9..11]`, restituisce 1 e fa `retn 8`.

### Implementazione e prova runtime
`runtime/arm/storm_sdraw_arm.c` ora implementa la scelta degli entry e le patch relative del ramo allineato, inclusi i buffer prefix/core/remainder e la selezione dei due loop. L’interprete fail-closed è stato esteso a riconoscere il terzo buffer SCode. Build `TMPDIR=/dev/shm --runtime-only` completata in 1.028 ms; `TMPDIR=/dev/shm make check` **PASS**.

Smoke eseguito con `SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 2s 30s`. Supera il fail-fast `flags=0x40000`, apre gli SMK dei bottoni e arriva all’interprete; non resta però vivo fino al timeout: exit 73 al primo opcode non coperto, all’indirizzo `0x20178C52`. Bytes emessi dall’interprete:
```text
[storm-scode] unsupported x86 at 0x20178C52
[storm-scode] bytes=0x0000008B
0x0000000F
0x0000008B
0x00000016
0x0000008A
0x000000C1
0x0000008A
0x000000E2
[smoke exit=73]
```
Primi opcode, in little-endian: `8B 0F` (`mov ecx,[edi]`), `8B 16` (`mov edx,[esi]`), `8A C1` (`mov al,cl`), `8A E2` (`mov ah,dl`). Questi restano intenzionalmente fail-closed fino al prossimo aggiornamento dell’interprete. Gli FPS osservati prima del blit erano 25, 59, 58, 59, 57, poi 19–20; non è una prova di loop menu stabile.

Log verbatim integrali: `build/qa/armfix-menu6-link.log`, `build/qa/armfix-menu6-check.log`, `build/qa/armfix-menu6-smoke.log`.

SHA-256:
```text
b8d962c839445af7a3172c6e8adbc8c8afae00dde78431725db080b183da9ea1  build/starcraft-armv6-menu6
3a6ac3fe3774b214f44c017f0704ae0ed8f12ffdbe608ab3e3424dc169e384b0  build/lift-full/entry/runtime/storm_sdraw_arm.o
ca91c12037e5225c78678f6a0dc52c7e9b0cc727105f7e759ff3a8bdeaa7a0e6  runtime/arm/storm_sdraw_arm.c
12bbceb4991609831ec02832b5f7e15d07c8e3476764632c983267c4656bab93  build/qa/armfix-menu6-link.log
7836ad52dc52fb88f02f4b0d065b1e55b444f0559234b3ea4b016952d478ba41  build/qa/armfix-menu6-check.log
10dd83f58455980a0e038de23d621e97d9d7f6c42c30b5914e08aee36fd8af1c  build/qa/armfix-menu6-smoke.log
```
