# Report finale — scx86-polish

## Esito

- Refactor completato: `runtime/common/` contiene runtime C/H, generatori, `.def`, patch, include e test; `runtime/arch/{x86,arm}/` contiene gli assembly specifici.
- Aggiunti `tools/build.sh` e `runtime/README.md`; aggiornati i riferimenti degli script in `tools/` e `tests/`. `rg -n 'runtime/(arm|x86)' tools/ tests/` non trova path obsoleti.
- `tools/build.sh --arch x86 --skip-entry` completa build e link con exit 0.
- Gate x86 fastcc, runtime, ELF, dispatch map e gate ARM richiesti passano.
- Smoke reale Wayland: finestra, renderer GPU, PipeWire, SmackOpen, DoFrame con hash nonzero e FPS; timeout previsto 124 dopo 5 s.

## Parte A — callback on-exit

IDA non ha aperto `build/lift-full/StarCraft.orig.exe.i64`; ho usato il PE originale con `llvm-objdump` (come consentito dal task) e GDB sul nuovo ELF.

- `StarCraft.orig.exe:0x00404F09–0x00404F26`: carica/decrementa il puntatore `0x006DD684`, legge la callback e la chiama indirettamente (`call *%eax` a `0x00404F24`), poi ripete il ciclo a `0x00404F26`.
- Al fail-closed effettivo, ritorno guest `0x00404F26`, target `0x1805F2D4`, EAX=`0x1805F2D4`, ESP=`0x021FFE30`. Il port `.text` è `0x18000000..0x1803A435`; il target è fuori dal codice caricato, senza simbolo nell’ELF e non è una VA guest valida. Nessun target è stato aggiunto.
- I precedenti archi includono `0x004FDE70` e `0x004FDE7B`. Il disassemblato originale a `0x004FDE70` chiama `0x00401970`, che scollega e azzera nodi di una lista collegata da `[EBX+8]`; `0x004FDE7B` legge i globali `0x0051A338/0x0051A33C`. È evidenza di cleanup, non prova che un evento SDL `WM_CLOSE`/`SDL_QUIT`, `Dlg`, `SNet` o un normale ritorno abbia avviato l’uscita.
- GDB ha osservato l’inizializzazione di `0x006DD684` a un buffer runtime `0x20180c4c`; la prova non ha isolato lo store della callback specifica dentro quel buffer. La provenienza di `0x1805F2D4` e l’evento causale restano non determinati. Il resolver D19 resta fail-closed.
- Log: `build/qa/gdb-x86-polish.log`. Nota: i tentativi GDB hanno richiesto timeout; nessuna modifica sorgente diagnostica è stata mantenuta.

## Parte B — struttura/build

- File spostati in `runtime/common/` e `runtime/arch/{x86,arm}/`; i generatori comuni mantengono l’opzione `--arch`.
- Compatibilità aggiornata: `tools/build-runtime-{x86,arm}.sh`, `tools/build-shim-x86.sh`, `tools/build-entry-obj.sh`, `tools/link-arm.sh`, `tests/x86_runtime_check/check.sh` e relativi riferimenti.
- Nuovo `tools/build.sh --arch <x86|arm> [--skip-entry] [--no-link]`; modalità x86 invoca entry (se richiesto), shim, runtime e link. Modalità ARM usa entry `--no-smoke`, stage runtime 4 e link.
- Guida sintetica in `runtime/README.md` per target 32-bit e percorso 64-bit.

## Parte C — verifiche

- `tools/build.sh --arch x86 --skip-entry`: PASS (exit 0); 35 oggetti runtime e 12 shim i386 linkati.
- `tests/x86_fastcc_check/check`: PASS. `tests/x86_runtime_check/check.sh`: PASS.
- `readelf -h build/starcraft-x86`: ELF32, EXEC, Intel 80386. `dispatch-ir.py --dump-map`: 102376 voci univoche, zero collisioni.
- Bridge ARM generati da `runtime/common/gen_bridge_stubs.py --arch arm`: PASS. I quattro `.S` in `runtime/arch/arm/` compilano individualmente con clang `armv6-unknown-linux-gnueabihf`.
- Smoke reale `build/qa/run-x86-final-wayland.log`: exit 124 previsto; Wayland finestra creata, audio PipeWire 22050 Hz stereo, `SmackOpen`, `DoFrame` hash nonzero (`D7570F54`, `D15C48A1`, `238FA8DD`) e FPS. Il fail-closed si ripresenta al target `0x1805F2D4`; il menu principale non è certificato.

## Decisioni autonome

Ho mantenuto D19: senza identificare e validare il writer non ho modificato callback, resolver o handling eventi. Il menu resta dichiarato non verificato. Nessun Git, sotto-agente, input desktop o modifica a `vendor/`/repo esterni.
