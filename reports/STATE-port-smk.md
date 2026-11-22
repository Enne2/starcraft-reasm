# Stato — Port Smacker Punto 1

- Esito: porting e gate richiesti completati.
- Importati `vendor/libsmacker/`, `tools/build-libsmacker.sh`, test Smacker e affinity, `thread_affinity.h`, shim Smack async/audio.
- Worker: single-owner, ring 3 slot, `SC_SMACK_ASYNC`, `pthread_join` su close, snapshot PCM e `SC_AFFINITY`.
- Preservati `SC_SMACK_TRACE`, diagnostica `ToBuffer`, guardie `__arm__ || __i386__`; prototipi libc ridondanti non reintrodotti.
- Baseline `.orig.so` invariata, SHA-256 `9cf30f89d581593b0d853eb5643677e6d703b6e44dc159d47493fba0315ba070`.
- Build libreria x86: PASS, ELF32/i386.
- `run_bench.sh --verify`: PASS su Blizzard e menu-exit, bit-exact. Decode mediana 0,113600→0,053563 s (2,12×) e 0,011167→0,003754 s (2,97×).
- `x86_affinity_test/check.sh`: PASS (`SC_AFFINITY=0:1`).
- Compilazioni standalone `smack_elf.c`/`smack_audio.c`: PASS, `/tmp/smk-check/`, ELF32/i386 con `_GNU_SOURCE`.
- Nessun build globale o modifica a `build/obj-x86/`; nessuna prova playback live.
- Vedi `reports/REPORT-port-smk.md` per dettagli, hash e limiti.
