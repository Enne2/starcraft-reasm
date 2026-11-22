# Stato — port-gfx-tools

- Integrazione upstream: host_elf GL/GLES2 e aggiornamenti `tools/build*.sh`, `tools/link*.sh` applicati.
- Presenter: `SC_PRESENT=gl`, palette shader 8bpp, `SC_GL_VERIFY`, fallback GL → GPU → CPU.
- Arm: profili armv6/armv7; `storm_scomp` incluso nei due stage del runtime ARM e nel runtime x86.
- Preservati: `SC_CONTROL_FILE` incrementale, soppressione mouse globale per input controllato, override `llasm_cmps.c`, `win32_shell_dlg`, rimozione preventiva dell’output nel link x86.
- Test sintattico `bash -n` sui 7 script modificati: PASS.
- Gate `tests/x86_gl_palette_test/check.sh`: FAIL; timeout rc 124 e marker di readback GL assente. Log: `build/qa/gfx/gl-palette-test.log`.
- Nessun build completo, link o rebuild del binario eseguito.
- Reports: `reports/REPORT-port-gfx-tools.md`.
- Decisione autonoma: rimandare il test GL definitivo a una build autorizzata dal flusso di lavoro; non usare il binario non verificato come prova del codice appena integrato.
