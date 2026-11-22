# Runtime Linux ARMv6 (Raspberry Pi OS Bookworm armhf) — ambiente, runtime, verifiche

Filone: `work/runtime-arm/` (owner: `runtime/arm/`, `build/sysroot/`, `build/runtime-arm/`,
`tools/build-runtime-arm.sh`).
Data: 2026-11-19. Nel testo i percorsi sono relativi a `/home/enne2/dev/starcraft-armv6` salvo
dove indicato.

## 1. Sintesi

| obiettivo del task | esito |
|---|---|
| 1. sysroot dai file dell'immagine ufficiale, senza sudo | **fatto**: `build/sysroot/` 1,1 GB (glibc 2.36, libSDL2 2.26.5, crt*, `ld-linux-armhf.so.3`, gcc 12 runtime) |
| 1b. programma C+SDL2 ARMv6 linkato contro il sysroot, eseguito sotto qemu | **fatto**: `build/runtime-arm/sysroot_probe.elf`, PASS con `SDL_VIDEODRIVER=dummy` e `offscreen` |
| 2. studio di `llasm-support/`, `SRW*` e degli shim di `sc-reasm/tools/elf` | **fatto**: ABI ricavata da `llasm.d`/`SRW_*.c` + IR emesso (citazioni in §4) |
| 3. `cpu_llasm.c`, `x86_helpers.c`, `game_memory_arm.c`, adattamento shim + tabella bridge | **fatto**: 137/137 helper coperti, 17 import nel bridge, 379 ordinali storm, 14/14 shim compilati per ARM |
| 4. smoke test eseguibile (runtime + SDL2 + helper LLASM sotto `qemu-arm-static -cpu arm1176`) | **fatto**: `build/runtime-arm/smoke.elf`, **28 controlli tutti verdi**, ESITO PASS |
| report con inventario, simboli, dipendenze, stato verifiche | questo documento |
| **5. link del gioco completo** (StarCraft.o + storm.o + runtime + 14 shim) | **fatto**: `build/runtime-arm/starcraft-armv6` (18,6 MB) — 324 import nel bridge (291 SHIM + 33 UNK), vedi §14 |
| **6. boot sotto qemu fino a WinMain / finestra SDL2** | **parziale**: la CRT tradotta parte e chiama **32 import Win32 distinti (95 chiamate)** attraverso gli shim ARM; si ferma in una routine interna della CRT (marcatori `__onexit*` non inizializzati) — diagnosi e worklist in §14.5 |

Unico comando per riprodurre tutto: `tools/build-runtime-arm.sh` (log completo:
`build/runtime-arm/pipeline.log`, exit 0).

## 2. Oracolo e metodo (dichiarazione)

La regola di progetto impone IDA sul binario *oppure* gli artefatti verificati di sc-reasm/docs +
sorgente. Qui la seconda via è quella corretta, e non per comodità: **le interfacce da
ricostruire non sono definite da un binario x86, ma dall'emettitore** (`llasm.d`, `SRW_*.c`) e dal
codice che esso produce. Le ho lette lì e le ho verificate sugli oggetti realmente compilati:

| interfaccia | fonte primaria | verifica |
|---|---|---|
| layout `%_cpu` | `vendor/sr/llasm/llasm.d:5001` | `_Static_assert` su `offsetof` in `runtime/arm/cpu_llasm.c`; IR emesso in `build/runtime-arm/guest/smoke_guest.ll` |
| entry `ccc`/`fastcc`, wrapper `c_*` | `llasm.d:5197, 5273` | disassemblaggio di `c_smoke_guest` (`llvm-objdump -d`) |
| semantica del ritorno del guest | oggetto compilato | `ldr r2,=__return_procedure` + `b` + `bx lr` (commento in `cpu_llasm.c`) |
| nomi degli import | `SRW/SR_output.c:SR_write_output_import` (ramo `OUT_LLASM`) | `declare hidden fastcc @armtest_probe(%_cpu*)` nell'IR |
| nome degli ordinali | `SRW/SRW_loader.c:MakeOrdinalSymbol` | `ordinal_73746f726d2e646c6c_101` in `GetProcAddress` |
| insieme degli helper | `SR/llasm-support/*.llinc` | confronto automatico `.llinc` ↔ `.c`: 137 = 137, insiemi identici (nessun simbolo inventato, nessuno mancante) |
| vincolo `ptr < 0x80000000` | `sc-reasm/docs/17-STATO-PORT-NATIVO.md` trappola 5 (e commento in `tools/elf/main_elf.c`) | `arm_selftest_low()` + controlli a runtime |

Nessuna sessione IDA è stata aperta o toccata (in particolare `aoe1-re/empires_test.exe.i64`, che
resta di un altro lavoro). Nessuna modifica a `sc-reasm` o `m-ht-sr`: gli shim sono **copiati** in
`build/runtime-arm/shim-port/` e lì patchati.

## 3. Inventario file

### 3.1 Codice nuovo (`runtime/arm/`, 1966 righe + 259 dello script)

