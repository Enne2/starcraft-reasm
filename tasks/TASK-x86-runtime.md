# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, layout, registri, vtable o convenzioni del binario originale, l'oracolo primario sono i database IDA in `build/lift-full/StarCraft.orig.exe.i64` e `build/lift-full/storm.orig.dll.i64`. Prima di usare tool IDA controlla sempre `ida_list` e NON toccare sessioni IDA aperte per altri lavori.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/shim-port/`, `build/lift-full/entry-x86/` né `build/obj-x86/{llasm,shim,shim-redef}/`.
   - NON leggere `build/lift-full/*.ll` per intero.
   - NON rompere la compilazione/comportamento ARM di `runtime/arm/` (`--arch arm` in `gen_bridge_stubs.py` deve rimanere identico al comportamento attuale).

---

# Missione `scx86-runtime`: Passo 2 — Runtime x86-32 (`runtime/x86/`), adattamenti `#ifdef __i386__` e compilazione oggetti runtime

Leggi prima:
- `PLAN-MULTIARCH.md` (sezioni 3.3, 6 Passo 2)
- `reports/STATE-x86-ir.md` e `reports/REPORT-x86-step1.md` (contratto `fastcc` + `musttail` su `i686` verificato al Passo 1!)
- `reports/STATE-x86-shim.md` e `reports/REPORT-x86-shim.md` (checklist riga-per-riga di `runtime/arm/*.c`)

