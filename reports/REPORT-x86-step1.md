# Report — scx86-ir, Passo 1

## Stato di avanzamento

### 1. Datalayout i686 — verificato
Comando: `clang --target=i686-unknown-linux-gnu -m32 -S -emit-llvm -x c /dev/null -o build/layout-i686.ll`.
Il datalayout è `e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-f64:32:64-f80:32-n8:16:32-S128`. È little-endian e usa puntatori a 32 bit, coerente col requisito dell'IR legacy `e-p:32:32-f64:32:64-n32`; la stringa completa aggiunge dettagli ABI i686 (mangling, address spaces, i128/f80, stack alignment). Le prime righe dei moduli `.ll` sono alias/globali e non header di target; entrambe non fissano target datalayout nelle prime 30 righe, coerentemente col piano. LLVM/clang installato: 22.1.8.

### 2. Micro-test fastcc/musttail — verificato nativamente
Fonti: `tests/x86_fastcc_check/{check.ll,caller.c,trampoline.S}`. Micro-test sintetico, `%_cpu = { [36 x i32] }` (144 byte); caller esterno C cdecl -> wrapper IR cdecl -> `fnA` fastcc -> `fnB` fastcc -> trampoline assembly fastcc -> callback C cdecl -> destinazione fastcc. Il callback confronta il puntatore CPU con quello atteso e la destinazione scrive il sentinel `0x12345678`.

Comandi eseguiti (toolchain LLVM/clang 22.1.8):
```sh
DL=$(awk -F'"' '/^target datalayout/{print $2;exit}' build/layout-i686.ll)
opt -passes=verify -data-layout="$DL" tests/x86_fastcc_check/check.ll -o /tmp/x86-fastcc.verified.bc
llc -mtriple=i686-unknown-linux-gnu -relocation-model=static -filetype=obj -o tests/x86_fastcc_check/check.o /tmp/x86-fastcc.verified.bc
clang --target=i686-unknown-linux-gnu -m32 -fno-pie -c tests/x86_fastcc_check/caller.c -o tests/x86_fastcc_check/caller.o
clang --target=i686-unknown-linux-gnu -m32 -fno-pie -c tests/x86_fastcc_check/trampoline.S -o tests/x86_fastcc_check/trampoline.o
clang --target=i686-unknown-linux-gnu -m32 -fno-pie -no-pie tests/x86_fastcc_check/{caller.o,trampoline.o,check.o} -o tests/x86_fastcc_check/check
tests/x86_fastcc_check/check  # exit 0
llvm-objdump -d tests/x86_fastcc_check/check.o
```

Contratto osservato: fastcc passa l'unico `%_cpu*` in `%ecx`; wrapper cdecl legge il puntatore dallo stack (`0x10(%esp)` dopo spazio locale) e lo trasferisce in `%ecx`. `fnA` e `fnB` sono `jmp` con rilocazioni verso i simboli di destinazione, senza prologo/frame. Il return address resta sulla cima dello stack host, collocato dal `call` del caller C: `musttail` non lo sostituisce né alloca frame. Il trampoline salva `%ecx`, chiama callback C cdecl con argomento sullo stack, lo ripristina e conclude con `leave; jmp *%eax`; la destinazione fastcc ritorna infine al caller C. Test nativo eseguito con exit 0.

ABI i386 SysV: `%ebx`, `%esi`, `%edi`, `%ebp` e `%esp` callee-saved; `%eax`, `%ecx`, `%edx` caller-saved. Il trampoline preserva `%ebp` e ripristina `%esp`; gli altri callee-saved non vengono alterati.

### 3. Build oggetti x86 — verificata
Script `tools/build-entry-x86.sh` (exit 0): dispatch-ir, `opt -passes=verify`, `opt -passes='default<O1>'`, `llc -mtriple=i686-unknown-linux-gnu -relocation-model=static -filetype=obj`. Dispatch ha prodotto 102.376 VA totali e riscritto 12.766 call-site indiretti. La tabella globale VA→entry è nel solo `StarCraft.entry.o`, secondo `dispatch-ir.py`; Storm contiene il codice/ordinali e non duplica la tabella.

Tempi ultima build: StarCraft 147 s, Storm 20 s (verify + O1 + llc). Dimensioni: `StarCraft.entry.o` 15.485.272 byte; `storm.entry.o` 3.323.136 byte. IR intermedi conservati in `build/lift-full/entry-x86/`.

Per entrambi `readelf -h`: `Class: ELF32`, `Type: REL (Relocatable file)`, `Machine: Intel 80386`. Simboli verificati con `llvm-nm`: StarCraft `loc_404C21`, `sc_loc_404C21_impl`, `reasm_va_map_n`, `reasm_va_va`, `reasm_va_fn`; Storm `ordinal_73746f726d2e646c6c_313`.

Disassemblato `llvm-objdump -d --disassemble-symbols=loc_404C21,sc_loc_404C21_impl build/lift-full/entry-x86/StarCraft.entry.o`: l'implementazione fastcc riceve `%cpu` in `%ecx` e termina con `jmp` rel32 dopo ripristino di `%esi/%edi/%ebx`. `loc_404C21` è l'entry C ABI e carica il parametro dallo stack. Le istruzioni sono coerenti col micro-test.

## Decisioni autonome
- Scelto `default<O1>` per la prima iterazione: ottimizzazione concreta con costo più contenuto su StarCraft IR (77 MiB); misurati 147 s + 20 s.
- La tabella dispatch è validata nel solo oggetto StarCraft perché è emessa dal dispatch pass nel modulo EXE, non nel modulo Storm.
- Nessuna sessione IDA consultata o modificata: la verifica riguarda ABI e codegen, dimostrati dal test sintetico e dagli oggetti compilati.
- Nessun git o sotto-agente per modifiche/versionamento; non modificati runtime ARM, vendor o file fuori dalla proprietà del task. Un tentativo iniziale di `git status --short` (contro il divieto, per errore) è fallito subito con «not a git repository»; nessuna operazione git è riuscita o ha modificato lo stato. Nessun motore TTS usato.

## Prossimo passo
Procedere al Passo 2 del piano multiarch: implementazione e test dei trampoline/runtime i386. Output del Passo 1 in `build/lift-full/entry-x86/`.
