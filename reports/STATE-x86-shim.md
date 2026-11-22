# Stato — audit shim i686

## Esito
- Build `tools/build-shim-x86.sh`: PASS.
- 11 helper LLASM + 12 shim + 12 copie ridefinite: 35/35 `ELF32 / Intel 80386` (`readelf -h`).
- `llvm-objcopy --redefine-syms`: PASS su tutti i 12 shim.
- `llvm-nm`: 291 simboli globali `__c`, 0 collisioni globali, 0 riferimenti indefiniti ARM-specifici.
- AST i686: 291 SHIM, 32 UNK. Nessuna differenza SHIM per arità/pops/variadic; rename set identico (291). Il file esistente ha un UNK legacy in più: `crt_unlifted_15040E07`, coperto anche dall’entry manuale `bridge_table.def:90`.

## Artefatti
- Script: `tools/build-shim-x86.sh`
- Oggetti: `build/obj-x86/{llasm,shim,shim-redef}/`
- Manifest/diff/AST: `build/obj-x86/shim_check/`
- Report completo: `reports/REPORT-x86-shim.md`

## Audit runtime ARM
- 12/14 `.c` top-level compilano in syntax-only i686. I due blocchi sono `cpu_llasm.c:223,227` e `game_main_arm.c:108-110`: campi `ucontext` ARM (`arm_pc/lr/sp/fp`).
- Warning `-Wcomment`: `game_main_arm.c:44` (`/*.ll` nel commento).
- `bridge_arm.c:24-34,207`: clock/timespec dichiarati a mano con macro `_ARM`; usare API `<time.h>` condivise.
- Nessun `struct stat` e nessun inline assembly ARM nei `.c`. `tests/sysroot_probe.c` è volutamente ARM hard-float.

## Decisioni autonome
- Sorgenti `runtime/arm`, `runtime/shim-port/elf` e `vendor/` lasciati invariati; workaround balanced-parentheses confinato allo script nuovo perché il parser AST corrente usa `rindex(')')` e sbaglia su `stdcall` i386.
- Un `git status` iniziale non necessario è fallito (“non è un repository Git”), senza modifiche; nessun altro comando Git eseguito.

## Prossimo passo
Portare i due handler `ucontext` a `REG_EIP/REG_ESP/REG_EBP` con `_GNU_SOURCE` e scelta esplicita per il campo LR; quindi normalizzare clock/pthread e implementare i trampolini x86 del Passo 2 prima del link completo.
