# Verifica parità ARM (`--arch arm`)

## Esito

Audit su frigate del refactor `runtime/common/` + `runtime/arch/arm/`. Toolchain presente: `clang`, `opt`, `llc`, `llvm-nm`, `readelf`. **La pipeline integrata completa non è stata eseguita** perché `build/sysroot/` non esiste e `tools/build-entry-obj.sh --check` termina con `build-entry-obj: sysroot assente ... (stage 1)`. Il workspace non include il sysroot ARMv6 né `build/runtime-arm/game` e l'output ARM di lift.

## Verifiche positive

- Riferimenti nelle quattro pipeline esaminati: gli assembly specifici si trovano sotto `runtime/arch/arm/`, sorgenti/generatori sotto `runtime/common/`. Nessun riferimento attivo ai vecchi file in `runtime/arm/` nei percorsi controllati. `tools/build-entry-obj.sh` usa `runtime/common` e le coppie `arch/arm/tls.S`, `arch/arm/indirect_dispatch.S`, `arch/arm/com_bridge.S`.
- `gen_bridge_stubs.py --arch arm` ha generato 21 stub in `build/qa/gap-arm/bridge_stubs.S`.
- Compilati gli assembly disponibili `runtime/arch/arm/*.S` e gli stub: `com_bridge.S`, `guest_abs_zero.S`, `indirect_dispatch.S`, `tls.S`, `bridge_stubs.S`. Ogni oggetto è stato letto da `LC_ALL=C readelf -h`: `ELF32`, `ARM`.
- `dispatch-ir.py` è stato eseguito sugli IR reali in output privato: mappa 102.376 VA, 12.766 call-site indiretti riscritti, 12 basi tabella `.text` e 619 `ptrtoint` di alias `@_text` corretti complessivamente. L'IR Storm risultante ha superato `opt -passes=verify`, `opt -passes='default<O2>'`, quindi `llc` ARMv6. Oggetto verificato `ELF32 ARM`.
- Il tentativo `clang --target=armv6-unknown-linux-gnueabihf ... -ffreestanding -fsyntax-only runtime/common/indirect_dispatch.c` non passa senza sysroot: gli header host Fedora (`stdio.h` → `bits/floatn.h`) dichiarano mode `__TC__` e `__float128` non supportati per il target. Non è evidenza di un errore sorgente: la compilazione runtime normale usa `--sysroot=build/sysroot`.

## Verifiche bloccate / input mancanti

- `gen_game_layout.py` non è stato eseguibile: richiede gli oggetti entry e relativi IR; nel workspace sono presenti i `.ll`, ma non gli oggetti ARM d'ingresso. `build/lift-full/entry` non è presente.
- `gen_pe_image_bytes.py --arch arm` richiede righe `.guest_*` con sezioni/oggetti che descrivono il layout lifted; il tentativo con un linker script minimo isolato è stato rifiutato correttamente (“nessuna riga .guest_*”). Non è stato fabbricato un layout d'ingresso non rappresentativo.
- `gen_storm_ordinals.py` richiede `analysis/storm_ordinals.json`; il file non è presente in questo workspace (lo script di stage 4 lo cerca in `$SC_REASM/analysis/`).
- Non compilati i C runtime comuni: mancano header e librerie ARM del sysroot; inoltre `build-entry-obj.sh` non può partire oltre il check prerequisiti.
- `guest_pe_bytes.S` non generato, dunque non compilato.

## Percorsi / opzioni e documentazione

`tools/build.sh --arch arm` sceglie correttamente entry ARM, stage 4 runtime e `link-arm.sh`; linka con `ENTRY_DIR` e `RUNTIME_OBJ_DIR` (quest'ultimo è supportato dal linker). Tuttavia il build entry script mantiene fisso `P=$ROOT/build/lift-full/entry`: non offre un override privato `ENTRY_DIR`. La pipeline integrata, quindi, non è sicura per un'esecuzione completa isolata semplicemente impostando `ENTRY_DIR`; non è stata lanciata per evitare di scrivere fuori da `build/qa/gap-arm/`.

## Decisioni autonome

- Non creare un sysroot incompleto o derivare layout PE fittizi: avrebbero dato una prova fuorviante. È conservata la separazione dell'output ARM in `build/qa/gap-arm/`.
- Non ho modificato `tools/build.sh`: risolvere la scrittura fuori dalla directory QA richiede supportare coerentemente un override d'output nel build-entry e propagare lo stesso percorso al linker; non era necessario per le prove eseguibili e avrebbe ampliato il cambiamento oltre una correzione puntuale.

## File di prova

Oggetti e IR generati in `build/qa/gap-arm/`; nessun output in `build/starcraft-x86` o `build/obj-x86/`. Nessun file `.c` dello shim/runtime è stato modificato.
