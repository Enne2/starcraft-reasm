# Preludio obbligatorio
- Oracolo primario: IDA / `storm.orig.dll`, sorgenti in `~/dev/starcraft-reasm-smacker/`.
- **Proprietà esclusiva di questo agente (NON toccare file altrui):**
  - `vendor/libsmacker/`
  - `tools/build-libsmacker.sh`
  - `tests/smacker_bench/`, `tests/smacker_bench_*.smk`, `tests/x86_affinity_test/`
  - `runtime/common/thread_affinity.h`
  - `runtime/shim-port/elf/smack_elf.c`
  - `runtime/shim-port/elf/smack_audio.c`, `runtime/shim-port/elf/smack_audio.h`
- **NON modificare `tools/build*.sh` (tranne `build-libsmacker.sh`), `host_elf.c`, `win32_shim.c`, `indirect_dispatch*`, `build/starcraft-x86`, e NON eseguire `tools/build.sh` globale.**
- NON usare `vendor/sr/` e NON lanciare altri sottoagenti.

# Missione: Porting Punto 1 (Smacker bit-reservoir/LUT + async worker) + `thread_affinity.h`

Leggi `/home/enne2/dev/starcraft-reasm-smacker/HANDOFF-PORTING.md` (sezioni 1 e 4.2).

1. **Copia & Valuta `libsmacker`, `build-libsmacker.sh`, `thread_affinity.h` e test:**
   - Copia `~/dev/starcraft-reasm-smacker/vendor/libsmacker/`, `tools/build-libsmacker.sh`, `tests/smacker_bench/`, `tests/smacker_bench_*.smk`, `tests/x86_affinity_test/`, e `runtime/common/thread_affinity.h` in questo repo.
   - Verifica che `build/i386-libs/libsmacker-1.2.0.orig.so` sia intatto (già salvato!).
   - Esegui `./tools/build-libsmacker.sh --arch x86` per rigenerare `build/i386-libs/libsmacker-1.2.0.so`.
   - Esegui `./tests/smacker_bench/run_bench.sh --verify` (confronto bit-exact vs `.orig.so` su `blizzard.smk` e `menu-exit.smk`!) e `./tests/x86_affinity_test/check.sh`.
2. **Merge accurato di `smack_elf.c` e `smack_audio.{c,h}`:**
   - Confronta con `diff -u`:
     - `runtime/shim-port/elf/smack_elf.c` vs `~/dev/starcraft-reasm-smacker/runtime/shim-port/elf/smack_elf.c`
     - `runtime/shim-port/elf/smack_audio.{c,h}` vs `~/dev/starcraft-reasm-smacker/runtime/shim-port/elf/smack_audio.{c,h}`
   - Integra il worker async single-owner (ring 3 slot + `SC_SMACK_ASYNC=1` + `pthread_join` su `SmackClose` + `SC_AFFINITY` da `thread_affinity.h` + `smack_audio_hook_pcm` / snapshot) **preservando qualsiasi fix o diagnostica presente nella nostra versione (`SC_SMACK_TRACE`, guardie `__i386__`, ecc.)** e includendo i fix `e03357f` (niente prototipi libc ridondanti dopo `thread_affinity.h`).
   - Verifica la compilazione standalone `-m32` di `smack_elf.c` e `smack_audio.c` (con gli stessi flag di `tools/build-shim-x86.sh`, incluso `-D_GNU_SOURCE=1` se usato da `sched_setaffinity`) in `/tmp/smk-check/` (NON toccare `build/obj-x86/`).
3. **Consegna:**
   - Scrivi `reports/REPORT-port-smk.md` e `reports/STATE-port-smk.md` (max 70 righe) con esiti e misure reali.
