# Report pipeline ARMv6

## Esito gate
- **Tool build: PASS.** `ldc2` compila `llasm`; SRW è compilato da uno snapshot privato con `OUT_LLASM` sotto `build/pipeline/srw-src`. Il sorgente condiviso non è modificato.
- **LLVM IR → Linux ELF ARM → QEMU: PASS.** `tools/test-freestanding.sh` genera un fixture IR con `_start` e `svc #0` per `exit(23)`, deriva il datalayout da clang ARM prima di `opt`, produce ELF32 ARM EABI hard-float senza libc via `arm-none-eabi-ld -m armelf` e verifica exit 23 con `qemu-arm-static -cpu arm1176`.
- **LLASM→IR end-to-end: BLOCCATO da input SRW.** Il solo EXE disponibile in `work/slice/` non è accompagnato dai `relocations.csv`/configurazione esterna richiesta dal loader: SRW termina con “error opening external relocations file: relocations.csv”, prima di generare LLASM. Non ho modificato né usato gli artefatti slice oltre a questa prova read-only.
- Output accettato in `build/*` (compresi `build/slice`), non al di fuori di `build/`. `compile-arm.sh` conserva l'output `.o` senza link; un'estensione diversa da `.o` produce un ELF freestanding, con entry `_start` e linker privo di libc.

## CLI
```sh
tools/build-tools.sh
tools/lift.sh input.exe build/pipeline/out.llasm
tools/compile-arm.sh build/pipeline/out.ll build/pipeline/out.o   # solo oggetto
tools/compile-arm.sh build/pipeline/start.ll build/pipeline/start.elf
tools/test-freestanding.sh                                      # test QEMU ripetibile
```
`lift.sh` richiede gli input ausiliari SRW (config e relocazioni) nella posizione attesa dal programma. Il linker è sovrascrivibile con `ARM_LD`; QEMU con `QEMU_ARM`.

## Prossimo passo
Fornire il set SRW di configurazione e relocazioni per un EXE fixture, quindi eseguire `lift.sh` e passare l'IR risultante attraverso `compile-arm.sh`. Il test presente valida indipendentemente la coda IR→ARM→Linux syscall→QEMU, non la traduzione LLASM.
