# runtime/common — runtime condiviso da tutte le architetture (ARMv6/ARMv7, x86)

Questi sorgenti **non sono specifici di un'architettura**: `tools/build-runtime-armv7.sh` e
`tools/build-runtime-x86.sh` compilano gli stessi `.c` con target diversi (armv7 hard-float con
sysroot armhf, oppure i686 con `-m32`). Storicamente molti file avevano il suffisso `_arm`
(`bridge_arm.c`, `game_main_arm.c`, `storm_sdraw_arm.c`, `x86_helpers.c`…): il suffisso è stato
rimosso per non far credere che esistano due implementazioni.

Dove sta davvero il codice per-architettura:

- `runtime/arch/arm/*.S` e `runtime/arch/x86/*.S` — seam in assembly (dispatch indiretto, bridge
  COM, azzeramento guest, TLS).
- blocchi `#if defined(__arm__)` / `#if defined(__i386__)` nei sorgenti condivisi: **224 righe su
  20.697** (1,1%), elencate in `reports/REPORT-x86-vs-arm.md`.
- artefatti **generati** per-arch: `guest_layout_arm.h` / `guest_layout_x86.h`,
  `pe_headers_arm.c`, `storm_ordinals[_arm].c`.
- `guest_cpu_helpers.c/.h` (ex `x86_helpers`): nome arch-neutro perché è compilato per ogni host;
  gli helper `X86_*` al suo interno riguardano la CPU **emulata** (il guest i386), non l'host.
- `cpu_llasm.c`, `guest_cpu_helpers.c` restano gli unici punti che nominano `x86`/`fs:` perché
  descrivono la semantica del guest, non l'architettura di build.

Runtime nativo per il guest LLASM (StarCraft + storm.dll): collega gli oggetti LLVM generati da SRW
+ `llasm` ai servizi C/SDL2 già esistenti in `~/dev/sc-reasm/tools/elf/`, senza Wine e senza DLL
Windows. Il percorso ARMv6 (Raspberry Pi OS armhf, ARM1176JZF-S, VFPv2) è quello storico descritto
dagli script `tools/build-runtime-arm.sh`; il port ARMv7/Pinebook e quello i686 usano rispettivamente
`tools/build-runtime-armv7.sh` e `tools/build-runtime-x86.sh`.

Costruzione e verifica end-to-end (sysroot, runtime, guest, smoke test, port degli shim,
**link del gioco completo**, boot sotto qemu):

```sh
tools/build-runtime-arm.sh            # tutti gli stage 1..9 (idempotente)
tools/build-runtime-arm.sh --check    # solo verifica dei prerequisiti
tools/build-runtime-arm.sh --stage 6  # fino allo smoke test ARMv6
tools/build-runtime-arm.sh --stage 8  # link del gioco: build/runtime-arm/starcraft-armv6
tools/build-runtime-arm.sh --stage 9  # boot sotto qemu con gli MPQ collegati (GAME_DIR=...)
SC_BRIDGE_TRACE=1 tools/build-runtime-arm.sh --stage 9   # traccia degli import chiamati
```

Artefatti in `build/sysroot/` (sysroot Raspberry Pi OS, 1,1 GB) e `build/runtime-arm/`
(`libruntime-arm.a`, `smoke.elf`, `sysroot_probe.elf`, `shim-port/`). Report delle verifiche:
`work/runtime-arm/REPORT.md`.

## Contratto ABI (letto dall'emettitore, non dedotto)

| elemento | fonte | valore |
|---|---|---|
| tipo `%_cpu` | `vendor/sr/llasm/llasm.d:5001` | `{ i32 ×12, [8 x double], i64 }` — eax ecx edx ebx esp ebp esi edi eflags st_top st_sw_cond st_cw, st[8], st_result |
| `st[]` | idem | offset 48 (asserito con `_Static_assert` in `cpu_llasm.c`) |
| entry del guest | `llasm.d:5197/5273` | `proc X global G` → `protected fastcc @X(%_cpu*)` + wrapper `protected ccc @G(%_cpu*)` che spinge `__return_procedure` sullo stack del guest |
| ritorno del guest | oggetto compilato | il wrapper fa `b X` (tail), il `ret` finale salta a `__return_procedure` = `bx lr` → ritorno C normale |
| import | `SRW/SR_output.c:SR_write_output_import` | `proc <ProcName> external` → `declare hidden fastcc @<ProcName>(%_cpu*)` |
| nome ordinale | `SRW/SRW_loader.c:MakeOrdinalSymbol` | `ordinal_<hex minuscolo del nome DLL>_<n>`, es. `ordinal_73746f726d2e646c6c_101` |
| helper | `SR/llasm-support/*.llinc` | 137 `func`/`funcv` dichiarati, 137 definiti nei `.c` (insiemi identici) |

## Come è fatto

