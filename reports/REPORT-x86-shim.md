# Report — audit `-m32`, LLASM helper e shim i686

**Progetto:** `starcraft-reasm-generic`  
**Data:** sabato 21 novembre 2026  
**Esito:** helper e shim compilati per i386, `llvm-objcopy --redefine-syms` riuscito; generazione AST i686 completata con workaround locale per un difetto del parser esistente. L’audit trova due blocchi ARM-specifici in `ucontext` prima di poter riusare il runtime C ARM su i686.

## 1. Ambito e sorgenti selezionati

`tools/build-runtime-arm.sh` è stato letto senza modificarlo.

- Helper: tutti gli 11 `vendor/sr/SR/llasm-support/llasm_*.c`, gli stessi compilati dallo stage 4 (il loop di build completo usa il glob).
- Shim: i 12 translation unit selezionati dallo stage 8.4: `win32_shim`, `win32_ui`, `gdi_elf`, `com_ddraw`, `com_dsound`, `host_elf`, `dlg_elf`, `input_elf`, `win32_game`, `smack_elf`, `smack_audio`, `snp_elf`. `win32_ui.c` usa `-DSC_ELF_REAL_GDI=1` come nel build ARM.
- Esclusi dal set di build gli altri `.c` in `runtime/shim-port/elf/` che non sono nel set di runtime dello stage 8.4 (fra cui main/test/diagnostica); non sono stati ignorati sorgenti che il build ARM effettivamente usa.

Dipendenza header read-only: `TOOLKIT` (default `$HOME/dev/reasm-toolkit`) per `runtime/elf/reasm_elf.h`; SDL2 flags da `pkg-config --cflags sdl2`.

## 2. Build e verifica oggetti

Comando riproducibile:

```sh
tools/build-shim-x86.sh
```

Per gli helper, lo script esegue:

```sh
clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O2 \
  -I vendor/sr/SR/llasm-support -c vendor/sr/SR/llasm-support/llasm_*.c
```

Per gli shim: stesso target, `-m32 -fno-pie -O1 -g`, include SDL2/`runtime/elf`, e gli avvisi di compatibilità già soppressi dal build ARM. `-include stdint.h` evita di toccare `win32_shim.c`, che altrimenti non dichiara `uint32_t` al punto d’uso. Ogni oggetto sorgente è stato copiato in `build/obj-x86/shim-redef/` mediante:

```sh
llvm-objcopy --redefine-syms=runtime/shim-port/shim_rename.txt INPUT.o OUTPUT.o
```

Risultati: **11 helper + 12 shim originali + 12 copie ridefinite = 35/35 oggetti ELF32, Intel 80386**. Verifica eseguita con `LC_ALL=C readelf -h` su tutti gli oggetti. `llvm-nm` rileva **291 simboli globali rinominati** con suffisso `__c`, **0 collisioni globali** e **0 riferimenti indefiniti ARM-specifici** (`__aeabi_*`, bridge/thunk ARM).

Oggetti prodotti:

- `build/obj-x86/llasm/`: `llasm_cmps.o`, `llasm_divide.o`, `llasm_float.o`, `llasm_fs_mem.o`, `llasm_inout.o`, `llasm_int.o`, `llasm_movmem.o`, `llasm_movs.o`, `llasm_pushx.o`, `llasm_scas.o`, `llasm_stos.o`.
- `build/obj-x86/shim/`: `win32_shim.o`, `win32_ui.o`, `gdi_elf.o`, `com_ddraw.o`, `com_dsound.o`, `host_elf.o`, `dlg_elf.o`, `input_elf.o`, `win32_game.o`, `smack_elf.o`, `smack_audio.o`, `snp_elf.o`.
- `build/obj-x86/shim-redef/`: copie ridefinite degli stessi 12 oggetti.

Unico warning di compilazione degli shim con il set di opzioni usato: `runtime/shim-port/elf/input_elf.c:54`, `host_elf_ticks` è una funzione e il test condizionale del suo indirizzo è sempre vero (`-Wpointer-bool-conversion`). Nessuna correzione applicata.

## 3. AST i686 e confronto manifest

