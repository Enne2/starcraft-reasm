# Report — Porting Smacker Punto 1

## Esito
Portati i componenti richiesti da `starcraft-reasm-smacker` in `starcraft-reasm-generic`, mantenendo la baseline `.orig.so` e senza intervenire su file fuori perimetro. La directory target non contiene metadati Git (`git status` non disponibile).

## File importati/integrati
- `vendor/libsmacker/` (bit-reservoir LSB-first + LUT Huffman), `tools/build-libsmacker.sh`.
- `tests/smacker_bench/`, i tre `tests/smacker_bench_*.smk`, `tests/x86_affinity_test/`.
- `runtime/common/thread_affinity.h`.
- `runtime/shim-port/elf/smack_elf.c`, `smack_audio.c`, `smack_audio.h`: worker asincrono single-owner, ring a 3 slot, `SC_SMACK_ASYNC`, join su `SmackClose`, audio PCM snapshot e affinità worker via `sc_affinity_apply(1)`.
- Conservati nel file Smack target il tracing `SC_SMACK_TRACE` (compresa la diagnostica `SmackToBuffer`) e le guardie `__arm__ || __i386__`; nessun prototipo libc ridondante dopo `thread_affinity.h`.

## Integrità e test
- Prima e dopo la build: `build/i386-libs/libsmacker-1.2.0.orig.so`, SHA-256 `9cf30f89d581593b0d853eb5643677e6d703b6e44dc159d47493fba0315ba070` (invariato).
- `./tools/build-libsmacker.sh --arch x86`: PASS; output ELF32 Intel 80386, SHA-256 `1124afcfb24148892de074969f2fe3908328ad959cdf4620880a155a8f19d71b`.
- `./tests/smacker_bench/run_bench.sh --verify`: PASS bit-exact contro `.orig.so`:
  - `blizzard.smk`: dump pari, 17.996.568 byte; mediana decode originale 0,113600 s, ottimizzata 0,053563 s (2,12×); apertura 0,004921 → 0,002624 s.
  - `menu-exit.smk`: dump pari, 1.217.820 byte; mediana decode 0,011167 s → 0,003754 s (2,97×); apertura 0,000199 → 0,000124 s.
  - Benchmark a 5 run; file input rispettivamente 2.429.724 e 241.908 byte.
- `./tests/x86_affinity_test/check.sh`: PASS, `SC_AFFINITY=0:1`, `sched_getaffinity` verificata.
- Compilazione standalone di `smack_elf.c` e `smack_audio.c` in `/tmp/smk-check/`: PASS con clang `--target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g`, include/define del build shim e `-D_GNU_SOURCE=1`; entrambi ELF32/i386. Non toccato `build/obj-x86/`.
- `bash -n` su build e test script: PASS.

## Limiti della verifica
Non eseguiti il build globale, la build completa dello shim, né una prova di playback live. La verifica runtime end-to-end dell'worker asincrono è quindi fuori da questo esito; qui sono validati build della libreria, parità benchmark, affinità e compilazione separata dei due translation unit.

## Decisioni autonome
Ho seguito la sorgente del porting per il worker e l'audio, ripristinando dal file target la diagnostica `SmackToBuffer` associata a `SC_SMACK_TRACE`. Ho mantenuto la baseline `.orig.so` intatta e limitato le verifiche ai gate richiesti e alla compilazione isolata; nessuna modifica a build globale o file esclusi dal preludio.
