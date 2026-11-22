# Report — Porting GL/GLES2 e build tools

## Esito

Integrato il presenter OpenGL/GLES2 8-bit palette shader e le modifiche selezionate a build/link da `starcraft-reasm-smacker` (`b77bf0d..HEAD`), senza lanciare il build completo.

## File aggiornati

- `runtime/shim-port/elf/host_elf.c`: importati `SC_PRESENT=gl`, shader GLES2, `SC_GL_VERIFY`, fallback GL → GPU → CPU.
- `tools/build.sh`, `build-entry-obj.sh`, `build-runtime-arm.sh`, `build-runtime-x86.sh`, `build-shim-x86.sh`, `link-arm.sh`, `link-x86.sh`: profili ARMv6/ARMv7, `storm_scomp`, `_GNU_SOURCE` nel parsing bridge e fix layout/link x86.
- `tests/x86_gl_palette_test/check.sh`: copiato dal repository sorgente.

## Salvaguardie verificate

- `host_elf.c` conserva il parsing incrementale `SC_CONTROL_FILE` con `lseek` e la soppressione di `mouse_poll_global()` quando è attivo uno tra `SC_CONTROL_FILE`, `SC_INPUT_SCRIPT`, `SC_INPUT_REPLAY` e `SC_NO_GLOBAL_MOUSE`.
- Preservati l’override `runtime/common/llasm_cmps.c` in tutte le pipeline interessate, `win32_shell_dlg` in `build-shim-x86.sh` e `rm -f "$OUT"` in `link-x86.sh`.
- `storm_crt_unlifted.c` è presente nel runtime, ma non risultava già incluso nella lista di compilazione: non ho aggiunto un’inclusione non richiesta dal diff upstream.

## Verifiche

- `bash -n` su tutti e sette gli script aggiornati: **PASS**.
- `./tests/x86_gl_palette_test/check.sh`: **FAIL**, exit 1. Il test è partito con Wayland e ha atteso 6 secondi (processo terminato da `timeout`, rc 124) senza trovare `SC_GL_VERIFY PASS: 307200 pixel esatti`. Il log `build/qa/gfx/gl-palette-test.log` mostra attività Smacker ma nessuna conferma GL. Non ho ricostruito `build/starcraft-x86`: il build completo è escluso dal task in corso, quindi non posso attribuire il mancato readback a codice aggiornato o a un binario preesistente.
- Nessuna prova di build/link end-to-end eseguita.

## Decisioni autonome

Ho mantenuto il vincolo di non eseguire `tools/build.sh` e non ho tentato di aggirare il test ricostruendo il binario. Il risultato GL resta da verificare dopo la build prevista, riutilizzando lo stesso gate.