| file | righe | ruolo |
|---|---|---|
| `cpu_llasm.h` / `cpu_llasm.c` | 96 / 221 | ABI `_cpu`, creazione, stack del guest, `cpu_llasm_run`, `cpu_llasm_run_raw`, accesso memoria, handler crash |
| `x86_helpers.h` / `x86_helpers.c` | 62 / 124 | seam `X86_*`: TEB/PEB emulato, mem procs, porte I/O, `int`, `X86_InterruptFlag` |
| `game_memory_arm.h` / `.c` | 50 / 172 | mapping a VA fissi, arena bassa, validazione VA, selftest `~ptr` |
| `bridge_arm.h` / `.c` | 44 / 195 | tabella import, dispatcher stdcall, ordinali storm |
| `bridge_table.def` | 54 | 17 import Win32 (nome, arità, implementazione) |
| `gen_bridge_stubs.py` | 104 | genera `bridge_stubs.S` + `bridge_symbols.c` dalla `.def` |
| `gen_storm_ordinals.py` | 62 | genera la tabella ordinale→VA |
| `elf_support_arm.h` / `.c` | 46 / 193 | log, `GetModuleHandleA`/`GetProcAddress`, `loader_init`/`teb_*` |
| `include/sdl2-arm/SDL_config-arm.h` | 17 | ramo `__arm__` per gli header SDL2 dell'host |
| `patches/0001-i386-only-seams-to-arm.patch` | 139 | **unica** modifica al C di sc-reasm (6 hunk / 3 file) |
| `tests/smoke_main.c` | 225 | harness dello smoke test (28 asserzioni) |
| `tests/smoke_guest.llasm` | 110 | codice guest generato da `llasm` (div, idiv, x87, string ops, fs, import, sentinella) |
| `tests/sysroot_probe.c` | 52 | programma C+SDL2 nudo per validare il sysroot |
| `guest_call_arm.c` | 90 | call-out verso procedure del guest (window/dialog proc), `jmp_buf` per livello |
| `win32_ext_arm.c` | 68 | `AdjustWindowRectEx` + IMM32 col significato documentato "nessun IME" |
| `x87_crt_arm.c` | 47 | `__CIpow` (pow sullo stack x87 emulato), contratto e incertezza dichiarati |
| `game_main_arm.c` | 175 | main ARM: registrazione regioni, header PE, cpu, entry `loc_404C21` |
| `bridge_dynamic.def` | 17 | 4 API risolte via `GetProcAddress` (`DYNAMIC`) |
| `bridge_unk.def` | 53 | 34 trampolini `UNK` dichiarati (worklist esplicita) |
| `guest_abs_zero.S` | 26 | `__fpmath = 0`: la CRT usa il proprio ramo x87 tradotto |
| `gen_shims_bridge.py` | 232 | arità dai prototipi (AST), def SHIM/UNK, rinomina, caller per arità |
| `patch_guest_pointers.py` | 150 | riscrive nel binario i puntatori a codice rimasti byte |
| `patches/0002-guest-callbacks.patch` | 81 | `SendMessageA`/`CallWindowProcA`/`dlg_elf_wndproc` via call-out |
| `patches/0003-getprocaddress-bridge.patch` | 17 | `GetProcAddress` consulta il bridge (stub, non simbolo C) |
| `README.md` | — | architettura e contratto ABI |

`tools/build-runtime-arm.sh` (259 righe): 7 stage idempotenti (`--check`, `--stage N`,
`--keep-mount`).

### 3.2 Riuso (nessun fork)

- 11 TU di `vendor/sr/SR/llasm-support/*.c` compilate **verbatim** per ARMv6: `llasm_cmps`,
  `_divide`, `_float`, `_fs_mem`, `_inout`, `_int`, `_movmem`, `_movs`, `_pushx`, `_scas`, `_stos`.
- 14 TU di `sc-reasm/tools/elf/*.c` compilate per ARMv6 dopo la patch: `win32_shim`, `win32_ui`,
  `gdi_elf`, `com_ddraw`, `com_dsound`, `host_elf`, `dlg_elf`, `input_elf`, `win32_game`,
  `main_elf`, `smack_elf`, `smack_audio`, `snp_elf`, `storm_ext`.

## 4. Sysroot (stage 1)

Estrazione **senza sudo** con `fuse2fs -o ro,offset=545259520` sulla partizione 2 (`ext4`,
start LBA 1064960, ricavato leggendo la MBR, non da fidisk), poi copia mirata; lo smontaggio è
fatto dallo script (verificato: `build/sysroot-mnt` non esiste più).

| albero | dimensione | file | contenuto rilevante |
|---|---|---|---|
| `usr/lib/arm-linux-gnueabihf` | 938 MB | 4749 | `libc-2.36.so`, `libSDL2-2.0.so.0.2600.5`, `libSDL2_image/mixer/ttf`, X11/Mesa/ALSA |
| `usr/include` | 34 MB | 2865 | glibc + `arm-linux-gnueabihf/{asm,bits,sys}` |
| `usr/lib/gcc/arm-linux-gnueabihf/12` | 81 MB | 67 | `crtbegin.o`, `libgcc.a`, `libgcc_s.so` |
| `usr/lib/linux/uapi` | 380 KB | 74 | **necessario**: `asm/*.h` sono symlink a questo albero (senza, `#include <signal.h>` fallisce) |

Aggiunte non presenti nell'immagine, tutte motivabili:

| cosa | perché |
|---|---|
| `lib -> usr/lib` | merged-usr di bookworm: il linker cerca `/lib/arm-linux-gnueabihf` |
| `usr/lib/ld-linux-armhf.so.3` | path canonico dell'interprete richiesto da `-dynamic-linker` |
| `libSDL2.so -> libSDL2-2.0.so.0.2600.5` | l'immagine ha solo il runtime object (nessun `-dev`), serve il nome `-lSDL2` |
| `build/runtime-arm/sdl2-include/` | header SDL2 dell'host (2.32.74, l'unico set disponibile offline) + `SDL_config-arm.h`: la libreria linkata resta quella del sysroot (2.26.5) |

Dati verificati: `libc` = `Debian GLIBC 2.36-9+rpt2+deb12u13`, `libSDL2` = 2.26.5,
`ld-linux-armhf.so.3` = 167456 byte.

## 5. Toolchain (stage 2-3)

- Compilatore: `clang 22.1.8`, target `armv6kz-unknown-linux-gnueabihf`, `-march=armv6zk
  -mfpu=vfp -mfloat-abi=hard` (clang imposta già `+strict-align`, `-neon`, `+vfp2`).
- IR: `opt`/`llc` del sistema; il data layout del target viene applicato prima di `opt`
  (`tools/compile-arm.sh`), come già fa il test slice.
- Linker: **`ld.lld` 20.0.0** (albero LLVM di ROCm; accettabile anche un lld di wasi-sdk). Motivo
  concreto: non esiste una binutils cross ARM Linux su questa macchina e
  `arm-none-eabi-ld` (GNU Arm 15.2) dichiara **solo** l'emulazione `armelf`, quindi rifiuta
  `-m armelf_linux_eabi`; riprodotto:

  ```
  /usr/bin/ld.bfd: modalità di emulazione non riconosciuta: armelf_linux_eabi
  Emulazioni supportate: elf_x86_64 elf_i386 ...
  ```

  `clang --ld-path=<lld>` è il modo che funziona (`-fuse-ld=` con path assoluto viene ignorato in
  questo build: verificato, ed è il motivo per cui lo script usa `--ld-path`).
