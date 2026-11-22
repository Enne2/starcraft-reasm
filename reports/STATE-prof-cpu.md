# Stato — Profiling CPU

- Profilatore ELF32 compilato: `build/qa/prof-cpu/prof_cpu.so`; wrapper dedicato `starcraft-prof-cpu.sh` usa `LD_PRELOAD` solo sul gioco i386.
- Run isolati completati: Boot, Menu animato e Gameplay Boot Camp. Campioni validi: 1.223 Boot, 3.634 Menu, 2.724 Gameplay; nessun crash. Profiler 1 kHz per-thread `CLOCK_THREAD_CPUTIME_ID`/`SIGEV_THREAD_ID`.
- CPU processo stimata: Boot 45,2%, Menu 108,7%, Gameplay 220,0% (percentuale per core). Nel gameplay i worker llvmpipe assorbono circa 188,5% aggregato contro 30,5% del thread principale del gioco.
- FPS osservati nel log: Boot 6–10 startup e 282–320 dopo; Menu 252–300; Gameplay 380–610. Xvfb usa llvmpipe, perciò FPS non sono rappresentativi di un renderer GPU hardware.
- Hotspot campionati: `storm_334_arm_body` (34,89% Menu / 18,65% Gameplay), `proc_range`/`arm_shadow_check_range`, codice `loc_*` Storm di lettura bit/decoding token e `memcpy`. I guest più caldi sono stati ispezionati in IDA su copie identiche dei binari originali dentro `build/qa/prof-cpu/`; nomi e commenti salvati nelle IDB QA.
- Screenshot gameplay verifica mappa attiva e unità; non dimostra raccolta risorse riuscita. Nessuna modifica a runtime/tools/vendor/tests/build/starcraft-x86. Nessun A/B SC_PRESENT/SC_SMACK_ASYNC eseguito.
- Tutte le istanze `profcpu-*` fermate; sessioni IDA create per questa attività chiuse. Un'istanza indipendente `prof-mem` era presente durante la raccolta e non è stata alterata.
- Report completo, breakdown 25 hot spot/fase e limiti: `reports/PROF-CPU.md`.
