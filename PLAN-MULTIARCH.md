# `starcraft-reasm-generic` — Contesto completo e piano per il porting 32-bit / multi-architettura

**Data creazione:** 2026-11-21
**Origine:** estratto pulito da `/home/enne2/dev/starcraft-armv6` (stato fino a `menu8`, 2026-11-21 01:21).
**Host di sviluppo:** `frigate` — AMD Ryzen 7 H 255 (`x86_64`, LLVM `znver4`), Fedora Linux 44 (Workstation Edition), KDE/Wayland (`XDG_CURRENT_DESKTOP=KDE`).

---

## 1. Scopo di questa directory

Portare StarCraft (binari originali `StarCraft.exe` + `storm.dll`, già sottoposti a lifting statico via `SRW` → `.llasm` → `llasm` → LLVM IR `.ll`) verso un runtime **generico e multi-architettura**:

1. **Fase 1 (immediata):** compilare e far funzionare il gioco **a 32 bit nativi (`i686-unknown-linux-gnu`)** direttamente su questa CPU `x86_64` (senza QEMU), partendo dall'LLVM IR già liftato e separando chiaramente le parti comuni da quelle specifiche per architettura.
2. **Fase 2 (successiva):** una volta stabile su `i686`, consolidare la separazione `runtime/common/` + `runtime/arch/<arch>/` (incluso `arm` già esistente) ed estendere il supporto ad altre architetture (32-bit e 64-bit).

---

## 2. Stato ereditato da `starcraft-armv6` (verificato)

Nota: `CHECKPOINT.md` (20 nov 16:46) è un documento intermedio superato dai report più recenti in `work/link/` e `work/qemu/`:
- `work/link/REPORT-arm-menu.md`
- `work/link/REPORT-arm-fix.md`
- `work/link/REPORT-arm-scode.md`
- `work/qemu/REPORT-arm-pbp.md`

### Cosa funziona già nell'architettura del port
- **Lifting statico completo (`build/lift-full/`):**
  - `StarCraft.llasm` / `StarCraft.ll` (77 MiB) e `storm.llasm` / `storm.ll` (21 MiB).
  - Loader ordinale Storm: **135/135** import ordinali risolti; tabella di dispatch indiretta con **101.800** entry ordinate e univoche (zero collisioni VA↔host).
