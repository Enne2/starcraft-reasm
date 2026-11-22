# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, IAT, vtable o crash guest, l'oracolo primario sono i database IDA in `build/lift-full/StarCraft.orig.exe.i64` e `build/lift-full/storm.orig.dll.i64`. Controlla sempre `ida_list` prima di operare su IDA e NON toccare sessioni IDA aperte per altri lavori.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; non allentare `reasm_indirect_resolve` con target fittizi.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/shim-port/` (salvo bug x86 dimostrato e documentato), `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-bringup2`: Risolvere `STOP target 0x18001546` e completare bring-up fino al menu principale su `frigate`

Leggi prima:
- `reports/STATE-x86-bringup.md` e `reports/REPORT-x86-bringup.md`
- `runtime/arm/gen_bridge_stubs.py`, `runtime/arm/bridge_arm.c`, `runtime/arm/indirect_dispatch.c`, `runtime/arm/guest_call_arm.c`
- `work/link/REPORT-arm-menu.md`

## File di proprietà
- `runtime/x86/*`
- `runtime/arm/gen_bridge_stubs.py` (mantenendo invariato `--arch arm`)
- `runtime/arm/{cpu_llasm.c,game_main_arm.c,bridge_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (solo fix mirati `#ifdef __i386__` se necessari)
- `tools/build-runtime-x86.sh`, `tools/link-x86.sh`
- `build/obj-x86/runtime/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup2.md`
- `reports/STATE-x86-bringup2.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Diagnosi esatta dello STOP `0x18001546` (`build/qa/run-x86-bringup.log`)**:
   - Ispeziona `build/qa/run-x86-bringup.log` (ultime 60 righe) e `runtime/arm/indirect_dispatch.c` (`reasm_indirect_resolve`, `reasm_dispatch_init`, `g_arm_lo`/`g_arm_hi`/`reasm_text_lo`/`reasm_text_hi`).
   - Esegui GDB (`gdb -batch`) con breakpoint su `reasm_indirect_unresolved` e `reasm_indirect_resolve` per vedere **chi** chiama `reasm_indirect_resolve(0x18001546)` (backtrace + argomento + `cpu->_esp` + `cpu->_ind_target` + disassembly attorno al call site `loc_1503F6C3`).
   - Verifica due ipotesi chiave:
     - (a) Perché `0x18001546` è finito come `target`? È il thunk `InitializeCriticalSection` (quindi `x86_bridge_common` / `arm_bridge_common_c` / `storm.entry.o` ha risolto la continuation sbagliata o ha lasciato `0x18001546` su stack/registro)? Oppure `storm_loader` / IAT ha scritto l'indirizzo host `0x18001546` e `reasm_indirect_resolve` avrebbe dovuto accettarlo o non ci sarebbe dovuto arrivare affatto? Confronta con `runtime/arm/bridge_arm.c` e `runtime/arm/gen_bridge_stubs.py` (`--arch arm` vs `--arch x86`)!
     - (b) Se `0x18001546` è dentro `[reasm_text_lo, reasm_text_hi)`, perché `reasm_indirect_resolve` lo rifiuta? Controlla `reasm_text_lo`/`reasm_text_hi` in `runtime_bounds.ld` vs `indirect_dispatch.c`!
   - Applica il fix pulito (senza aggirare D19), ricompila (`tools/build-runtime-x86.sh` + `TMPDIR=/dev/shm tools/link-x86.sh`) e verifica!

2. **Bring-up iterativo in `build/asset-sandbox` fino al menu principale**:
   - Esegui lo smoke test:
     ```sh
     (cd build/asset-sandbox && \
       SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
       SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       timeout 15 ../starcraft-x86)
     ```
   - Diagnostica e risolvi ogni successivo bug x86-specifico (bridging, `com_bridge_x86.S`, `guest_call`, `SCode`/blitter, `libsmacker`, memoria/arena, segnali, variadic `shim_calls`) finché `build/starcraft-x86` supera:
     - CRT init + `DllMain` Storm
     - apertura MPQ (`Stardat.mpq`, `BroodWar.mpq`, `patch_rt.mpq`)
     - `SetDisplayMode` + blitter/Smacker
     - **menu principale** (log `[fps]` o finestra attiva per più secondi, o eventuale STOP noto `0x0041CAC8` come su ARM `menu8`).
   - Se raggiunge il menu in `dummy`, esegui una prova rapida (5 secondi con `timeout 5`) con finestra reale SDL2 su `frigate` (usando l'ambiente grafico disponibile `DISPLAY`/`WAYLAND_DISPLAY`, `SDL_VIDEODRIVER=x11` o Wayland, senza inviare input desktop).

3. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup2.md` e `reports/STATE-x86-bringup2.md` (max 70 righe) con comandi esatti, cause radice trovate, fix applicati, log/FPS delle milestone raggiunte, esito prova finestra reale, e `## Prossimo passo` (pulizia `runtime/common/` + `runtime/arch/` per Fase 2).

## Gate di accettazione (oggettivi)
1. `tools/build-runtime-x86.sh` e `TMPDIR=/dev/shm tools/link-x86.sh` escono con `0`; `readelf -h` = `ELF32/EXEC/Intel 80386`; `--dump-map` = `0` collisioni.
2. Smoke test in `build/asset-sandbox` supera `DllMain` Storm, apertura MPQ, `SetDisplayMode` e raggiunge il menu principale (con log FPS o tappe oltre il menu documentate).
3. Prova con finestra reale SDL2 su `frigate` eseguita e documentata (o motivo esatto se ambiente headless).
4. `reports/REPORT-x86-bringup2.md` e `reports/STATE-x86-bringup2.md` completi.
