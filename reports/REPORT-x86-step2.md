# Report — Runtime x86-32, Passo 2

## Esito

Runtime i686 implementato in `runtime/x86/`, con gli adattamenti necessari dei sorgenti C condivisi, generatore multi-arch e script di build. Build oggetti runtime e micro-test nativo terminano entrambi con exit 0. Nessuna operazione Git o sotto-agente; directory vietate lasciate fuori dal lavoro.

## Modifiche

- `runtime/x86/indirect_dispatch_x86.S`: `reasm_indirect` salva l'ESP guest diagnostico, risolve `_ind_target`, gestisce il caso unresolved e tail-jumpa all'entry con ECX ripristinato e stack host al valore d'ingresso. `reasm_extern` invoca il target C cdecl con CPU, consuma la VA guest di ritorno e la risolve prima del tail-jump. Aggiunti i quattro stub per Storm ordinal 313/334 e gli initializer SSE2.
- `runtime/x86/com_bridge_x86.S`: 256 thunk COM, tabella e gate comune; bridge C, risoluzione della VA di ritorno e tail-dispatch.
- `runtime/x86/guest_abs_zero_x86.S`: simbolo `__fpmath` definito a zero con GAS i386.
- `runtime/arm/gen_bridge_stubs.py`: nuovo `--arch arm|x86` (default `arm`). L'output assembly ARM in modalità predefinita è stato confrontato byte per byte con `--arch arm`: identico. L'output x86 usa bridge stub che trasferiscono l'indice, chiamano il dispatcher C, risolvono il ritorno guest e tail-jumpano.
- `runtime/arm/gen_pe_image_bytes.py`: nuovo `--arch arm|x86`, default ARM; per i386 emette direttive GAS `.long` e omette `.syntax unified`. Il ramo ARM mantiene la generazione originale.
- `cpu_llasm.c`, `game_main_arm.c`: feature macro `_GNU_SOURCE` e registri `REG_EIP/REG_ESP/REG_EBP` su i386; LR esplicitamente zero su x86; rami ARM conservati. Aggiunto `<pthread.h>`, corretto il commento `-Wcomment`, selezione dell'header layout x86/ARM.
- `bridge_arm.c`: passaggio a `<time.h>`, `struct timespec` e `CLOCK_MONOTONIC` per clock e performance counter.
- `tools/build-runtime-x86.sh`: genera layout/PE bytes/ordinali/bridge stub e compila gli 11 helper LLASM, C runtime e assembly i386 in `build/obj-x86/runtime/`.
- `tests/x86_runtime_check/`: fixture e runner nativi che esercitano dispatch hit, ritorno C esterno, bridge stub generato e thunk COM; gli adattatori verificano la conservazione di EBX/ESI/EDI e i target controllano ECX.

## Verifiche effettuate

1. `python3 -m py_compile runtime/arm/gen_bridge_stubs.py` — PASS.
2. Generazione ARM predefinita contro `--arch arm` con `cmp` — output byte-identico.
3. Compilazione i686 `-fsyntax-only -Wall -Wextra` dei tre C modificati — PASS, zero warning.
4. `tools/build-runtime-x86.sh` — exit 0; **34 oggetti** verificati individualmente con `LC_ALL=C readelf -h` come ELF32 / Intel 80386. Include `guest_data.ld`, `guest_layout_x86.h`, `guest_pe_bytes.o`, `guest_pe_bytes_asm.o` e bridge stub sorgente/oggetto. Il generatore ha prodotto 9 gap PE e 327 stub (7 HAND, 288 SHIM, 32 UNK).
5. `tests/x86_runtime_check/check.sh` — exit 0 (`x86_runtime_check: PASS`). Test nativo i686 di `reasm_indirect`, `reasm_extern`, bridge generato e COM thunk.

## Limiti

- Non è stato eseguito il link del gioco completo né uno smoke test del gioco: questa missione valida runtime e oggetti, non l'intera integrazione di link.
- Il micro-test verifica i trampolini con fixture sintetiche; non sostituisce test di tutte le API bridge o delle 327 importazioni in esecuzione.
- Nessuna sessione IDA era aperta (`ida_list` vuoto); le modifiche seguono il contratto ABI già verificato nel Passo 1 e il codice runtime esistente. Nessuna analisi del binario originale è stata necessaria per queste modifiche di ABI/toolchain.

## Decisioni autonome

- Conservati i nomi storici `arm_*` già usati da C, tabelle e contratto di collegamento; i nuovi entry point specifici x86 hanno nomi espliciti `x86_*` dove previsto.
- `LR` nei log i386 è zero perché l'architettura non ha un link register; non è stata inventata un'equivalenza con EIP o con lo stack.
- Il layout generato mantiene il nome C `ArmGuestRegion` già emesso dal generatore; l'header è distinto come `guest_layout_x86.h` e non modifica il percorso ARM.
- Non modificati `runtime/arm/indirect_dispatch.S`, file in `vendor/`, `runtime/shim-port/`, oggetti di altri stage o le directory esplicitamente escluse. Nessuna operazione Git eseguita.
