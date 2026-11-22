# STATE — Pinebook Pro ARMv7
- Build ARMv7: **PASS** (`build/starcraft-armv7`, ELF32 ARM EABI5 hard-float, dump-map 102392/0 collisioni).
- Script nuovi: `tools/build-entry-arm.sh`, `tools/build-runtime-armv7.sh` (sorgenti del repo, non patch sc-reasm).
- Sysroot armhf Jammy in `build/sysroot` (ricreato senza root da ports.ubuntu.com).
- **Fix critico**: path degli oggetti nel `.ld` di link ARM (symlink farm) — prima le sezioni
  `.guest_storm_rdata`/`.guest_storm_data` finivano nel `.data` del port e il gioco crashava a 0x1505FC20.
- Sul Pinebook: si avvia, resta vivo >60 s, FPS 23-24 @640x480 (14-20 in fullscreen 1280x960), CPU ~74%, RSS 160-222 MB.
- **Aperto**: finestra nera su PBP con tutti i presenter (gl/gpu/cpu) — indagare palette/canvas ARM e swap Xwayland.
- Report: `reports/REPORT-pbp-armv7.md`.