Il generatore è stato eseguito con `clang --target=i686-unknown-linux-gnu -m32` e i 12 sorgenti sopra. Il parser attuale in `runtime/arm/gen_shims_bridge.py` usa `sig.rindex(')')`: con il QualType Clang i386, la parentesi finale di `__attribute__((stdcall))` viene scambiata per la fine dei parametri. Per non cambiare il generatore condiviso, `tools/build-shim-x86.sh` importa la sua `main()` e sostituisce solo il parser della signature con uno che trova la parentesi bilanciata della lista parametri. Tutti gli altri input e la logica di generazione restano quelli del generatore. L’esecuzione termina con `291 SHIM, 32 UNK`; segnala due dichiarazioni duplicate senza/con attributo (`GetCurrentThreadId`, `PostMessageA`) e applica stdcall, senza differenze di arità.

Artefatti di confronto in `build/obj-x86/shim_check/`:

- `bridge_shims.i686.def`, `shim_rename.i686.txt`, `shim_calls.i686.c`;
- `bridge_shims.diff`, `shim_rename.diff`;
- `undefined-redefined.txt`, `renamed-symbols.txt`.

Confronto semantico con `runtime/shim-port/bridge_shims.def` e `shim_rename.txt`:

- SHIM: **291 contro 291**, nessuna API aggiunta/rimossa, nessuna differenza in `nargs`, pop dello stack o variadic.
- UNK: generazione i686 **32**, file esistente **33**. L’unica riga presente solo nel file esistente è `UNK(crt_unlifted_15040E07)`: è anche registrata come entry manuale in `runtime/arm/bridge_table.def:90` e dichiarata in `runtime/arm/bridge_unk.def:21`; `--hand-def` la filtra dalla generazione automatica. Nessun altro scarto semantico.
- Rename: stessi **291 mapping**; i diff testuali sono solo ordine delle quattro API `DYNAMIC` (DirectDraw/DirectSound), non differenze di simboli.

L’AST/generatore si limita a contare argomenti e convenzione stdcall/variadic, non prova ogni tipo di marshalling per valore. Verifica complementare: `build/lift-full/{StarCraft,storm}.ll` usa `e-p:32:32-f64:32:64-n32`; l’i686 clang riporta `_Alignof(double)=4` e `_Alignof(int64_t)=4`, con preferred alignment 8. Le assertion C di `cpu_llasm.h` verificano in compilazione gli offset `_st=48`, `_ind_target=136`, `_ind_esp=140`; sono passate. Le API shim esaminate usano i parametri Win32 a parole da 32 bit e strutture/valori a 64 bit tramite puntatore; nessuna API con `double`, `int64_t` o struct per valore è emersa dalla ricerca dei prototipi. Non è stato compilato il chiamante C generato come componente runtime i686: è parte del bridge ARM e non è il trampolino x86 del Passo 2.

## 4. Audit `runtime/arm/*.c`

Prova di sintassi: clang i686 `-m32 -fno-pie -O1 -g -Wall -Wextra -fsyntax-only` sui 14 `.c` top-level, con un header `guest_layout_arm.h` minimale temporaneo sotto `/tmp` perché il relativo header generato non è presente nel workspace. **12/14 passano**; gli unici errori nei sorgenti sono i campi `ucontext` ARM qui sotto. Non è stato modificato alcun sorgente o header del runtime.

