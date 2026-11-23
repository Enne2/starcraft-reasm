# REPORT — divergenza x86 vs ARM (misurata sul repo)

Data: 2026-11-23. Metodo: conteggio delle righe C/H in `runtime/` che stanno dentro blocchi
`#if defined(__arm__)` / `#elif`/`#else` (solo ARM o solo i686), più inventario dell'assembly
per-arch e dei flag di build. Nessuna stima a impressione.

## Quadro

Non esistono due implementazioni. Il guest (StarCraft.exe + storm.dll) è liftato **una volta** in
LLVM IR (`build/lift-full/StarCraft.ll`, `storm.ll`) e poi ricompilato per il target scelto:
`tools/build-entry-arm.sh` → `llc -mtriple=armv7a-...`, `tools/build-entry-x86.sh` → i686.
Gli stessi `.c` di `runtime/common` e `runtime/shim-port/elf` sono compilati in entrambe le build
(i nomi con `_arm`, ora rimossi, erano storici).

## Numeri

| metrica | valore |
|---|---|
| righe C/H in `runtime/` | 20.697 |
| righe identiche fra le due architetture | 20.473 (98,9 %) |
| righe solo ARM | 158 |
| righe solo i686 | 66 |
| **righe divergenti** | **224 (1,08 %)** |
| file C/H toccati da condizionali | 19 (12 con righe esclusive) |
| righe shim C (`runtime/shim-port/elf`) | 14.980, condivise |
| assembly per-arch | ARM 257 righe (4 file) / x86 208 righe (3 file) |

## Differenze di codice, per file

| file | ARM / x86 (righe) | cosa cambia |
|---|---|---|
| `shim-port/elf/win32_shim.c` | 63 / 27 | `getdents64` via wrapper glibc e `O_DIRECTORY` da `<fcntl.h>` (0x4000 su armhf); su i686 gate `int $0x80` e letterale 0x10000. TEB emulato, default `WaitForSingleObject` |
| `shim-port/elf/host_elf.c` | 71 / 1 | canvas su `mmap` allineato (serve al watchdog `mprotect`) con `munmap` e handler `SIGSEGV` che legge `arm_pc`; su x86 `free()` |
| `common/indirect_dispatch.c` | 0 / 18 | validazione dei target di salto indiretto contro `reasm_exe_code_lo/hi` (solo i686) |
| `common/guest_call.c` | 4 / 5 | chiamata al codice liftato: i686 `x86_call_guest_fast` (fastcc, `%_cpu` in ECX), ARM chiamata C diretta |
| `common/game_main.c`, `common/cpu_llasm.c` | 8 / 7 | registri da `ucontext` nei fault: `REG_EIP/ESP/EBP` vs `arm_pc/arm_lr/arm_sp/arm_fp` (diagnostica) |
| `shim-port/elf/win32_game.c` | 2 / 1 | `GAME_O_DIRECTORY`: `O_DIRECTORY` su ARM, `0x10000` su i686 |
| 6 file minori (`main_elf.c`, `bridge.c`, header COM/GDI) | 1 / 1 ciascuno | include/feature |

## Assembly

| file | righe |
|---|---|
| `runtime/arch/arm/indirect_dispatch.S` | 164 |
| `runtime/arch/x86/indirect_dispatch_x86.S` | 135 |
| `runtime/arch/arm/com_bridge.S` / `arm/tls.S` / `arm/guest_abs_zero.S` | 53 / 20 / 20 |
| `runtime/arch/x86/com_bridge_x86.S` / `x86/guest_abs_zero_x86.S` | 70 / 3 |

## Build / link

| | ARM | x86 |
|---|---|---|
| triple | `armv7a-unknown-linux-gnueabihf` | `i686-unknown-linux-gnu -m32 -fno-pie` |
| CPU | `-march=armv7-a -mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm` | default i686 |
| sysroot | `build/sysroot` (armhf) | host |
| entry/link | `build-entry-arm.sh` + `link-arm.sh` | `build-entry-x86.sh` + `link-x86.sh` |
| SDL2 | overlay header `runtime/common/include/sdl2-arm/SDL_config-arm.h` | header di sistema |

## Cosa NON differisce

Lift del guest, mappa di dispatch (102.392 voci, 0 collisioni), `storm_scomp.c`, `storm_sdraw.c`
(kernel SCode 1x/2x LUT, span check), presentatore GL/GLES2 con shader di palette, logica di gioco,
shim Win32/DDraw/DSound/GDI/Smack/SNP. Su ARM cambia il *driver* di presentazione scelto a runtime
(GLES 3.1 + Mesa/Panfrost su Mali-T860), non il codice.

## Convenzione dei nomi (dopo questo riordino)

- **Nessun suffisso di architettura** nei file condivisi: `bridge.c`, `com_bridge.c`, `crt_init.c`,
  `elf_support.c`, `game_main.c`, `game_memory.c`, `guest_call.c`, `storm_sdraw.c`, `win32_ext.c`,
  `x87_crt.c`, `guest_cpu_helpers.c` (ex `x86_helpers`).
- Restano per-arch, e devono: `runtime/arch/arm/*`, `runtime/arch/x86/*` e gli artefatti generati
  `guest_layout_arm.h` / `guest_layout_x86.h`, `pe_headers_arm.c`, `storm_ordinals[_arm].c`.
- Gli helper `X86_*` in `guest_cpu_helpers.c` nominano la CPU **emulata**, non l'host.

## Verifica dopo il riordino

| prova | esito |
|---|---|
| build x86 (`--arch x86 --skip-entry`) | OK, `runtime=37 shim=13` (senza oggetti fantasma) |
| `tests/x86_scomp_test/check.sh` | OK |
| `tests/x86_runtime_check/check.sh` | PASS |
| intro x86, luminanza media | 0,0555 |
| menu x86, luminanza media | 0,2200 |
| build ARMv7 + deploy Pinebook | OK |
| menu ARM, luminanza media | 0,0556 |
| registro ARM, luminanza media | 0,2200 |
