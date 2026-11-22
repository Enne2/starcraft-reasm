# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA o callback (`0x00404F09`, `0x006DD684`), consulta `build/lift-full/StarCraft.orig.exe.i64` / `storm.orig.dll.i64` o `llvm-objdump -d` sui PE originali e `rg` mirato su `build/lift-full/*.llasm`. Controlla `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON iniettare input desktop (no mouse/tastiera/ydotool/click/tasti).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-polish`: Indagine rapida su `0x00404F09`/`0x1805F2D4`, consolidamento multi-architettura (`runtime/common/` + `runtime/arch/` + `tools/build.sh`) e verifica finale su `frigate`

Leggi prima:
- `reports/STATE-x86-real.md` e `reports/REPORT-x86-real.md`
- `PLAN-MULTIARCH.md` (sezione 7.1 — struttura target del repository)

## Obiettivi e ordine di esecuzione

### Parte A — Indagine mirata su `0x00404F09` / `0x1805F2D4` (max ~35k token)
1. Con GDB (`watch *(unsigned int*)0x006DD684` e breakpoint su `0x0801AE00` / routine `0x00404F09` e `0x004FDE70` / `0x004FDE7B`), determina in 1-2 run:
   - **Perché** il menu è uscito entrando in `doexit` (`0x00404F09`) dopo pochi secondi (è un evento SDL `WM_CLOSE`/`SDL_QUIT`, una dialog `Dlg` chiusa, `SNet`, o ritorno normale di una fase)?
   - **Chi** ha scritto `0x1805F2D4` nella tabella `0x006DD684` (es. `atexit`/`_onexit` chiamato dal CRT o dal codice C/LLASM, e cosa rappresenta `0x1805F2D4` nel binario ELF)?
2. Se emerge una causa pulita e chiaramente risolvibile (es. bug nell'handling eventi/dialog/onexit senza violare D19), applicala e verifica che il menu su Wayland resti vivo più a lungo (`timeout 5` con più `[fps]` e hash `DoFrame` diversi da zero!). Se invece `0x1805F2D4` richiede una modifica strutturale più ampia (o è l'equivalente x86 del residuo `0x0041CAC8` di `menu8`), documenta con esattezza la provenienza e lascia D19 intatto.

### Parte B — Struttura pulita multi-architettura (`PLAN-MULTIARCH.md` §7.1)
Prepara una base pulita, ordinata e pronta per estendere ad altre architetture:
1. Organizza `runtime/`:
   - `runtime/common/`: tutta la logica C/H/DEF/PY comune indipendente dall'architettura (`cpu_llasm.*`, `game_memory_arm.*`, `bridge_arm.*`, `bridge_*.def`, `com_bridge_arm.c`, `crt_init_arm.c`, `elf_support_arm.*`, `game_glue.c`, `game_main_arm.c`, `guest_call_arm.c`, `indirect_dispatch.{c,h}`, `storm_sdraw_arm.c`, `win32_ext_arm.c`, `x86_helpers.*`, `x87_crt_arm.c`, `gen_*.py`, `patch_guest_pointers.py`, `include/`, `patches/`, `tests/`).
   - `runtime/arch/x86/`: `indirect_dispatch_x86.S`, `com_bridge_x86.S`, `guest_abs_zero_x86.S`.
   - `runtime/arch/arm/`: `indirect_dispatch.S`, `com_bridge_arm.S`, `guest_abs_zero.S`, `tls_arm.S`.
   - **Compatibilità retroattiva**: mantieni funzionanti gli script esistenti (`tools/build-runtime-x86.sh`, `tools/build-runtime-arm.sh`, `tools/link-arm.sh`, `tools/link-x86.sh`, `tests/x86_runtime_check/check.sh`) aggiornando i loro path verso `runtime/common/` + `runtime/arch/<arch>/` (oppure fornendo symlink puliti se più sicuro — verifica che non ci siano path rotti in `tools/` e `tests/` con `rg -n 'runtime/(arm|x86)' tools/ tests/`!).
2. Crea `tools/build.sh`:
   - CLI unificata: `tools/build.sh --arch <x86|arm> [--skip-entry] [--no-link]`
   - Per `--arch x86`: invoca (o coordina) `build-entry-x86.sh` (saltabile con `--skip-entry` se `entry-x86/*.entry.o` già pronti), `build-shim-x86.sh`, `build-runtime-x86.sh`, `link-x86.sh`.
   - Per `--arch arm`: invoca `build-entry-obj.sh` (con `--no-smoke`), `build-runtime-arm.sh`, `link-arm.sh`.
   - Documenta l'architettura in un `README-MULTIARCH.md` o `runtime/README.md` sintetico (cosa serve per aggiungere una nuova architettura 32-bit vs 64-bit).

### Parte C — Gate di verifica finale (dopo il refactor!)
1. Esegui `tools/build.sh --arch x86 --skip-entry` (exit `0`).
2. Esegui `tests/x86_fastcc_check/check`, `tests/x86_runtime_check/check.sh` (entrambi exit `0`).
3. Verifica `LC_ALL=C readelf -h build/starcraft-x86` (`ELF32`, `EXEC`, `Intel 80386`) e `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` (0 collisioni).
4. Verifica che ARM non sia rotto: `python3 runtime/common/gen_bridge_stubs.py --arch arm ...` e compilazione `.S` ARM (`clang --target=armv6-unknown-linux-gnueabihf -c runtime/arch/arm/*.S` in `/tmp` — se supportato da clang installato!).
5. Esegui nuovamente lo smoke **reale** su `frigate` con `SDL_VIDEODRIVER=wayland` (`timeout -k 1s 5s`) salvando `build/qa/run-x86-final-wayland.log` e verifica `[host] finestra creata`, PipeWire audio, `SmackOpen`, e `[fps]`.
6. Scrivi `reports/REPORT-x86-final.md` e `reports/STATE-x86-final.md` (max 70 righe).
