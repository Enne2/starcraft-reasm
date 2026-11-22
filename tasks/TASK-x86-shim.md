# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su signature Win32/Storm, convenzioni di chiamata (`stdcall`/`cdecl`/`thiscall`), vtable o costanti del binario originale, l'oracolo primario sono i database IDA in `build/lift-full/StarCraft.orig.exe.i64` e `build/lift-full/storm.orig.dll.i64`. Prima di usare tool IDA controlla sempre `ida_list` e NON toccare sessioni IDA aperte per altri lavori.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/` né `build/lift-full/entry-x86/`.

---

# Missione `scx86-shim`: Audit portabilità `-m32` e compilazione helper `llasm` + shim SDL2 (`i686`)

Leggi `PLAN-MULTIARCH.md` (sezioni 3.3, 4, 6 Passo 2 parte C, Passo 3 punti 1-3) e ispeziona `runtime/arm/gen_shims_bridge.py`, `runtime/shim-port/bridge_shims.def`, `runtime/shim-port/shim_rename.txt` e `vendor/sr/SR/llasm-support/`.

## File di proprietà (esclusivi di questo task)
- `tools/build-shim-x86.sh` (nuovo script per compilare `llasm` support e shim `runtime/shim-port/elf/*.c` con `clang -m32`)
- `build/obj-x86/llasm/` (oggetti compilati da `vendor/sr/SR/llasm-support/llasm_*.c`)
- `build/obj-x86/shim/` (oggetti shim compilati `-m32` + ridefiniti con `llvm-objcopy --redefine-syms`)
- `build/obj-x86/shim_check/` (eventuali `.def` / `shim_rename.txt` rigenerati per i386 e diff rispetto a `runtime/shim-port/`)
- `reports/REPORT-x86-shim.md`
- `reports/STATE-x86-shim.md` (max 70 righe)

Nota: NON modificare i sorgenti dentro `runtime/shim-port/elf/` salvo stretta necessità di portabilità `-m32`; se trovi un bug bloccante in `runtime/shim-port/elf/` o `runtime/arm/*.c`, documentalo nel report con patch proposta prima di alterare file condivisi (oppure crea override/report dettagliato; se è una piccola correzione `#ifdef __i386__` sicura in `runtime/shim-port/elf/`, documentala esplicitamente). In ogni caso NON toccare `runtime/arm/`.

## Obiettivi e ordine di esecuzione

1. **Compilazione helper `llasm` (`vendor/sr/SR/llasm-support/llasm_*.c`)**:
   - Verifica quali file `.c` in `vendor/sr/SR/llasm-support/` vengono usati dal build ARM (controlla `tools/build-runtime-arm.sh` senza modificarlo).
   - Compilali con `clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O2 -I vendor/sr/SR/llasm-support` in `build/obj-x86/llasm/`.

2. **Verifica AST / `bridge_shims.def` / `shim_rename.txt` per `i686`**:
   - Ispeziona `runtime/arm/gen_shims_bridge.py` e i header/sorgenti di `runtime/shim-port/elf/`.
   - Esegui la generazione/verifica `gen_shims_bridge.py` con flag `clang -m32` (su i386) in `build/obj-x86/shim_check/` e confronta il risultato con `runtime/shim-port/bridge_shims.def` e `runtime/shim-port/shim_rename.txt`.
   - Verifica se ci sono differenze di arità, allineamento `int64_t`/`double`/`struct`, o macro ABI (`__i386__` vs `__arm__`).

3. **Compilazione `runtime/shim-port/elf/*.c` con `clang -m32`**:
   - Verifica in `tools/build-runtime-arm.sh` o `runtime/arm/README.md` quali `.c` in `runtime/shim-port/elf/` vanno compilati, quali `-D` e `-I` servono (es. `-DSC_ELF_REAL_GDI=1`, header SDL2 dell'host `pkg-config --cflags sdl2` o `/usr/include/SDL2`).
   - Compila tutti gli oggetti shim in `build/obj-x86/shim/` con `clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g`, poi applica `llvm-objcopy --redefine-syms` su una copia/directory dedicata `build/obj-x86/shim-redef/` (come previsto da Passo 3.3).
   - Verifica con `readelf -h` che ogni `.o` sia `ELF32` `Intel 80386` e con `llvm-nm` che i simboli ridefiniti non abbiano collisioni né riferimenti imprevisti a simboli ARM.

4. **Audit rapido dei `.c` in `runtime/arm/` per il futuro Passo 2**:
   - Senza modificare `runtime/arm/`, elenca con file+riga esatti tutti i punti non portabili o dipendenti da ARM nei `.c` di `runtime/arm/` (es. `uc_mcontext.arm_pc`, `CLOCK_MONOTONIC_ARM`, struct `stat`/`timespec`, warning con `clang -m32 -Wall`) così che il worker del Passo 2 abbia una checklist precisa.

5. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-shim.md` e `reports/STATE-x86-shim.md` (max 70 righe) con comandi esatti, lista `.o` prodotti, esito confronto AST/bridge_shims, checklist audit `runtime/arm/*.c`, `## Decisioni autonome` e `## Prossimo passo`.

## Gate di accettazione (oggettivi)
1. `tools/build-shim-x86.sh` compila tutti gli helper `llasm` e gli shim `runtime/shim-port/elf/*.c` a 32-bit i386 con exit code `0` e applica `--redefine-syms` senza errori.
2. `readelf -h` su tutti i `.o` prodotti conferma `ELF32` / `Intel 80386`.
3. Diff documentato tra `bridge_shims.def`/`shim_rename.txt` esistenti e generazione i386, più checklist riga-per-riga dei punti `runtime/arm/*.c` da adattare al Passo 2.
4. `reports/REPORT-x86-shim.md` e `reports/STATE-x86-shim.md` presenti e completi.
