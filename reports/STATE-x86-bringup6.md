# Stato x86 bring-up 6 — frigate

- Build shim/runtime/link + `tests/x86_runtime_check/check.sh`: PASS. ELF32/EXEC/i386; dispatch map 102376 voci, 0 collisioni.
- **Causa SIGSEGV `0x56F24`:** `WaitForMultipleObjects` x86 dereferenziava l'RVA guest in `0x15045264` come puntatore host; GDB ha verificato valore `0x56F24`, chiamata `w1` con handle evento `0x5E000054`.
- **Fix:** su i386 WFMO chiama `WaitForSingleObject` locale e tratta gli handle thread guest `0x72...` con `arm_guest_thread_join`; logica parallela all'ARM.
- `SC_SKIP_INTRO=1` è la variabile corretta; il filtro ignorava la stringa host `smk\\blizzard.smk` perché usava solo `arm_va_is_mapped`. Ora convalida anche le mappe host leggibili tramite `proc_range`; GDB conferma ritorno `1`.
- Smoke finale (`run-x86-bringup6-smoke.log`): nessun `0x56F24`; SMK menu e FPS ~329–338 raggiunti. Dopo 60 s exit 124 per STOP fail-closed su `0x1805F2D4`, caller `0x00404F26`; non aggiunto target e non allentato resolver.
- SDL reale X11 tentata 5 s e 15 s: solo `[host] init: video driver = x11`, exit 137, nessuna finestra/audio/FPS; non PASS. DISPLAY `:0`; `xdpyinfo` PASS.
- File sorgente modificati: `runtime/shim-port/elf/win32_shim.c`, `runtime/arm/storm_sdraw_arm.c`. Log e dettagli in `reports/REPORT-x86-bringup6.md`.

## Decisioni autonome

Fix limitati a cause provate; fail-closed/D19 intatti. Nessun input desktop.

## Prossimo passo

Eseguire il comando GDB sotto `## Prossimo passo` in `reports/REPORT-x86-bringup6.md` per diagnosticare il blocco SDL a `[host] init`; poi rilanciare il comando X11 `timeout -k 1s 5s` riportato lì e verificare finestra, backend audio reale e FPS.