## File di proprietà (esclusivi di questo task)
- `runtime/x86/` (nuova cartella: `indirect_dispatch_x86.S`, `com_bridge_x86.S`, `guest_abs_zero_x86.S`, eventuale `README.md`)
- `runtime/arm/gen_bridge_stubs.py` (aggiunta `--arch arm|x86`, default `arm` invariato)
- `runtime/arm/cpu_llasm.c`, `runtime/arm/game_main_arm.c`, `runtime/arm/bridge_arm.c` (adattamenti puntuali `#ifdef __i386__` / `#elif defined(__arm__)`, più pulizia warning/prototipi indicata nell'audit)
- `runtime/arm/gen_pe_image_bytes.py`, `runtime/arm/gen_game_layout.py` (solo se serve adattamento per ELF32 i386 senza rompere ARM)
- `tools/build-runtime-x86.sh` (nuovo script ispirato a `tools/build-runtime-arm.sh` che produce tutti gli oggetti runtime i686 in `build/obj-x86/runtime/` riusando `build/lift-full/entry-x86/` e `build/obj-x86/{llasm,shim-redef}/`)
- `build/obj-x86/runtime/` (oggetti compilati, `guest_data.ld`, `guest_pe_bytes.o`, `guest_layout_x86.h` o `guest_layout_arm.h`, `bridge_stubs.S/.o`, `storm_ordinals.c/.o`)
- `tests/x86_runtime_check/` (piccolo test di collegamento e esecuzione nativa dei trampolini `reasm_indirect`, `reasm_extern`, `x86_bridge_common`, `com_bridge_x86`)
- `reports/REPORT-x86-step2.md`
- `reports/STATE-x86-runtime.md` (max 70 righe)

## Dettaglio tecnico critico (dal Passo 1)
- `fastcc void (%_cpu*)` su `i686-unknown-linux-gnu`:
  - `%_cpu*` arriva in **`%ecx`** (caller-saved in SysV i386!).
  - `musttail` emette `jmp`: il return address verso il chiamante C resta in cima allo stack `(%esp)`.
  - Callee-saved SysV i386: `%ebx, %esi, %edi, %ebp, %esp`. Caller-saved: `%eax, %ecx, %edx`.
  - Quindi in ogni trampolino (`reasm_indirect`, `reasm_extern`, `x86_bridge_common`, `arm_com_bridge_common`):
    - **NON lasciare frame sullo stack** prima di `jmp *%eax`: l'eventuale `push`/`sub` locale usato per salvare `%ecx` (e chiamare funzioni C `cdecl`) va **completamente ripristinato** prima del `jmp *%eax` finale, così che `(%esp)` sia esattamente il return address originale e `%ecx` contenga di nuovo `%_cpu*`!
    - Preserva sempre i registri callee-saved (`%ebx, %esi, %edi, %ebp`) se li usi.
    - Verifica anche le chiamate dirette `call` verso C `cdecl`: in SysV i386 molti tool/compiler assumono stack 16-byte aligned opzionale o 4-byte aligned con `-m32`; se fai `pushl arg` su stack non allineato va bene su i386 Linux/glibc (SysV i386 base usa allineamento a 4 byte, SSE usa 16 byte se abilitato; verifica con `clang -m32` o evita SSE non allineato). Verifica nel micro-test reale!

## Obiettivi e ordine di esecuzione

1. **Ispeziona gli equivalenti ARM**:
   - `runtime/arm/indirect_dispatch.S`, `runtime/arm/indirect_dispatch.c`, `runtime/arm/indirect_dispatch.h`
   - `runtime/arm/com_bridge_arm.S`, `runtime/arm/com_bridge_arm.c`
   - `runtime/arm/guest_abs_zero.S`
   - `runtime/arm/gen_bridge_stubs.py`
   - `tools/build-runtime-arm.sh` (per sapere ogni singolo file, `-D`, `-I`, generatore, e ordine di compilazione!)

2. **Implementa `runtime/x86/`**:
   - `runtime/x86/indirect_dispatch_x86.S`:
     - `reasm_indirect`: leggi `0x10(%ecx)` (`_esp`) -> salva in `0x8c(%ecx)` (`_ind_esp`, offset 140), leggi `0x88(%ecx)` (`_ind_target`, offset 136); salva `%ecx` (es. in `%ebx` dopo `pushl %ebx`, oppure sullo stack), chiama `reasm_indirect_resolve(target)` (`cdecl`), se `0` chiama `reasm_indirect_unresolved(cpu, target)`; altrimenti metti `target_fn` in `%eax`, ripristina `%ecx = cpu` e qualsiasi registro callee-saved salvato (`popl %ebx` ecc.) in modo che `(%esp)` torni ad essere il return address del chiamante, e fai `jmp *%eax`.
     - `reasm_extern`: verifica esattamente la semantica di `reasm_extern` in `runtime/arm/indirect_dispatch.S`! (Su ARM: riceve target C in `r12`, `cpu` in `r0`; come entra `reasm_extern` su x86? Controlla `storm_313_arm`, `storm_334_arm`, `__sse2_available_init`, `__sse2_available_init_storm` e `gen_bridge_stubs.py` / `dispatch-ir.py` per vedere come viene chiamato `reasm_extern`!). Implementa esattamente lo stesso contratto su x86 (es. target C in `%eax` o `%edx`, `cpu` in `%ecx`, chiama `target(cpu)` `cdecl`, poi `cpu_llasm_pop32(cpu)` per il return VA guest, poi `reasm_indirect_resolve(ret_va)` / unresolved, ripristina `%ecx = cpu` e lo stack originale e `jmp *%eax`).
     - `storm_313_arm`, `storm_334_arm`, `__sse2_available_init`, `__sse2_available_init_storm`: stub equivalenti x86 che caricano il puntatore alla funzione `*_body` e saltano a `reasm_extern`.
   - `runtime/x86/com_bridge_x86.S`:
     - Equivalente x86 di `com_bridge_arm.S`: 256 thunk `arm_com_thunk_0..255` + `arm_com_thunk_table` + `arm_com_bridge_common`. Verifica come `com_bridge_arm.c` / `com_bridge_arm.S` ricevono `cpu` (in `%ecx`? O `cdecl`? Controlla `com_bridge_arm.S` e `com_bridge_arm.c`!).
   - `runtime/x86/guest_abs_zero_x86.S`:
     - `.globl __fpmath` / `.set __fpmath, 0` in sintassi GAS i386.

3. **Aggiorna `runtime/arm/gen_bridge_stubs.py` con `--arch arm|x86`**:
   - Mantieni `--arch arm` come default e verifica con `diff` che l'output con `--arch arm` sia byte-identico a prima.
   - Con `--arch x86` emetti sintassi GAS i386:
     - `x86_bridge_common` (equivalente di `arm_bridge_common`: riceve `cpu` in `%ecx` e `idx` in `%eax` o `%edx`, salva callee-saved, chiama `arm_bridge_common_c(cpu, idx)`, ripristina `%ecx = cpu` e stack originale, fa `jmp *%eax`).
     - Tutti gli stub `bridge_<sym>` che impostano l'indice e saltano a `x86_bridge_common`.
     - Verifica se `gen_bridge_stubs.py` gestisce `bridge_table.def`, `bridge_dynamic.def`, `bridge_unk.def`, `bridge_shims.def` — mantieni tutte le tabelle identiche!

4. **Adatta i `.c` condivisi (usando `#ifdef __i386__` / `#elif defined(__arm__)`)**:
   - `runtime/arm/cpu_llasm.c`: in `cpu_signal`, aggiungi `#define _GNU_SOURCE` prima degli header di sistema e ramo `#ifdef __i386__` che legge `uc->uc_mcontext.gregs[REG_EIP]` (e gestisci esplicitamente l'assenza di `LR`, es. `lr=0` o omissione nel log i386).
   - `runtime/arm/game_main_arm.c`: `_GNU_SOURCE` in testa, `#ifdef __i386__` in `sc_prof_handler` per `REG_EIP`, `REG_ESP`, `REG_EBP` (e `lr=0`), correggi commento `/*.ll` alla riga 44 (`-Wcomment`), usa `<pthread.h>` se pulito.
   - `runtime/arm/bridge_arm.c`: usa `#include <time.h>`, `struct timespec`, `clock_gettime(CLOCK_MONOTONIC, &ts)` in modo portabile (o `#ifdef __i386__`).
   - Verifica che tutti i `.c` del runtime compilino senza errori (e con zero nuovi warning) con `clang --target=i686-unknown-linux-gnu -m32 -fno-pie`.

5. **Crea `tools/build-runtime-x86.sh` e verifica la compilazione completa di `build/obj-x86/runtime/`**:
   - Replica tutti gli stage di `tools/build-runtime-arm.sh` relativi al runtime (generatori `gen_pe_image_bytes.py`, `gen_game_layout.py`, `gen_storm_ordinals.py`, `gen_bridge_stubs.py --arch x86`, e compilazione di tutti i `.c` e `.S` di runtime + `guest_pe_bytes.o` + `guest_data.ld`).
   - Verifica se `gen_pe_image_bytes.py` e `gen_game_layout.py` funzionano invariati su `StarCraft.entry.o` / `storm.entry.o` x86; se hanno path/flag ARM hardcoded, parametrizzali pulitamente senza rompere ARM.
   - Scrivi e esegui `tests/x86_runtime_check/` per testare in un piccolo binario nativo i686: `reasm_indirect` (hit e sentinel), `reasm_extern`, un `bridge_stub` generato da `gen_bridge_stubs.py --arch x86`, e un thunk di `com_bridge_x86.S`.

6. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-step2.md` e `reports/STATE-x86-runtime.md` (max 70 righe).

## Gate di accettazione (oggettivi)
1. `gen_bridge_stubs.py --arch arm` produce output byte-identico al comportamento precedente; `gen_bridge_stubs.py --arch x86` genera `bridge_stubs.S` i386 valido.
2. `tools/build-runtime-x86.sh` termina con exit code `0` e produce in `build/obj-x86/runtime/` tutti gli `.o` runtime ELF32 `Intel 80386` (verificati con `LC_ALL=C readelf -h`) + `guest_data.ld` + `guest_pe_bytes.o`.
3. `tests/x86_runtime_check/` compila ed esegue nativamente su `frigate` con exit code `0`, dimostrando `reasm_indirect`, `reasm_extern`, `bridge_stub` e `com_bridge_x86` con conservazione di `%ecx` e callee-saved e zero stack leak.
4. `reports/REPORT-x86-step2.md` e `reports/STATE-x86-runtime.md` presenti e completi.