- `qemu-arm-static` del sistema, `-cpu arm1176 -L build/sysroot`.

Attributi dell'eseguibile prodotto (`readelf -A`): `Tag_CPU_arch: v6`, `Tag_FP_arch: VFPv2`,
`Tag_ABI_VFP_args: VFP registers`, `Tag_ABI_align_needed: 8-byte` → ARMv6/VFPv2 hard-float, no NEON.

## 6. Simboli coperti

### 6.1 Helper LLASM: 137/137

Confronto automatico tra le dichiarazioni `func`/`funcv` dei `.llinc` e le definizioni `EXTERNC …
CCALL` dei `.c`: **insiemi identici, 137 simboli**, 129 con parametro `CPU`, 8 senza
(`x86_read_fs_dword`, `x86_write_fs_dword`, `x86_in_*_imm`… e i `x86_mov_{reg,mem}_*`).

| file vendor | helper | note ARM |
|---|---|---|
| `llasm_float.c` | 89 | x87 software su `double` (VFPv2 fp64), endianness scelta via `__BYTE_ORDER__` |
| `llasm_inout.c` | 12 | chiamano `X86_InPortProcedure`/`OutPort` → stop diagnostico |
| `llasm_divide.c` | 8 | `div/idiv` 16/32/64 bit, solo C |
| `llasm_cmps.c` | 6 | `repe/repne cmpsb` |
| `llasm_movmem.c` | 6 | `mov reg,mem` con callback `X86_Read/WriteMemProcedure` |
| `llasm_pushx.c` | 5 | `pushad/popad/popfd…`; definisce `X86_InterruptFlag` |
| `llasm_movs.c` | 3 | `rep movsb/sw/sd`, gestione DF e disallineamento |
| `llasm_stos.c` | 3 | `rep stosb/w/d` |
| `llasm_fs_mem.c` | 2 | `fs:[off]` → TEB emulato |
| `llasm_scas.c` | 2 | `repe/repne scasb` |
| `llasm_int.c` | 1 | `x86_int` → `X86_InterruptProcedure` (stop) |

Nell'ELF dello smoke test risultano **137** simboli globali `x86_*`/`x87_*` più una funzione
statica interna (`x86_cmps_rep`): nessun helper indefinito, nessun simbolo inventato. Le sole
aggiunte con prefisso proprio sono `arm_teb_*`/`arm_peb_base` (namespace del runtime, non
`x86_*`, per non occupare quello degli helper).

### 6.2 Seam di piattaforma implementate (`runtime/arm/x86_helpers.c`)

`X86_ReadFsDword`, `X86_WriteFsDword`, `X86_ReadMemProcedure`, `X86_WriteMemProcedure`,
`X86_InPortProcedure`, `X86_OutPortProcedure`, `X86_InterruptProcedure`, `X86_InterruptFlag`.
Tutte senza `%fs`, senza `int $0x80`, senza asm inline.

### 6.3 Bridge degli import

- 17 import Win32 in `bridge_table.def` (GetLastError, SetLastError, GetTickCount, timeGetTime,
  Sleep, GetCurrentThreadId, GetCurrentProcessId, IsDebuggerPresent, ExitProcess, GetModuleHandleA,
  TlsAlloc/Free/GetValue/SetValue, OutputDebugStringA, QueryPerformanceCounter, `armtest_probe`).
- 379 ordinali storm con VA tradotta (da `sc-reasm/analysis/storm_ordinals.json`; il JSON elenca
  379 export dei 815 function slot dichiarati: la tabella copre ciò che il JSON contiene e la
  lookup fallisce in modo esplicito per il resto).
- `GetProcAddress` risolve per nome (stub) e per `#N`/`ordinal_<hexdll>_<n>` (VA), verificato nel
  test.

## 7. ABI del bridge (e un bug vero, trovato dalla verifica)

Regola ricavata dall'IR e dall'oggetto compilato: ogni `call` x86 diventa
`push <EIP di ritorno>` + `tcall` (`musttail`), e ogni `ret` è `POP tmp1` + `tcall tmp1`. Quindi
una funzione importata (1) **non deve tornare** sul chiamante C: deve togliere dalla pila del guest
il return EIP e i propri argomenti stdcall, e (2) deve **saltare** al VA di ripresa, non chiamarlo,
altrimenti ogni andata/ritorno guest→import lascia un frame host (una `GetTickCount` in un ciclo
consumerebbe lo stack).

Implementazione: stub ARM (generati, 5 istruzioni) → `arm_bridge_common` → `arm_bridge_common_c`
(C) → `bx`. Durante la messa a punto lo stub salvava il VA di ripresa in `r5` e poi faceva
`pop {r4,r5,r6,lr}`: il `pop` ripristinava `r5` e il `bx r5` saltava a un valore qualsiasi
(SIGSEGV su indirizzo in `.bss`). Corretto salvando il target in `r2` (caller-saved, non
ripristinato dal `pop`). È l'unico punto dell'intero lavoro in cui la verifica ha smentito il
progetto iniziale, ed è documentato nel commento del generatore.

## 8. Memoria del guest e vincolo `ptr < 0x80000000` (stage 4)

- Immagini ai VA originali: `0x00400000` (exe) e `0x15000000` (storm), con `mmap MAP_FIXED`
  (`MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS`, `0x32`), **senza** `MAP_32BIT` (esiste solo su x86_64) e
  senza `personality`.
- Arena runtime: `0x00500000` + 16 MiB, bump allocator allineato a 8 byte (il `_cpu` contiene
  `double st[8]`, e il suo indirizzo torna al guest come numero: `PTR2REG(&st_result)`).
- Stack del guest: `0x02000000` + fino a 4 MiB; `cpu->esp` è un VA reale, quindi il guest vede il
  proprio stack come memoria normale (nel modello LLASM lo stack del guest *non* è lo stack host).