- **`cpu_llasm.c/.h`** — creazione del `_cpu` nell'arena bassa, stack del guest come memoria a un VA
  reale (4 MiB da `0x02000000`), `cpu_llasm_run` (entry `c_*`) e `cpu_llasm_run_raw` (corpo `proc X
  public`, con sentinella), helper di push/pop/lettura/scrittura, handler `SIGSEGV`/`SIGBUS` che
  stampa fault address + eip/esp/eax/edx del guest (exit 9).
- **`guest_cpu_helpers.c/.h`** — le seam che i 137 helper del vendor chiamano: TEB/PEB emulato
  (`X86_ReadFsDword`/`X86_WriteFsDword`, nessun `%fs`), `X86_ReadMemProcedure`/
  `X86_WriteMemProcedure` con validazione del VA, porte I/O e `int` x86 come stop diagnostico
  (mai uno stub che ritorna), `X86_InterruptFlag`.
- **`game_memory.c/.h`** — mapping ai VA originali (`MAP_FIXED`, niente `MAP_32BIT`), arena
  bump sotto `0x80000000`, registro delle regioni, `arm_va_is_mapped`, selftest del contratto
  `~ptr` (negativo solo se il puntatore è < `0x80000000`).
- **`bridge.c/.h` + `bridge_table.def` + `gen_bridge_stubs.py`** — 17 import Win32 con arità
  stdcall; il generatore emette gli stub ARM (`bridge_stubs.S`) e la tabella nome→indirizzo. Il
  dispatcher C toglie dalla pila del guest il return EIP e `nargs` argomenti, esegue
  l'implementazione e restituisce il VA di ripresa: lo stub fa `bx`, quindi un `call`+`ret` del
  guest non consuma stack host.
- **`gen_storm_ordinals.py`** — tabella ordinale→VA di storm (379 export da
  `sc-reasm/analysis/storm_ordinals.json`): storm è tradotta in-process, non passa dal bridge.
- **`game_main.c`** — main del runtime ARM: registra le regioni delle immagini, ripristina gli
  header PE ai VA delle immagini, crea `_cpu`/TEB e entra nella CRT tradotta (`loc_404C21`) con
  `cpu_llasm_run_raw`.
- **`gen_shims_bridge.py`** — l'arità stdcall dei 291 import serviti dagli shim C viene dai loro
  prototipi (`clang -Xclang -ast-dump`), non da una tabella scritta a mano; emette le voci SHIM,
  la lista di rinomina per `llvm-objcopy` e i chiamanti per arità. Le API senza implementazione
  vanno in `bridge_unk.def` come `UNK(...)` (trampolino dichiarato, log + stop).
- **`patch_guest_pointers.py`** — dopo il link riscrive nel binario i puntatori a codice che SRW ha
  lasciato come byte nei datasegment (`daddr` non riconosciuto), usando i simboli `loc_<VA>`; gli
  indirizzi senza funzione tradotta vengono azzerati e stampati (worklist del lifting).
- **`guest_call.c`** — chiamata *dentro* il guest (window procedure, dialog procedure): spinge
  gli argomenti sullo stack emulato, entra nel guest e torna via `longjmp`. Senza questo una
  finestra non riceve `WM_CREATE`.
- **`elf_support.c/.h`** — log (`reasm_elf_log`…), `GetModuleHandleA`/`LoadLibraryA`/
  `GetProcAddress` (per nome → stub del bridge; `#N` o `ordinal_…` → VA tradotta),
  `loader_init`/`teb_init`/`teb_set_stack`/`teb_envA` sopra il TEB emulato.
- **`patches/0001-i386-only-seams-to-arm.patch`** — unica modifica al codice C di sc-reasm
  (6 hunk su 3 file: `win32_shim.c`, `win32_game.h`, `main_elf.c`). Applicata su una **copia** in
  `build/runtime-arm/shim-port/`, mai sul repository.
- **`include/sdl2-arm/SDL_config-arm.h`** — l'immagine Pi OS non ha i header SDL2 (solo la
  libreria): si usano gli header dell'host, e questo file è il ramo `__arm__` che `SDL_config.h`
  dell'host cerca.

## Due regole che non vanno violate

1. **Nessun puntatore consegnato al guest può stare sopra `0x80000000`.** storm codifica
   "contenitore vuoto" come `~ptr` (vedi `sc-reasm/docs/17-STATO-PORT-NATIVO.md`, trappola 5).
   `arm_alloc_low()` è l'unico allocatore ammesso per memoria visibile al guest; `malloc` della
   libc è solo per strutture interne dell'host.
2. **Mai uno stub che ritorna** per un'API non implementata (regola D19, trappola 6 dello stesso
   documento): `reasm_elf_unimplemented`, `X86_InPortProcedure`, `X86_InterruptProcedure` loggano
   ed escono con 3.
