# Stato finale x86 / multiarch

- Runtime consolidato: logica comune in `runtime/common/`, assembly in `runtime/arch/{x86,arm}/`.
- Aggiunti `tools/build.sh` e `runtime/README.md`; riferimenti in `tools/` e `tests/` aggiornati.
- Build x86 `tools/build.sh --arch x86 --skip-entry`: PASS; ELF32 EXEC Intel 80386.
- Test fastcc/runtime: PASS; dispatch map 102376 voci, 0 collisioni.
- ARM bridge generation e compilazione dei quattro assembly ARMv6: PASS.
- Wayland 5 s: finestra, PipeWire, SmackOpen, frame hash nonzero e FPS; timeout 124 atteso.
- STOP `0x1805F2D4` ancora fail-closed. Originale `0x00404F09` invoca callback dal ciclo on-exit; archi precedenti includono cleanup `0x004FDE70/7B`. Writer e causa iniziale (evento/ritorno) non provati; menu non certificato.
- Evidenze: `reports/REPORT-x86-final.md`, `build/qa/run-x86-final-wayland.log`, `build/qa/gdb-x86-polish.log`.

## Decisioni autonome

D19 preservato: nessun callback arbitrario o stub silenzioso. Nessuna modifica a `vendor/`, repository esterni, Git o input desktop.
