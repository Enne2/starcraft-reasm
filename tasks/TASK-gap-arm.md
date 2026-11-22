# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
2. **Divieti**: NON toccare i file `.c` dello shim/runtime (`win32_shim.c`, `host_elf.c`, `snp_elf.c`, `com_*.c`, `storm_sdraw_arm.c`, `crt_init_arm.c`), NON sovrascrivere `build/starcraft-x86` né `build/obj-x86/`. Usa `build/qa/gap-arm/` o `/tmp/gap-arm/` per gli output ARM! Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gap-arm`: Verifica completa parità multi-architettura `--arch arm` (`tools/build.sh` + `runtime/common/` + `runtime/arch/arm/`)

## File di proprietà (esclusivi)
- `tools/build.sh`
- `tools/build-entry-obj.sh`
- `tools/build-runtime-arm.sh`
- `tools/link-arm.sh`
- `runtime/README.md`
- `build/qa/gap-arm/` (output test/oggetti ARM)
- `reports/REPORT-arm-parity.md`
- `reports/STATE-arm-parity.md` (max 70 righe)

## Obiettivi
1. **Verifica tutti i generatori e script `--arch arm` dopo il refactor `runtime/common/` + `runtime/arch/arm/`**:
   - Controlla che ogni riferimento in `tools/build-entry-obj.sh`, `tools/build-runtime-arm.sh`, `tools/link-arm.sh`, `tools/build.sh` punti ai nuovi path `runtime/common/` e `runtime/arch/arm/` senza riferimenti rotti a `runtime/arm/`.
   - Esegui/verifica (in `build/qa/gap-arm/` senza toccare `build/lift-full/entry` se già non esiste o usando dir privata `ENTRY_DIR`/`RUNTIME_OBJ_DIR`!):
     - `gen_game_layout.py`, `gen_pe_image_bytes.py --arch arm`, `gen_storm_ordinals.py`, `gen_bridge_stubs.py --arch arm`.
     - Compilazione di tutti gli assembly ARM (`runtime/arch/arm/*.S` + `bridge_stubs.S` + `guest_pe_bytes.S`) con `clang --target=armv6-unknown-linux-gnueabihf -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard` -> verifica `LC_ALL=C readelf -h` (`ELF32`, `ARM`)!
     - Compilazione syntax/object-check dei `.c` di `runtime/common/` con `--target=armv6-unknown-linux-gnueabihf` (identificando eventuali dipendenze da sysroot ARM se assente su `frigate` vs file compilabili direttamente con header freestanding/LLVM!).
     - Verifica anche `tools/build-entry-obj.sh --check` (o verifica di `dispatch-ir.py` + `opt` + `llc -mtriple=armv6-unknown-linux-gnueabihf` almeno su `storm.ll`, o su un subset se `StarCraft.ll` è lento, verificando che `RE_TABLE_BASE` + `RE_PTRTOINT_TEXT` appena aggiunti producano IR valido e ottimizzabile anche su target ARM!).
2. **Aggiorna `runtime/README.md` e `tools/build.sh`**:
   - Se trovi path/opzioni imprecise in `tools/build.sh --arch arm` o `build-runtime-arm.sh`, correggile pulitamente in modo che la pipeline ARM sia allineata a quella x86.
   - Documenta esattamente in `runtime/README.md` e `reports/REPORT-arm-parity.md` cosa compila su `frigate` direttamente e cosa richiede il sysroot ARMv6 (non copiato in questo workspace leggero).
3. **Report, STATE e `qmem_store`**:
   - Scrivi `reports/REPORT-arm-parity.md`, `reports/STATE-arm-parity.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
