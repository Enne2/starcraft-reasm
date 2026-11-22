# Stato — opt-host

- **Completato:** software GL escluso in auto-select; fallback SDL 2D/CPU pulito; `SC_PRESENT=gl` esplicito preservato.
- **Completato:** snapshot canvas/palette e dedup ravvicinato; `SC_DEDUP_PRESENT=0` disabilita; expose/resize/fullscreen/refresh forzano il redraw; `SC_GL_VERIFY` bypassa il dedup.
- **Completato:** `SC_FPS_CAP` opzionale senza sleep e senza throttling di pump/input.
- **Preservati:** upload GL palette solo su `g_pal_dirty`; `SC_CONTROL_FILE` e soppressione `mouse_poll_global()` invariati.
- **Build:** C11 i386 e x86-64 PASS. Gli avvisi Clang sono invariati rispetto a HEAD. Oggetti/log in `build/qa/opt-host/`.
- **Test:** `git diff --check`, `bash -n tests/x86_gl_palette_test/check.sh` PASS. Gate pixel runtime non eseguito: richiede il binario vietato dal task; non dichiarare PASS dei 307200 pixel.
- **File modificati:** `runtime/shim-port/elf/host_elf.c`; report `reports/REPORT-opt-host.md`, `reports/STATE-opt-host.md`.
- **Non toccati:** build globale, `build/starcraft-x86`, directory test; nessun sotto-agente. Modifiche simultanee preesistenti in altri file di lavoro lasciate intatte.