- **Layout a spazi disgiunti (`guest_data.ld` + `guest_pe_bytes.o`):**
  - Dati e gap PE mappati ai VA originali (`0x00400000..` EXE, `0x15000000..` Storm).
  - Stack guest emulato a `0x02000000+`, arena low a `0x0E000000+` (`< 0x80000000`, per preservare l'invariante `~ptr` usato da Storm per i contenitori vuoti).
  - Codice ricompilato del port spostato a `IMG_BASE=0x18000000` (`--section-start=.text=0x18000000`), disgiunto da tutte le aree dati guest.
- **Runtime e Shim SDL2 (`runtime/arm/` + `runtime/shim-port/elf/`):**
  - `menu8` su hardware ARM reale (Pinebook Pro RK3399, compat AArch32) supera il CRT, carica gli MPQ, apre finestra SDL2/X11 e audio PulseAudio, **visualizza e anima il menu principale** (`Single Player`, `Multiplayer`, `Campaign Editor`, `Exit`) con `libsmacker` e tocca ~173–207 FPS prima di fermarsi su `[arm-dispatch] STOP: target = 0x0041CAC8` (collocato da IDA a metà istruzione a `0x0041CAC6`, probabile callback/residui di chiusura).
  - `runtime/arm/storm_sdraw.c` contiene un interprete fail-closed per i kernel `SCode` x86 generati da Storm a runtime e il percorso row-by-row per ROP `0xCC0020` / `0xF00021`.

---

## 3. Verifica tecnica: quanto è davvero indipendente dall'architettura l'IR?

### 3.1 L'IR puro (`build/lift-full/StarCraft.ll` e `storm.ll`): **PORTABILE, con vincolo 32-bit little-endian**
- **Assenza di dipendenze ISA host:** nessuna riga `target triple`, nessun inline assembly (`call asm`), nessun intrinsic specifico ARM o x86. Tutto il codice usa operazioni LLVM standard su `%_cpu*` e `musttail call fastcc`.
- **Helper portabili (`vendor/sr/SR/llasm-support/llasm_*.c`):** 137 funzioni C pure per semantica x86/x87 (`div`/`idiv`, `rep movs/stos/cmps/scas`, FPU x87, `fs:` TEB/PEB, I/O/int stop).
- **Vincolo strutturale sull'IR `.ll` attuale:**
  - `target datalayout = "e-p:32:32-f64:32:64-n32"` (little-endian, puntatori a 32 bit).
  - Uso pervasivo di `inttoptr i32` / `ptrtoint ... to i32` e costanti assolute a 32 bit nei segmenti dati e nelle chiamate.
  - **Conseguenza:** l'IR `.ll` attuale è compilabile per qualunque target **32-bit little-endian** supportato da LLVM (incluso `i686-unknown-linux-gnu`), ma **non** direttamente per target 64-bit (`x86_64`, `aarch64` LP64) senza rigenerarlo dai `.llasm` con `llasm` in modalità 64-bit / `PTROFS_64BIT`.

### 3.2 L'IR post-processato (`tools/dispatch-ir.py`)
- Allunga `%_cpu` da 136 a 144 byte aggiungendo `_ind_target` (indice 17, offset 136) e `_ind_esp` (indice 18, offset 140), verificati a compile-time da `_Static_assert` in `cpu_llasm.h`.
- Riscrive tutti i `musttail call fastcc void %reg(%_cpu*)` in `musttail call fastcc void @reasm_indirect(%_cpu* %cpu)`.
- In `storm.ll` sostituisce gli ordinali `#313`, `#334`, `#454`, `#457`, `#458` con wrapper che chiamano `storm_313_arm`, `storm_334_arm`, `storm_video_skip_enabled` (i nomi hanno suffisso `_arm`, ma la logica IR è standard).

### 3.3 Il runtime attuale (`runtime/arm/` e `tools/`): **SPECIFICO PER ARMv6**
Classificazione esatta di cosa va portato per `i686` (e poi astratto):

| File / Componente | Natura attuale | Cosa serve per x86-32 (`i686`) |
|---|---|---|
| `runtime/arm/indirect_dispatch.S` | **Assembly ARM** (`reasm_indirect`, `reasm_extern`, `storm_313_arm`, `storm_334_arm`, `__sse2_available_init*`) | Riscrivere in assembly x86-32 (`jmp`, nessun frame host accumulato) |
| `runtime/arm/gen_bridge_stubs.py` | Emette `bridge_stubs.S` in **assembly ARM** | Aggiungere `--arch x86` per emettere stub `i386` + `x86_bridge_common` |
| `runtime/arm/com_bridge.S` | 256 thunk COM in **assembly ARM** | Equivalente `com_bridge_x86.S` (256 thunk + tail-jump) |
| `runtime/arm/guest_abs_zero.S` | Direttiva `.arm` per `__fpmath = 0` | Equivalente `.text`/`.globl __fpmath; .set __fpmath, 0` (neutro/x86) |
| `runtime/arm/tls_arm.S` | `__aeabi_read_tp` (solo ARM EABI) | **Non necessario** su `i686` (glibc TLS usa `%gs` nativamente) |
| `runtime/arm/cpu_llasm.c` | C portabile tranne `uc_mcontext.arm_pc`/`arm_lr` | `#ifdef __i386__` → `REG_EIP` / `REG_ESP` (`_GNU_SOURCE`) |
| `runtime/arm/game_main.c` | C portabile tranne profiler `uc_mcontext.arm_*` | `#ifdef __i386__` → `REG_EIP`, `REG_EBP`, `REG_ESP` |
| `runtime/arm/bridge.c` | C quasi tutto portabile (`CLOCK_MONOTONIC_ARM` / `timespec`) | Usare `<time.h>` (`CLOCK_MONOTONIC`) o `#ifdef` pulito |
| `runtime/arm/game_memory.c`, `elf_support.c`, `guest_cpu_helpers.c`, `x87_crt.c`, `crt_init.c`, `guest_call.c`, `indirect_dispatch.c`, `com_bridge.c`, `win32_ext.c`, `storm_sdraw.c`, `game_glue.c` | **C portabile a 32-bit** (nessun inline asm ARM) | Compilabili con `clang -m32` verificando tipi `unsigned long` vs `uint32_t` e struct stat/timespec |
| `runtime/shim-port/elf/*.c` | Shim C già patchati (copiati da `build/runtime-arm/shim-port/`) | Compilabili direttamente con `clang -m32` |
| `build/i386-libs/libsmacker-1.2.0.so` | Già compilato ELF 32-bit i386 | Pronto per `SC_SMACKER_SO` o fallback di ricerca |

---

## 4. Prerequisiti di sistema verificati su `frigate`

- **Compiler / LLVM:** `clang`, `opt`, `llc` (LLVM 22.1.8, backend `x86` e `arm` presenti), `llvm-objcopy`, `llvm-nm`, `readelf`.
- **Linker:** `~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld`.
- **Pacchetti 32-bit installati (2026-11-21):**
  - `glibc-devel.i686`, `libxcrypt-devel.i686`, `libgcc.i686`, `libstdc++-devel.i686`, `sdl2-compat-devel.i686` (oltre alle `.so` runtime `.i686` per X11, Wayland, Mesa, PulseAudio, ALSA).
  - Verifica eseguita: `clang -m32` compila, linka con `-lSDL2` ed esegue correttamente su `frigate`.

---

## 5. Contenuto di questa cartella (`starcraft-reasm-generic`)

- `build/lift-full/`: `StarCraft.orig.exe`, `storm.orig.dll`, `.i64` IDA (per oracolo), `StarCraft.llasm`, `storm.llasm`, `StarCraft.ll`, `storm.ll`, `*.sci`, `relocations*.csv`, `run-exe/`, `run-storm/`, `bin/SRW-llasm`, `bin/llasm`.
- `build/asset-sandbox/`: symlink verso `/home/enne2/dev/final-starcraft/Starcraft/*` (MPQ, `Local.dll`, ecc.).
- `build/i386-libs/libsmacker-1.2.0.so`: libreria Smacker 32-bit x86.
- `runtime/arm/`: runtime di riferimento ereditato (con `patches/`).
- `runtime/shim-port/`: sorgenti degli shim C già aggiornati fino a `menu8` + `shim_rename.txt` + `bridge_shims.def`.
- `vendor/`: snapshot completo di SR (`SRW`, `llasm`, `llasm-support`).
- `tools/`: script di build, lifting, dispatch e test.
- `tests/`: test freestanding e slice ordinale.
- `work/`: report tecnici e diagnostica ereditati da `starcraft-armv6`.

**Nota:** NON sono stati copiati `build/sysroot` (ARM), `build/qemu*` (VM QEMU ARM), `build/slice` o `build/lift-full/entry` (cache `.o` ARM), per mantenere il workspace leggero (~298 MB) e indipendente dagli artefatti ARM precompilati.

---

## 6. Piano dettagliato — Fase 1: Porting 32-bit su `i686-unknown-linux-gnu`

### Passo 1 — Verifica toolchain e compile test dell'IR su `i686`
1. Derivare il `target datalayout` di `i686-unknown-linux-gnu`:
   ```sh
   clang --target=i686-unknown-linux-gnu -m32 -S -emit-llvm -x c /dev/null -o build/layout-i686.ll
   ```
   Confrontarlo con `"e-p:32:32-f64:32:64-n32"` (su i686 è tipicamente `e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-f64:32:64-f80:32-n8:16:32-S128`).
2. Creare uno script dedicato `tools/build-entry-x86.sh` (o generalizzare con `ARCH=x86|arm`) che:
   - Generi `StarCraft.entry.ll` e `storm.entry.ll` via `tools/dispatch-ir.py`.
   - Esegua `opt -passes=verify` + `opt -passes='default<O2>'` (o `O1` per iterazione veloce) con `-data-layout="$dl"`.
   - Compili con `llc -mtriple=i686-unknown-linux-gnu -filetype=obj` in `build/lift-full/entry-x86/`.
3. **Punto critico da verificare subito su `i686`:** la calling convention **`fastcc`** combinata con **`musttail call`** su LLVM x86-32!
   - Su ARM `fastcc` passa il primo argomento `%_cpu*` in `r0` (identico a `ccc`) e `musttail` genera `b <target>` senza toccare lo stack.
   - Su x86-32 `fastcc` passa il primo argomento `%_cpu*` in **`%ecx`** (invece che sullo stack come `cdecl`), e `musttail` deve generare `jmp <target>` lasciando `%ecx = %_cpu*` e `(%esp)` = return address del chiamante C (`cpu_llasm_run_raw` / `guest_call_at` / `arm_bridge_common` / `reasm_extern`).
   - **Verifica obbligatoria prima di linkare tutto:** ispezionare con `llvm-objdump -d` un paio di funzioni compilate in `StarCraft.entry.o` (es. `loc_404C21` e `sc_loc_404C21_impl`) e un piccolo test sintetico `make check-x86` per confermare esattamente in quale registro (`ecx` vs `eax` vs `[esp+4]`) LLVM `i686` passa `%_cpu*` in `fastcc` e `ccc`!

### Passo 2 — Organizzazione pulita `runtime/` e implementazione trampolini x86-32
Separare il codice senza rompere `runtime/arm/` (opzionale: tenere `runtime/arm/` intatto come riferimento e creare `runtime/x86/` + `runtime/common/` oppure aggiungere i file `*_x86.S` e `#ifdef __i386__` nei `.c` condivisi). Scelta consigliata per ordine multi-arch:
- Creare `runtime/x86/` per i soli file specifici i686:
  1. `runtime/x86/indirect_dispatch_x86.S`:
     - `reasm_indirect`: legge `_cpu->_esp` (offset 16), salva in `_cpu->_ind_esp` (offset 140), legge `_cpu->_ind_target` (offset 136), chiama `reasm_indirect_resolve(target)` (`cdecl`), se `0` chiama `reasm_indirect_unresolved(cpu, target)`; altrimenti ripristina `%_cpu*` (secondo il contratto `fastcc` verificato al Passo 1!) e fa `jmp *%eax` **senza lasciare frame sullo stack host**.
     - `reasm_extern`: esegue `cdecl`/`fastcc` della funzione C target su `cpu`, chiama `cpu_llasm_pop32(cpu)` per ottenere il return VA guest, lo risolve con `reasm_indirect_resolve`, ripristina lo stack host e fa `jmp *%eax`.
     - `storm_313_arm`, `storm_334_arm`, `__sse2_available_init`, `__sse2_available_init_storm`: stub equivalenti che caricano il puntatore alla funzione `*_body` e saltano a `reasm_extern`.
  2. `runtime/x86/com_bridge_x86.S`:
     - 256 thunk `arm_com_thunk_0..255` + `arm_com_thunk_table` + `arm_com_bridge_common` con la stessa disciplina `jmp *%eax`.
  3. `runtime/x86/guest_abs_zero_x86.S`:
     - `.globl __fpmath; .set __fpmath, 0` (sintassi GAS i386).
  4. `runtime/arm/gen_bridge_stubs.py`:
     - Aggiungere `--arch arm|x86`. In modalità `x86` genera `bridge_stubs.S` con:
       - `x86_bridge_common` (chiama `arm_bridge_common_c(cpu, idx)`, ripristina `%_cpu*` e i callee-saved x86 `ebx/esi/edi/ebp`, fa `jmp *%eax`).
       - Ogni stub `<sym>:` imposta l'indice e salta a `x86_bridge_common`.
- Adattare i punti puntuali nei `.c` condivisi (usando `#ifdef __i386__` / `#elif defined(__arm__)`):
  - `cpu_llasm.c`: in `cpu_signal`, leggere `uc->uc_mcontext.gregs[REG_EIP]` (con `#define _GNU_SOURCE`).
  - `game_main.c`: in `sc_prof_handler`, leggere `REG_EIP`, `REG_EBP`, `REG_ESP` su `__i386__`.
  - `bridge.c`: usare `<time.h>` (`struct timespec`, `clock_gettime(CLOCK_MONOTONIC, &ts)`) valido su entrambe le architetture 32-bit glibc/ABI.
  - `runtime/arm/gen_pe_image_bytes.py` / `gen_game_layout.py`: verificare che funzionino anche su oggetti ELF32 i386 (`llvm-nm` / `readelf`).

### Passo 3 — Compilazione shim, helper e link i686
1. Compilare `vendor/sr/SR/llasm-support/llasm_*.c` con `clang -m32 -O2 -I vendor/sr/SR/llasm-support`.
2. Compilare `runtime/shim-port/elf/*.c` con `clang -m32 -O1 -g -DSC_ELF_REAL_GDI=1 -I runtime/shim-port/elf` (+ header SDL2 dell'host), rigenerare `bridge_shims.def` / `shim_rename.txt` da AST `clang -m32 -Xclang -ast-dump` (importante: verificare che non ci siano differenze di arità/tipi tra ARM e i386, es. allineamento di `int64_t`/`double` in `va_list` o struct passate per valore!).
3. Applicare `llvm-objcopy --redefine-syms` sugli oggetti degli shim in una directory temporanea.
4. Linkare con `ld.lld -m elf_i386`:
   - Usare `crt1.o`, `crti.o`, `crtn.o` 32-bit di Fedora (`/usr/lib/crt1.o`, `/usr/lib/gcc/x86_64-redhat-linux/16/32/crtbegin.o` ecc., oppure lasciare guidare a `clang -m32 --ld-path=$LLD -Wl,...`) con `-Wl,--section-start=.text=0x18000000 -Wl,-T,guest_data.ld -Wl,-z,muldefs`.
   - **Attenzione a PIE/ASLR su Fedora:** disabilitare PIE (`-no-pie` / `-fno-pie`) in compilazione e link! Gli indirizzi guest (`0x00400000`, `0x15000000`) e il `.text` del port (`0x18000000`) devono essere fissi (`ET_EXEC`), non `ET_DYN` relocato sopra `0x80000000`.
   - Linkare `-lc -lm -lgcc_s -lSDL2 -ldl -lpthread`.
5. Verificare con `tools/dispatch-ir.py --dump-map build/starcraft-x86` (0 collisioni) e `readelf -h` (`ELF32`, `Intel 80386`, `EXEC`).

### Passo 4 — Test e debug su `frigate`
1. **Test micro (`tests/slice` equivalente x86):** verificare `cpu_llasm_run`, `cpu_llasm_run_raw` (sentinel), chiamate `fastcc`↔`cdecl` e `ordinal_fixture` su i686 nativo.
2. **Smoke headless in `build/asset-sandbox`:**
   ```sh
   SC_SMACKER_SO="$PWD/build/i386-libs/libsmacker-1.2.0.so" \
   SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     ./build/starcraft-x86
   ```
   Verificare: CRT init → `DllMain` storm → `SetDisplayMode` → MPQ loading → SCode/blitter → menu principale.
3. **Prova interattiva con finestra reale su `frigate`** (con `SDL_VIDEODRIVER=x11` o Wayland nativo e audio PulseAudio/PipeWire).
4. **Indagine sullo STOP residuo `0x0041CAC8`** (se si ripresenta anche su x86) o confronto con il comportamento di `sc-reasm` originale usando IDA come oracolo primario.

---

## 7. Piano — Fase 2: Generalizzazione Multi-Architettura

Una volta che `i686` e `armv6` condividono la stessa base di runtime:

### 7.1 Struttura target del repository
- `runtime/common/`: tutta la logica C indipendente dall'architettura (`cpu_llasm.c`, `game_memory.c`, `bridge.c`, `elf_support.c`, `guest_call.c`, `indirect_dispatch.c`, `guest_cpu_helpers.c`, `x87_crt.c`, `crt_init.c`, `storm_sdraw.c`, `game_main.c`).
- `runtime/arch/<arch>/`: solo lo strato minimo dipendente da ISA/ABI (`indirect_dispatch.S`, `com_bridge.S`, `bridge_stubs_arch.py`, eventuale `tls.S`, helper `ucontext` per il signal handler).
- `tools/build.sh --arch <i686|armv6|...>`: un'unica pipeline parametrizzata per `TRIPLE`, flag CPU, `OPT`/`LLC` e link.

### 7.2 Supporto ad altre architetture 32-bit e 64-bit
1. **Altre architetture 32-bit little-endian (`armhf`/`armv7`, `riscv32`, `wasm32`?):**
   - L'IR `.ll` esistente (`e-p:32:32`) funziona **senza rigenerarlo da `.llasm`**; basta implementare i 3 file di trampolino in `runtime/arch/<arch>/` (meno di ~300 righe di assembly per architettura) e i flag target in `tools/`.
2. **Architetture 64-bit (`x86_64`, `aarch64`, `riscv64`):**
   - **Perché non basta ricompilare l'IR attuale:** i file `.ll` correnti hanno `p:32:32` e `inttoptr i32` / `ptrtoint ... to i32` incompatibili con puntatori host a 64 bit (a meno di ABI ILP32 come `x86_64-gnux32`, dove però mancano spesso librerie utente/SDL2 distro).
   - **Percorso pulito già previsto da `vendor/sr/llasm/llasm.d`:**
     - `llasm` supporta nativamente la modalità 64-bit + `PTROFS_64BIT` (`cpu->_pointer_offset`, `REG2PTR` / `PTR2REG` in `vendor/sr/SR/llasm-support/llasm_cpu.h`).
     - In questa modalità il guest continua a vedere indirizzi virtuali a 32 bit (`0x00400000.. < 0x80000000`, preservando l'invariante `~ptr` di Storm!), mentre sull'host a 64 bit l'intera arena da 2–4 GiB è mappata a un offset base (`_pointer_offset`) tramite `mmap`.
     - Richiede inoltre:
       1. Estendere `tools/dispatch-ir.py` per gestire la riscrittura `inttoptr i64`/`ptrtoint ... to i64` (o la forma emessa da `llasm -64bit`), le tabelle `reasm_va_fn` a 64 bit (`ptr`) e l'offset di `_ind_target`/`_ind_esp` nella struct `_cpu` 64-bit.
       2. Un marshalling `uint32_t` ↔ `uintptr_t` negli shim C (`win32_shim.c`, `com_*`) dove i puntatori guest a 32 bit vengono tradotti via `REG2PTR` / `PTR2REG` (o mantenendo i dati guest mappati nei primi 2 GiB dello spazio virtuale 64-bit tramite `MAP_FIXED` / linker script sotto `0x80000000`, soluzione che permette `REG2PTR(x) == (void*)(uintptr_t)x` con `_pointer_offset == 0`!).
       3. Verificare `va_list`/stdcall arity in `gen_shims_bridge.py` per ABI LP64 (System V AMD64 / AAPCS64).

---

## 8. Regole operative e vincoli da mantenere

- **Oracolo primario:** in caso di dubbi su semantica, costanti, vtable o crash guest, consultare sempre i database IDA in `build/lift-full/StarCraft.orig.exe.i64`, `storm.orig.dll.i64` e `build/lift-full/ida/` (sola lettura se non serve annotare, mai indovinare).
- **Non modificare sorgenti esterni:** non toccare `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `~/dev/final-starcraft` o `vendor/` senza esplicita necessità documentata.
- **Fail-closed (regola D19):** mai stub silenziosi per API non implementate o kernel `SCode` sconosciuti; ogni percorso non gestito deve loggare e uscire con codice distinto.
- **Nessuna modifica non autorizzata su altre sessioni IDA:** controllare sempre `ida_list` prima di usare tool IDA.