- `arm_va_is_mapped()` + registro delle regioni: una lettura/scrittura fuori mappa diventa un log
  con l'indirizzo, non un SIGSEGV muto (per gli accessi mediati dagli helper; il codice tradotto
  accede direttamente, e lì serve l'handler di crash).
- Misure sotto qemu (per onestà: qui *tutto* risulta già basso, quindi la trappola non si accende):
  `malloc(64) = 0x41190`, `malloc(1 MiB) = 0x409ec008`, stack host `0x407ff530`. Su hardware reale
  `mmap_base`/stack stanno vicino a `0xBE…`, per cui la regola è **imposta**, non osservata:
  memoria visibile al guest solo da `arm_alloc_low`.
- `arm_selftest_low()` verifica che `(int32_t) ~va < 0` per i VA dell'arena, dello stack e
  dell'immagine.

## 9. Port degli shim C (stage 7)

Approccio: copia in `build/runtime-arm/shim-port/tools/elf/` + `patch -p1` dal file in
`runtime/arm/patches/` (il repository `sc-reasm` resta intatto). Prima della patch **9 dei 14**
file compilano già per ARM; la patch tocca solo 3 file:

| file | hunk | modifica | motivo |
|---|---|---|---|
| `win32_shim.c` | 4 | `int $0x80` 220 (getdents64) → `getdents64()` glibc; `int $0x80` 99 (statfs) → `statfs()` glibc con copia campo per campo; `UI_O_DIRECTORY 0x10000` → `O_DIRECTORY` (su ARM 32 bit è `0x4000`); `%fs:0x34` → `X86_ReadFsDword`/`X86_WriteFsDword` | non esistono su ARM |
| `win32_game.h` | 1 | `#if !defined(__i386__) #error` → ammette `__arm__`; `GAME_STDCALL` vuoto fuori da i386 | l'attributo `stdcall` è ignorato da clang su ARM (117 occorrenze di `-Wignored-attributes` in `win32_ui.c` se non soppresso: la disciplina della pila del *guest* è del bridge, non della convenzione host) |
| `main_elf.c` | 1 | `movl %%esp` → `&sp` (indirizzo di una locale) | nessun registro esp leggibile in C portabile |

Esito: **14/14 compilano per ARMv6** (log per file in `build/runtime-arm/shim-port/*.log`; i
pochi warning residui — `v = v` in `gdi_elf.c`, confronto con stringa letterale nel test di
`dlg_elf.c`, variabili non usate in `win32_shim.c`/`host_elf.c`, dichiarazioni di `fopen` — sono
**preesistenti** nei sorgenti di progetto, non introdotti dal port su ARM), link
`ld.lld -r` riuscito: 1168 simboli definiti (shim + runtime), **130** riferimenti esterni, di cui
solo **7** non forniti da `runtime/arm`:

| simbolo | perché manca | cosa serve |
|---|---|---|
| `g_imports`, `g_imports_n`, `reasm_iat_install`, `reasm_storm_ordinal_va` | generati dal toolkit x86 (`iat_gen.py`, `elf_iat.py`) dalla IAT del PE | rigenerare le tabelle (o usare il bridge ARM, che già risolve per nome/ordinale) |
| `winmain_trap` | è `winmain_trap.asm` (nasm x86) | equivalente ARM del trap su `_WinMain@16` |
| `build_guard_check` | guardia di build in asm x86 (`build_guard.c` + oggetto) | rigenerazione per ARM (il resto è C portabile) |
| `reasm_call_on_stack` | sposta lo **stack host** in zona bassa: nel modello LLASM non ha significato (lo stack del guest è `cpu->esp` in memoria) | **non va implementata**: va tolta dal main ARM |

## 10. Stato delle verifiche

Comando unico: `tools/build-runtime-arm.sh` → exit 0, log `build/runtime-arm/pipeline.log`.

| # | verifica | comando/evidenza | esito |
|---|---|---|---|
| V1 | sysroot estratto senza sudo, smontato | `stage 1` | ok (1,1 GB) |
| V2 | C+SDL2 ARMv6 linkato al sysroot ed eseguito | `sysroot_probe.elf` + `qemu-arm-static -cpu arm1176 -L build/sysroot` | **PASS** (SDL 2.26.5, surface 64x64 bpp=32) |
| V3 | attributi ARMv6/VFPv2 hard-float, no NEON | `readelf -A`, `#error` in `sysroot_probe.c` su `__ARM_PCS_VFP`/`__ARM_NEON` | ok |
| V4 | helper LLASM: nessun simbolo mancante | confronto `.llinc`↔`.c` = 137/137; link dello smoke senza `x86_*`/`x87_*` indefiniti | ok |
| V5 | `div` 64 bit: `2^32 / 7` | guest `DIV_64` | quoziente `0x24924924`, resto 4 |
| V6 | `idiv` 32 bit: `100 / 7` | guest `IDIV_32` | quoziente 14, resto 2 |
| V7 | x87 software: `1.5 * 2.0` | guest `fld/fmul/fstp_double` | double = 3.0 |
| V8 | string ops | `REP_MOVSD` 16 byte, `REPE_CMPSB` 8 byte | copia identica, ecx=0 |
| V9 | `fs:[0x34]` senza `%fs` | guest scrive/legge, poi `GetLastError` dal bridge | 0x1234 in entrambi |
| V10 | bridge stdcall: argomento, risultato, pulizia pila | `armtest_probe(21) = 43`; esp del guest tornato al valore iniziale; canary `0xCCCCCCCC` intatto | ok |
| V11 | sentinella per proc senza wrapper | `cpu_llasm_run_raw(raw_guest)` | ritorno via sentinella, eax `0x5A5A5A5A` |
| V12 | `GetModuleHandleA`/`GetProcAddress` | `NULL→0x400000`, `storm.dll→0x15000000`, per nome → stub, `#101` e `ordinal_7374…_101` → VA tradotta | ok |
| V13 | ordinali storm | tabella generata da `storm_ordinals.json` | 379 voci, 101 risolto |
| V14 | SDL2 dal sysroot dentro lo smoke test | `SDL_Init`, window surface, `SDL_MapRGB` | driver `dummy` **e** `offscreen` PASS |
| V15 | handler di crash | prova dedicata: fault a 0 + `eip/esp/eax` del guest | stampa `fault 0x0 … eax 0xDEADBEEF`, exit 9 |
| V16 | shim C portati | 14/14 compilano, `ld.lld -r` | ok, gap = 7 simboli del loader x86 |
| V17 | nessuna modifica a sc-reasm/m-ht-sr | diff applicata su copia in `build/`; `sc-reasm` intatto | ok |
| V18 | link del gioco completo | `--stage 8` → `starcraft-armv6` (18,6 MB), 324 stub | ok |
| V19 | ogni import riferito dal guest ha una risoluzione | 517 indefiniti → 135 dal linker (storm), 52 helper, 2 runtime/libc, 291 SHIM, 4 DYNAMIC, 33 UNK dichiarati, 0 dimenticati | ok |
| V20 | arità degli import non inventata | 291 arità lette dai prototipi C (`-ast-dump`), generazione che si ferma su un simbolo non classificato | ok |
| V21 | il binario parte e la CRT tradotta esegue | 32 import distinti, 95 chiamate servite prima dello stop (§14.5) | ok (parziale) |
| V22 | puntatori a codice nei dati | 1532 riscritti verso il codice ARM, 3641 azzerati e stampati come worklist | ok (con debito dichiarato) |
| V23 | il guest arriva a `_WinMain@16` / finestra SDL2 | **no**: stop nella CRT (§14.5-14.6) | **aperto** |

