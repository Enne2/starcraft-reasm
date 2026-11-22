# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `llvm-mc`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/b1-hover/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (`--id b1-hover`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Bug 1 — Animazione scritte in hover che diventano nere (0 / 150 / 400 / 1000 ms)

1. **Cattura temporizzata & SCode dump iniziale:**
   - Avvia `SC_SCODE_DUMP=1 SC_SMACK_TRACE=1 ./tools/run-isolated.sh start --id b1-hover --skip-intro 1 --smack 1` (o passa `env` prima del comando).
   - Cattura `before.png`, poi fai `move 220 120` e cattura `hover-0ms.png`, `hover-150ms.png`, `hover-400ms.png`, `hover-1000ms.png`; leggile con `read`.
   - Estrai da `build/isolated/b1-hover/game.log` il bytecode SCode completo di `0x4A/0x2BC5` (e di eventuali altri kernel SCode che girano durante l'hover!). Disassemblalo con `llvm-mc --disassemble --triple=i686-unknown-linux-gnu` o Python e confronta ogni operazione con `run_scode` in `runtime/common/storm_sdraw_arm.c` e con `storm_334` (`0x15030AB0`) in `build/lift-full/storm.orig.dll`.
2. **Diagnosi anche lato caller / Smack / Palette / Blt:**
   - Verifica se il problema è in `storm_sdraw_arm.c` (es. opcode/mask/LUT/color-key nell'interprete SCode), in `smack_elf.c` (`SmackToBuffer` / `SmackDoFrame` / `SmackBufferOpen` / palette / `SmackWait`), in `com_ddraw.c` (`SetEntries` / `Blt` / `Unlock`), o in `StarCraft.orig.exe` (in IDA).
3. **Prototipo & Verifica su binario privato:**
   - Copia i soli `.c` modificati in `build/qa/b1-hover/src/`, compila gli `.o` privati con gli stessi flag di `tools/build-runtime-x86.sh` / `tools/build-shim-x86.sh` (`--redefine-syms runtime/shim-port/shim_rename.txt` se è uno shim!), copia `build/obj-x86/{runtime,shim-redef}/*.o` in `build/qa/b1-hover/{rt,sh}/`, sostituisci gli `.o` modificati, e linka `build/qa/b1-hover/starcraft-x86-b1` con gli stessi flag di `tools/link-x86.sh` (`ld.lld -m elf_i386`).
   - Testa `SC_BIN="$PWD/build/qa/b1-hover/starcraft-x86-b1" ./tools/run-isolated.sh start --id b1-hover-fix --skip-intro 1 --smack 1`, ripeti gli screenshot a 0 / 150 / 400 / 1000 ms su `move 220 120` e leggili con `read` per dimostrare che le scritte NON diventano più nere!
4. **Consegna:**
   - Ferma tutte le istanze `b1-hover*`.
   - Salva il diff pulito applicabile a `runtime/` in `build/qa/b1-hover/fix.patch` (relativo alla root del repo).
   - Scrivi `reports/SOL-b1-hover.md` e `reports/STATE-b1-hover.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