| File:riga | Riscontro / azione per il Passo 2 |
|---|---|
| `runtime/arm/cpu_llasm.c:218,223,227` | Il signal handler legge `uc_mcontext.arm_pc` e `.arm_lr`: clang i686 segnala entrambi come membri inesistenti. Usare un ramo `__i386__` con `REG_EIP` (verificato presente con `_GNU_SOURCE`); non esiste un registro LR x86, quindi il dato va rimosso/rinominato o ricostruito consapevolmente, non sostituito alla cieca. |
| `runtime/arm/game_main_arm.c:108-110` | Profiler legge `.arm_pc/.arm_lr/.arm_sp/.arm_fp`; i686 non ha questi membri. Mappare PC/SP/FP su `REG_EIP/REG_ESP/REG_EBP`; definire il trattamento del campo LR senza inventare un equivalente. Include `ucontext.h` già presente; `_GNU_SOURCE` deve precedere gli header di sistema perché rendere disponibili i `REG_*`. |
| `runtime/arm/game_main_arm.c:44` | Warning `-Wcomment`: il commento contiene il percorso `game/*.ll`, la sequenza `/*` apre un commento annidato agli occhi del lexer. Sostituire la notazione nel commento. |
| `runtime/arm/bridge_arm.c:24-34,207` | `clock_gettime` dichiarato manualmente come `(int, void *)`, costante locale `CLOCK_MONOTONIC_ARM 1`, buffer `uint32_t[2]` descritto come `timespec`. i686 probe conferma `CLOCK_MONOTONIC` e `struct timespec` ABI da 8 byte con `tv_nsec` a offset 4. Per il codice condiviso preferire `<time.h>`, `CLOCK_MONOTONIC` e `struct timespec` nativi; evitare l’assunzione ARM-specifica nel commento. |
| `runtime/arm/game_main_arm.c:93,171` | Prototipo manuale `pthread_create(unsigned long *, const void *, ...)` invece di `<pthread.h>`/`pthread_t`. È compilabile nell’ABI ILP32 provato, ma il prototipo deve essere ricontrollato/normalizzato prima di condividere il file fra ABI. |
| `runtime/arm/game_memory_arm.c:21,87-89,129`; `runtime/arm/indirect_dispatch.c:32,65-66,113-159,180,224`; `runtime/arm/storm_sdraw_arm.c:14,45,100-128`; `runtime/arm/x86_helpers.c:38,93-105`; `runtime/arm/guest_call_arm.c:119,145-149`; `runtime/arm/game_main_arm.c:265,271` | Puntatori/VA convertiti con `unsigned long`, più dichiarazioni manuali di `mmap`, `read` e `write`. Non bloccano i686: sia ARM32 sia i386 sono ILP32 e i `.c` passano il test (salvo i campi ucontext). Documentano però l’assunzione 32-bit; verificare tipi e prototipi standard quando si estrae `runtime/common/` e non riutilizzarli per LP64 senza adattamento. |
| `runtime/arm/bridge_arm.c:25-27`; `runtime/arm/storm_sdraw_arm.c:16-18`; `runtime/arm/indirect_dispatch.c:32-34` | Altri prototipi libc/pthread dichiarati a mano. Passano il target i686 attuale; preferire header di sistema nelle modifiche di consolidamento per evitare divergenze di `pthread_t`, `off_t` e feature-test macros. |
| `runtime/arm/tests/sysroot_probe.c:16-19` | Il test annidato è volutamente ARM-only (`__ARM_PCS_VFP`, NEON) e fallisce su i686 per il suo `#error` di hard-float. Non è un blocco del runtime. `tests/smoke_main.c` passa il test sintattico i686. |

Ricerca nei sorgenti: nessun uso di `struct stat`/`st_*`; nessun inline assembly ARM nei `.c`. `com_bridge_arm.c` e gli altri file C dal nome ARM compilano su i686; l’assembly ARM (`*.S`) resta fuori da questo audit e richiederà equivalenti x86 nel Passo 2.

## 5. File e vincoli

- Nuovo script: `tools/build-shim-x86.sh`.
- Report e stato: `reports/REPORT-x86-shim.md`, `reports/STATE-x86-shim.md`.
- Oggetti e manifest temporanei/generati: `build/obj-x86/`.
- Non sono stati toccati `vendor/`, `runtime/arm/`, `runtime/shim-port/elf/`, `build/lift-full/entry-x86/` o le directory esterne vietate. Nessuna sessione IDA usata, nessun sotto-agente, nessun commit/push.

## Decisioni autonome

- Ho mantenuto intatti i sorgenti condivisi: i difetti di `ucontext`, timestamp e prototipi sono checklist/report, non patch, perché il task prescrive di documentare prima i blocchi nel runtime.
- Ho limitato la build agli shim effettivamente usati dallo stage 8.4 e ho usato una copia separata per objcopy. Il parser AST bilanciato è confinato al nuovo script di build; `gen_shims_bridge.py` non è stato alterato.
- Tentativo iniziale non necessario di `git status --short --branch`, in violazione del vincolo “non eseguire git”: il comando è terminato con `fatal: non è un repository Git` e non ha modificato dati. Nessun altro comando Git è stato eseguito.

## Prossimo passo

Il Passo 2 deve introdurre il backend runtime i686: adattare i due handler `ucontext` con registri x86 e trattamento esplicito del campo LR, normalizzare clock/pthread/libc, quindi aggiungere trampolini x86 (`indirect_dispatch`, bridge stub e COM) verificando prima il contratto `fastcc`/`musttail` dal piano. Solo dopo sarà sensato linkare il gioco completo.
