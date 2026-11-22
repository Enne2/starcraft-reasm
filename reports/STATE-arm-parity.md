# Stato ARM parity
- Refactor path audit: pass; sorgenti ARM in `runtime/arch/arm`, codice/generatori comuni in `runtime/common`.
- Bridge stubs ARM: generati (21); assembly disponibili compilati e verificati ELF32 ARM.
- `dispatch-ir.py` su entrambi gli IR: 102376 VA, 12766 siti indiretti; Storm verify + O2 + llc ARM pass.
- Full `build-entry-obj.sh --check`: bloccato, sysroot ARM assente.
- Generatori game-layout/PE-bytes/ordinali: incompleti per mancanza oggetti entry, linker layout e storm_ordinals.json.
- C runtime senza sysroot fallisce nell'header glibc host (`__TC__`, `__float128` non supportato); non indica un difetto sorgente.
- Nessun cambiamento a shim/runtime C, binari x86 o `tools/build.sh`.
- Dettagli e comandi: `reports/REPORT-arm-parity.md`.
