# Stato — x86 IR (Passo 1)

## Fatto e verificato
- Datalayout i686 derivato da clang 22.1.8: puntatori 32-bit, little-endian.
- Micro-test fastcc/musttail eseguito nativamente su frigate, exit 0.
- ABI osservata: fastcc `%_cpu*` in `%ecx`; cdecl da stack; musttail `jmp`, senza frame; return address del caller conservato.
- `tools/build-entry-x86.sh` eseguito con exit 0; ottimizzazione `default<O1>`.
- `StarCraft.entry.o`, `storm.entry.o`: ELF32, REL, Intel 80386; simboli e disassembly campione controllati.
- Tempi: StarCraft 147 s, Storm 20 s. Dimensioni: 15.485.272 / 3.323.136 byte.
- Report dettagliato: `reports/REPORT-x86-step1.md`.

## File di proprietà
- `build/layout-i686.ll`
- `tests/x86_fastcc_check/`
- `tools/build-entry-x86.sh`
- `build/lift-full/entry-x86/`
- `reports/REPORT-x86-step1.md`
- `reports/STATE-x86-ir.md`

## Comandi che funzionano
- Micro-test: `tests/x86_fastcc_check/check`
- Build/verifica oggetti: `tools/build-entry-x86.sh`
- Header ELF: `readelf -h build/lift-full/entry-x86/{StarCraft,storm}.entry.o`
- Simboli: `llvm-nm build/lift-full/entry-x86/StarCraft.entry.o`
- Disassembly: `llvm-objdump -d --disassemble-symbols=loc_404C21,sc_loc_404C21_impl build/lift-full/entry-x86/StarCraft.entry.o`

## Aperto / prossimo passo
- Passo 2: runtime/trampoline i386, bridging fastcc↔cdecl, dispatch indiretta.
- Gli oggetti sono relocatable; non è stata effettuata la build/link del runtime i686.

## Trappole imparate
- Chiamare fastcc direttamente da C cdecl non passa l'argomento nel registro previsto: usare un wrapper C ABI che trasferisce il puntatore.
- LLVM IR richiede costanti i32 decimali per il sentinel; `0x...` viene interpretato come float e fallisce.
- In bash `local x="$1" y="$x"` con `set -u` può espandere `x` prima dell'assegnazione: assegnare in righe separate.
- `dispatch-ir.py` emette la tabella VA→entry nell'oggetto StarCraft; non aspettarsi `reasm_va_map_*` in Storm.
- `readelf` è localizzato: per controlli di script usare `LC_ALL=C`.
