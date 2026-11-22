# Missione Passo 3 & 4 — Link `build/starcraft-x86`, `--dump-map` e smoke test nativo su `frigate`

## Vincoli
- **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica/VA/crash guest, consulta in sola lettura `build/lift-full/StarCraft.orig.exe.i64` o `storm.orig.dll.i64` (controlla prima `ida_list`, non toccare altre sessioni).
- **Autonomia**: niente `ask_user`, niente `git`, niente sotto-agenti, non toccare `vendor/`, `runtime/arm/indirect_dispatch.S` o sorgenti esterni.
- **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti.
- **Soglia contesto**: se superi ~180k token, scrivi subito `reports/REPORT-x86-step3.md` e `reports/STATE-x86-link.md` (max 70 righe) prima di proseguire.

## File di proprietà aggiuntivi
- `tools/link-x86.sh` (ispirato a `tools/link-arm.sh`)
- `build/starcraft-x86`
- `reports/REPORT-x86-step3.md`
- `reports/STATE-x86-link.md` (max 70 righe)
- Eventuali fix mirati in `runtime/x86/`, `runtime/arm/{cpu_llasm.c,game_main_arm.c,bridge_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` o `tools/build-runtime-x86.sh` (documentando ogni modifica con `#ifdef __i386__`).

## Obiettivi e ordine di esecuzione

1. **Crea `tools/link-x86.sh` (leggi prima `tools/link-arm.sh` e `work/link/REPORT-arm-menu.md`)**:
   - Usa il linker `ld.lld` (`~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld`, oppure trova con `which ld.lld` / fallback sul path rustup) in modalità `elf_i386` / `-m32 -no-pie -fno-pie`.
   - Passa `-Wl,--section-start=.text=0x18000000 -Wl,-T,build/obj-x86/runtime/guest_data.ld -Wl,-z,muldefs` (verifica in `tools/link-arm.sh` tutti i flag esatti, es. `--allow-multiple-definition`, `--whole-archive`, ordine oggetti, librerie `-lc -lm -lgcc_s -lSDL2 -ldl -lpthread`, e se servono `crt1.o`/`crti.o`/`crtbegin.o` 32-bit o `clang -m32 -no-pie --ld-path=...`).
   - Collega:
     - `build/lift-full/entry-x86/StarCraft.entry.o` e `storm.entry.o`
     - `build/obj-x86/runtime/*.o` (attenzione a non duplicare `llasm_*.o` se già compilati in `build/obj-x86/runtime/` vs `build/obj-x86/llasm/`!)
     - `build/obj-x86/shim-redef/*.o`
   - Output: `build/starcraft-x86`.

2. **Verifica statica dell'eseguibile (`build/starcraft-x86`)**:
   - `LC_ALL=C readelf -h build/starcraft-x86` → deve essere `ELF32`, `EXEC (Executable file)` (NON `DYN`/PIE!), `Intel 80386`.
   - `LC_ALL=C readelf -lW build/starcraft-x86` → verifica i segmenti `LOAD` (guest data a `0x00400000..`, `0x15000000..`; codice host a `0x18000000..`; nessuna sovrapposizione!).
   - `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` → verifica **0 collisioni** VA↔host e numero di entry attese (~101.800+).

3. **Smoke test headless in `build/asset-sandbox` (confronto con `work/link/REPORT-arm-menu.md`)**:
   - Esegui da `build/asset-sandbox`:
     ```sh
     (cd build/asset-sandbox && \
       SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
       SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       timeout 15 ../starcraft-x86)
     ```
     (Controlla in `work/link/REPORT-arm-menu.md` e `runtime/arm/game_main_arm.c` quali env var usa il runtime, es. `SC_DLG_AUTOCLOSE=1`, `REASM_TRACE=1`, `SC_FPS_LOG=1`.)
   - Osserva fin dove arriva: CRT init → `DllMain` storm → `SetDisplayMode` → apertura MPQ (`Stardat.mpq`, `BroodWar.mpq`, `patch_rt.mpq`) → `SCode`/blitter → menu principale (`Single Player`, `Multiplayer`, `Campaign Editor`, `Exit`) → eventuali FPS o eventuale stop/crash.
   - Se incontra un crash o errore prima del menu (es. bug nei trampolini x86, allineamento stack, `VirtualAlloc`/arena, `libsmacker`, `SCode`, ecc.), diagnosticalo con `gdb`/`llvm-addr2line` (e IDA se riguarda il guest), correggilo, rilinka e ripeti lo smoke test!

4. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-step3.md` e `reports/STATE-x86-link.md` (max 70 righe) con:
     - comando esatto di `tools/link-x86.sh`
     - output `readelf -h`, `readelf -lW` (riassunto segmenti) e `--dump-map`
     - log/metriche complete dello smoke test in `build/asset-sandbox` (milestone raggiunte, FPS se nel menu, punto esatto di stop/uscita)
     - eventuali bug trovati e risolti durante il bring-up
     - `## Decisioni autonome` e `## Prossimo passo` (es. test interattivo con finestra reale SDL2 su `frigate` e pulizia/refactoring Fase 2 `runtime/common/` + `runtime/arch/`).