Controlli dell'harness (`smoke_main.c`): 28 asserzioni, tutte `ok`, `[smoke] ESITO: PASS`
(exit 0). Tipi ELF: `smoke.elf` = 228 KB, `NEEDED` = `libSDL2-2.0.so.0`, `libgcc_s.so.1`,
`libc.so.6`.

## 11. Limiti e lavoro residuo (dichiarati, non aggirati)

1. **Il main del gioco è cablato** (§14): il binario collega e parte. Restano due blocchi,
   entrambi diagnosticati in §14.5-14.6: quattro inizializzatori della CRT non tradotti
   (`0x4060C1`, `0x406AA1`, `0x40D8C6`, `0x4F8329` — buco del lifting) e l'allocatore diviso fra
   `HeapAlloc` (glibc) e `VirtualAlloc` (arena VM), che fa abortire `free()`. Lo *smoke test*
   (stage 3-7) resta la verifica indipendente di tutti i pezzi.
2. **x87 in `double`**: le 89 funzioni usano `double` (64 bit) e non `long double` (80 bit x87).
   È la scelta dell'implementazione di riferimento di SR; differenze di arrotondamento possibili in
   catene lunghe. `st_cw`/`st_sw_cond` sono gestiti ma non sollevano eccezioni.
3. **`div`/`idiv`**: nessuna eccezione su divisione per zero o quoziente troppo grande (come il
   riferimento); un guest che ci conta vedrebbe un risultato invece di un trap.
4. **Porte I/O e `int` x86**: stop diagnostico (`exit 3`), mai un valore finto.
5. **TEB a singolo thread**: il TEB emulato è una pagina globale. `CreateThread` di `win32_shim.c`
   compila ma un TEB per thread non è implementato (su ARM `teb_init` per thread è banale ma va
   progettato insieme al bridge, che oggi è a thread singolo).
6. **SDL fbdev non provato**: sotto qemu non c'è `/dev/fb0`. Verificati `dummy` e `offscreen`.
   Da rifare sulla Pi vera (o nell'immagine avviata con `tools/qemu/run.sh`).
7. **Header SDL2 2.32 compilati contro libreria 2.26**: compatibile per le API usate (tutte
   ≤ 2.0.16), ma un uso futuro di campi introdotti dopo la 2.26 andrebbe rivisto. L'alternativa
   (header 2.26) richiederebbe rete, vietata in questo task.

## 12. Decisioni autonome

1. **Riuso delle 11 TU di `llasm-support` invece di riscrivere 3400 righe di x87/div/string ops.**
   Il task chiede "implementa div/idiv, string ops, x87 software": sono esattamente i file che SR
   distribuisce per questo scopo, sono C neutro rispetto all'architettura, e riscriverli sarebbe un
   rischio di regressione senza contropartita. Il lavoro *nuovo* è ciò che l'emettitore si aspetta
   attorno a loro (CPU, stack, TEB, memoria, bridge, ABI): §6.2-§7. Verificato che 137/137 simboli
   tornino e che nessuno sia indefinito al link.
2. **`ld.lld` invece di una binutils cross**: non c'è una cross ARM Linux installata e non posso
   installarla; l'unica alternativa (arm-none-eabi-ld) non supporta `armelf_linux_eabi` (errore
   riprodotto in §5). Nessuna installazione di sistema è stata fatta.
3. **Header SDL2 dell'host + `SDL_config-arm.h`**: l'immagine ha solo la libreria. Ho scelto
   2.32 (host) invece di inventare header, e ho documentato lo scarto di versione (§11.7).
4. **Copia+patch degli shim** invece di modificarli nel repository di progetto: mantiene
   `sc-reasm` intatto e rende la patch verificabile e rigettabile (6 hunk leggibili).
5. **`cpu_llasm_run` non spinge la sentinella** (il wrapper `ccc` la installa da sé): scoperto
   leggendo l'oggetto compilato; la sentinella resta disponibile con `cpu_llasm_run_raw` per i
   corpi senza wrapper, ed è testata (V11).
6. **Aggiunta di `elf_support_arm.c`** (non esplicitamente richiesta): senza
   `reasm_elf_log`/`GetProcAddress`/`teb_*` nessuno shim si collega e il "port" resterebbe una
   compilazione senza link. Chiude 5 dei 12 simboli che mancavano.
7. **Nomi dei simboli del bridge = `ProcName` dell'emettitore** (non `_adr_<nome>`, che esiste solo
   nel percorso PE/x86): verificato in `SR_output.c` e nell'IR.
8. **Arità degli import dai prototipi C**, non da una tabella scritta a mano né da una stima: è
   l'unica fonte che non può divergere dalla funzione che verrà chiamata, e il generatore si ferma
   se un simbolo non è classificabile (così un import dimenticato non passa in silenzio).
9. **`__fpmath` = 0 invece del dispatcher di msvcrt**: la mappa degli opcode non è disponibile e
   ricostruirla sarebbe inventare una tabella. Con 0 la CRT usa il proprio ramo x87 tradotto
   (misurato: arriva molto più avanti). L'alternativa (voce HAND che registra l'opcode) è stata
   usata per la misura ed è documentata, non spedita.
10. **Puntatori a codice: patch nel file, non a runtime**: i segmenti `.tdata` delle immagini sono
    in segmenti read-only (come nell'originale) e non vanno resi scrivibili; la patch dopo il link
    è una passata sola e non tocca il comportamento del guest a runtime.
11. **Call-out generico per i callback del guest** (`arm_guest_calln`, `arm_guest_call4`) invece di
    far finta che un VA del guest sia chiamabile da C: senza questo una finestra non può nemmeno
    ricevere `WM_CREATE`.
12. **`reasm_call_on_stack` non implementata** (vedi §11 tabella §9): implementarla su ARM
   significherebbe fingere un comportamento che il modello LLASM non usa.

## 13. Riproduzione

```sh
cd /home/enne2/dev/starcraft-armv6
tools/build-runtime-arm.sh --check    # prerequisiti (clang, lld, qemu, fuse2fs, llasm)
tools/build-runtime-arm.sh            # stage 1..7, exit 0 = tutto verde
# singoli pezzi
tools/build-runtime-arm.sh --stage 3  # solo sysroot + probe C/SDL2
tools/build-runtime-arm.sh --stage 6  # fino allo smoke test
tools/build-runtime-arm.sh --stage 8  # link del gioco completo (starcraft-armv6)
tools/build-runtime-arm.sh --stage 9  # link + boot sotto qemu con gli MPQ collegati
SC_BRIDGE_TRACE=1 ./tools/build-runtime-arm.sh --stage 9   # traccia degli import chiamati
build/pipeline/bin/llasm -I vendor/sr/SR/llasm-support -o /tmp/g.ll runtime/arm/tests/smoke_guest.llasm
SDL_VIDEODRIVER=dummy qemu-arm-static -cpu arm1176 -L build/sysroot build/runtime-arm/smoke.elf
```

Variabili utili: `LLD=`, `QEMU_ARM=`, `IMAGE=`, `SYSROOT=`, `WORK=`, `SC_REASM=`, `TOOLKIT=`.

## 14. Collegamento del gioco completo e boot (stage 8-9)

### 14.1 Cosa produce

`tools/build-runtime-arm.sh --stage 8` costruisce `build/runtime-arm/starcraft-armv6` (18,6 MB,
ELF32 ARM EABI5, dinamico contro il sysroot) da:

| pezzo | provenienza | note |
|---|---|---|
| `StarCraft.data.o` (7,7 MB codice, 1,9 MB dati) | `build/lift-full/pipeline/StarCraft.optimized.ll` | ricompilato con `llc --data-sections` (una sezione per datasegment) |
| `storm.data.o` (2,4 MB codice) | `build/lift-full/pipeline/storm.optimized.ll` | idem |
| `libruntime-arm.a` (stessi TU del §3) | `runtime/arm/` + `vendor/sr/SR/llasm-support` | tabella import *diversa* da quella dello smoke test |
| 13 shim C di `sc-reasm/tools/elf` | copia in `build/.../game/shim` + patch 0001/0002/0003 | `main_elf.c` e `storm_ext.c` esclusi (vedi §14.3) |
| `game_main_arm.c` | `runtime/arm/` | main ARM: mapping, PE header, cpu, entry del CRT |
| `pe_headers_arm.c`, `guest_layout_arm.h`, `guest_layout.ld` | generati | layout ai VA originali + header PE |

### 14.2 Gli import: dai 511 riferimenti ai 324 stub

`llvm-nm -u` sui due oggetti dà **517 simboli indefiniti**. Il generatore
(`gen_shims_bridge.py`, `gen_bridge_stubs.py`) li classifica così (nessuno resta fuori):

| classe | n | trattamento |
|---|---|---|
| definiti dall'altro oggetto (ordinali storm `ordinal_73746f726d2e646c6c_*`) | 135 | nessuno: risolti dal linker, sono la storm tradotta ai suoi VA |
| helper LLASM (`x86_*`, `x87_*`) | 52 | forniti dal runtime (le 137 funzioni del §6.1) |
| già definiti dal runtime/libc | 2 | `__CIpow` (`x87_crt_arm.c`), `memset` (glibc) |
| implementati dagli shim C | **291** | stub con ABI del guest + chiamante per arità |
| API risolte a runtime (`LoadLibrary`+`GetProcAddress`) | 4 | `DYNAMIC(...)`: DirectDrawCreate, DirectDrawCreateClipper, DirectSoundCreate, DirectSoundCreate8 |
| senza implementazione | **33** | `UNK(...)` = trampolino dichiarato che logga ed esce (D19) |

135 + 52 + 2 + 291 + 4 + 33 = 517: la somma chiude, quindi non c'è nessun import che il
generatore abbia "perso" (è la proprietà che rende utile la classificazione).

**L'arità non è inventata**: viene dai prototipi C degli shim, letti con
`clang -Xclang -ast-dump` (la stessa dichiarazione che il compilatore usa per costruire la
funzione, quindi non possono divergere). Distribuzione sulle 291 voci: 0 argomenti ×22, 1 ×86,
2 ×71, 3 ×35, 4 ×33, 5 ×15, 6 ×13, 7 ×6, 8 ×4, 9/10/11 ×1, 12 ×2, 14 ×1 — più `wsprintfA`
(variadico, cdecl: il guest ripulisce da sé, il chiamante passa 2 argomenti + una finestra di 8
parole in lettura). Un simbolo senza prototipo e senza `UNK` **ferma la generazione** (nessun
import dimenticato in silenzio).

Il ponte verso il C: `objcopy --redefine-syms` (in realtà `llvm-objcopy`: la binutils di sistema
non conosce ARM) rinomina `<nome>` → `<nome>__c` negli oggetti degli shim, liberando il nome
dell'import per lo stub con ABI del guest. La tabella C chiama poi il simbolo rinominato attraverso
un *caller per arità* generato in C (`unsigned call3(void *fn, _cpu *cpu)`), così il compilatore
produce da sé il passaggio registri/stack AAPCS: nessun assembly scritto a mano e arità > 4
gratis.

### 14.3 Simboli duplicati, e come sono stati risolti (senza toccare i repo)

| duplicato | causa | soluzione |
|---|---|---|
| nessuno tra `StarCraft.o` e `storm.o` | verificato: intersezione dei simboli definiti = 0 | — |
| `GetTickCount`, `SetLastError`, `TlsAlloc`, ... | le stesse API erano sia nella tabella a mano (`bridge_table.def`, 17 voci per lo smoke test) sia negli shim | precedenza **SHIM**: la tabella a mano è usata solo dove non esiste uno shim (`--only-needed` scarta le voci non referenziate) |
| `IsDebuggerPresent`, `AdjustWindowRectEx`... | voce a mano non referenziata dal guest contro definizione C omonima | `--only-needed`: la voce a mano non entra nel binario |
| `main` | `main_elf.c` è il main del modello i386 | escluso dal runtime ARM (il main è `game_main_arm.c`) |
| `g_imports`, `reasm_storm_ordinal_va` | `storm_ext.c` è il livello override del modello i386 (tabella IAT generata) | escluso |
| `GetStockObject` | `gdi_elf.c` contro `win32_ui.c` (già noto al progetto) | `-DSC_ELF_REAL_GDI=1` come fa `tools/elf/build.sh` |

### 14.4 Tre problemi veri, trovati perché il guest va in crash

1. **Chiamate *dentro* il guest** (`patches/0002-guest-callbacks.patch`): `win32_ui.c` chiamava la
   window procedure come puntatore a funzione nativo (`w->wndproc(h,m,w,l)`), corretto su i386 dove
   il guest *è* codice nativo al suo VA. In LLASM una procedura del guest prende solo `%_cpu` e
   legge gli argomenti dal proprio stack: serve un *call-out* (`runtime/arm/guest_call_arm.c`) che
   spinge gli argomenti sullo stack emulato, entra nel guest e torna via `longjmp` (uno `jmp_buf`
   per livello, così i dialoghi modali — guest → shim → guest — funzionano).
2. **`GetProcAddress`** (`patches/0003-getprocaddress-bridge.patch`): restituiva il simbolo C; sul
   guest quel puntatore viene chiamato con l'ABI del guest. Ora il bridge è consultato per primo e
   restituisce lo *stub*; le 4 API risolte a runtime sono nel bridge per questo.
3. **Puntatori a codice scritti come byte nei dati** (`patch_guest_pointers.py`): SRW emette come
   rilocazione i puntatori che riconosce (`daddr`), ma le tabelle che la CRT si costruisce da sola
   restano byte con l'indirizzo x86. Sul runtime i386 non si notava (codice ai VA originali); qui il
   codice ARM vive altrove. Il fix è nel *file*, dopo il link, guidato dai simboli `loc_<VA>`:
   **1532 word riscritte** verso il codice ARM, **3641 azzerate** perché puntano a codice che il
   lifting non ha coperto (worklist in §14.5). Perché i simboli esistano, l'IR viene post-processato
   (`private` → `hidden` per tutte le `loc_*`): con i simboli locali il linker ne teneva solo 2 e la
   mappa VA→ARM era vuota.

Layout risultante (sezioni a VA esplicito, verificato con `readelf -SW`): exe `.data` a 0x50C000,
`.rdata` a 0x4FE5D0, datasegment `.text` a 0x406C0C, codice a 0x08000000; storm `.data`
0x15059000, `.rdata` 0x150453C4, `.text`-data 0x150062D8, codice 0x10000000. I dati stanno ai VA
originali perché gli shim leggono alcuni globali del gioco a indirizzo assoluto (0x6CF4A8,
0x6CDDC0..C8, 0x5968B0/B4) e perché i puntatori rimasti byte restano coerenti.

### 14.5 Il boot: dove arriva e dove si ferma

`--stage 9` collega gli asset (`GAME_DIR=~/dev/final-starcraft/Starcraft`, 8 file, symlink) e lancia
`qemu-arm-static -cpu arm1176 -L build/sysroot starcraft-armv6`. Con `SC_BRIDGE_TRACE=1` si vede il
percorso della CRT tradotta:

```
[arm-game] header PE exe a 0x00400000        <- header PE ripristinati ai VA delle immagini
[arm-game] header PE storm a 0x15000000
[arm-game] bridge: 324 import, 33 UNK dichiarati
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000   <- lo shim legge la directory risorse
GetVersionExA, GetModuleHandleA, HeapCreate/Alloc, InitializeCriticalSection,
GetProcAddress x4, TlsAlloc/Get/Set, VirtualAlloc, GetStartupInfoA, GetStdHandle,
GetFileType, SetHandleCount, GetCommandLineA, GetEnvironmentStringsW, WideCharToMultiByte,
GetACP, GetCPInfo, GetStringTypeW, MultiByteToWideChar, LCMapStringA/W, GetLocaleInfoA,
GetLastError, SetLastError, EnterCriticalSection, LeaveCriticalSection, GetModuleFileNameA
...
[arm-cpu] SIGSEGV  fault pc 0x0803282C  fault address 0x00000000
```

**32 import distinti, 95 chiamate**, tutte servite dagli shim ARM con l'ABI del guest: la catena
CRT → shim → valore di ritorno funziona, comprese le sezioni critiche reali, il TEB emulato
(`GetLastError`/`Tls*`) e la memoria virtuale (`VirtualAlloc` → arena 0x20000000).

Il crash è **dentro la CRT tradotta**, non nel runtime: `0x0803282C` = `loc_40604D+0x58` (VA
originale 0x40604D), che esegue `store edi, [__onexitbegin]` con `__onexitbegin` (0x6DD684) = NULL.
Subito prima la CRT chiama `HeapSize(0x10001, 0, NULL)` (tracciato: `= 0xFFFFFFFF`), cioè sta
maneggiando un blocco nullo. Due esperimenti, entrambi registrati per non lasciare il dubbio:

| esperimento | esito |
|---|---|
| `__fpmath` = 0 (simbolo assoluto, `guest_abs_zero.S`) invece del dispatcher di msvcrt | la CRT prende il proprio ramo `loc_404E60` e **prosegue** (è lo stato con cui si arriva a 32 import). Con la voce HAND che registra l'opcode si misura: `__fpmath(opcode=0x1)` chiamato durante l'init, risultato scartato |
| `SC_ATEXIT_FIX=1`: marcatori `__onexitbegin/end` puntati a un buffer dell'arena | la CRT supera il punto e arriva a `free(): invalid pointer` (glibc abort): emerge il problema successivo, l'allocatore diviso fra `HeapAlloc`→glibc e `VirtualAlloc`→arena VM |

**Worklist del lifting** (`patch_guest_pointers.py`, i 16 indirizzi più frequenti su 3641 word):
`0x00425180(90) 0x00428E60(87) 0x00429450(55) 0x00423310(55) 0x004233F0(44) 0x00424440(40)
0x004294E0(34) 0x00424140(31) 0x00423370(31) 0x150453F0(31) 0x00428900(27) 0x004234B0(27)
0x00429500(26) 0x00423350(26) 0x00424520(22) 0x00426EE0(22)`.

In particolare la tabella a 0x50C4C0 che la CRT percorre (`_initterm`) contiene quattro indirizzi
**non tradotti**: `0x4060C1`, `0x406AA1`, `0x40D8C6`, `0x4F8329` (tutti «uno dopo un `ret`», cioè
entry che IDA non ha riconosciuto come funzioni: il range 0x4060C0..0x4060E8 non ha un `proc`).
Questi quattro inizializzatori non possono essere eseguiti da nessuno dei due runtime (x86 o ARM):
è un buco del lifting, non del runtime, e va chiuso con un `.sci` «interpret as code» sul range.

### 14.6 Cosa manca per arrivare a `_WinMain@16` (in ordine di dipendenza)

1. lifting di `0x4060C0..0x4060E8` (e degli altri indirizzi della worklist) — lavoro del filone
   lifting, nessuna modifica al runtime;
2. allineamento dell'allocatore: la CRT usa `HeapAlloc` e `VirtualAlloc` in modo intercambiabile;
   con `HeapAlloc`→glibc e `VirtualAlloc`→arena VM un `free` finisce nell'allocatore sbagliato.
   Serve una scelta unica (probabilmente: tutto l'heap del guest nell'arena VM bassa, che è anche
   ciò che garantisce `ptr < 0x80000000` per gli oggetti del gioco);
3. `__fpmath`: se il ramo interno non basta, serve la mappa degli opcode di msvcrt (non
   disponibile): si implementa caso per caso, misurando l'opcode come fatto qui.

## Appendice A — log dello smoke test (estratto da `build/runtime-arm/pipeline.log`)

```
[runtime-arm] stage 3: programma C+SDL2 nudo, per validare il sysroot da solo
  Tag_CPU_arch: v6
  Tag_FP_arch: VFPv2
  Tag_ABI_VFP_args: VFP registers
[probe] ARMv6 hard-float C+SDL2 sysroot test
[probe] sizeof(void*)=4 float-abi=hard
[probe] SDL 2.26.5 driver=dummy
[probe] surface 64x64 bpp=32 ok
[probe] ESITO: PASS
[runtime-arm] stage 6: link ed esecuzione dello smoke test ARMv6
[smoke] runtime ARMv6: arena, TEB, guest LLASM helpers, bridge, SDL2
  ok   arm_alloc_low restituisce memoria sotto 0x80000000
  ok   cpu in zona bassa
  ok   TEB emulato in zona bassa
  ok   eflags IF impostato
  ok   x87 cw di default
  ok   esp del guest ripristinato dal ret
  ok   div 2^32 / 7 -> quoziente
  ok   div 2^32 / 7 -> resto
  ok   idiv 100 / 7 -> quoziente
  ok   idiv 100 / 7 -> resto
  ok   x87 1.5 * 2.0 == 3.0
  ok   rep movsd copia 16 byte
  ok   repe cmpsb su blocchi uguali -> ecx 0
  ok   fs:[0x34] round-trip
  ok   import stdcall 21*2+1
  ok   GetLastError via bridge legge il TEB
  ok   canary dello stack guest intatto
  ok   raw: ritorno via sentinella
  ok   raw: eax scritto dal guest
[smoke] bridge: 17 import Win32 cablati; ordinali storm nella tabella generata:
  ok   ordinale 101 di storm risolto (VA tradotta)
  ok   GetModuleHandleA(NULL) = 0x400000
  ok   GetModuleHandleA("storm.dll") = 0x15000000
  ok   GetProcAddress per nome -> stub del bridge
  ok   GetProcAddress("#101") -> VA tradotta di storm
  ok   GetProcAddress ordinal_73746f726d2e646c6c_101
  ok   handler di crash installato (SIGSEGV/SIGBUS -> eip/esp/eax guest)
[smoke] SDL 2.26 driver=dummy
  ok   SDL versione >= 2.0.12
  ok   superficie con pixel scrivibili
  ok   SDL_MapRGB coerente
[smoke] ESITO: PASS
[runtime-arm] stage 6: secondo giro con SDL_VIDEODRIVER=offscreen
[runtime-arm]     offscreen: PASS
```

Prova dell'handler di crash (programma separato, fault a indirizzo 0):

```
[arm-cpu] SIGSEGV
[arm-cpu] fault address 0x00000000
[arm-cpu] guest eip 0x00401234  esp 0x00400800  eax 0xDEADBEEF  edx 0x00000000
```

Estrazione del sysroot misurata da zero (sysroot in `/tmp`, poi rimosso): 21 s, 1,1 GB,
`ld-linux-armhf.so.3`, `crt1.o`, `libgcc.a`, `libSDL2-2.0.so.0` e gli header `asm/*` presenti; il
probe C+SDL2 ricompilato contro quel sysroot passa (stage 3 con `SYSROOT=/tmp/tsys`).

