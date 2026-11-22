# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, vtable o crash guest, l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`) oppure `llvm-objdump -d` sui PE originali e `rg` mirato su `build/lift-full/*.llasm`. Controlla sempre `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll` né il log da 6,2 milioni di righe (`run-x86-bringup5-fix3.log` - usa solo `tail` o `rg` mirati!).

---

# Missione `scx86-bringup6`: Isolare `SIGSEGV 0x00056F24`, verificare il menu principale (dummy + SDL2 reale su `frigate`) e fare pulizia dei log giganti

Leggi prima:
- `reports/STATE-x86-bringup5.md` e `reports/REPORT-x86-bringup5.md`
- `work/link/REPORT-arm-menu.md` (confronto con `menu8`, incluso STOP `0x0041CAC8` e menu loop)

## File di proprietà
- `runtime/x86/*`
- `runtime/arm/{bridge_arm.c,gen_bridge_stubs.py,cpu_llasm.c,game_main_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (fix mirati `#ifdef __i386__`, ARM invariato)
- `runtime/shim-port/elf/*` (fix mirati `#if defined(__arm__) || defined(__i386__)` / `SC_PORT_EMULATED_TEB`)
- `tools/build-shim-x86.sh`, `tools/build-runtime-x86.sh`, `tools/link-x86.sh`
- `build/obj-x86/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup6.md`
- `reports/STATE-x86-bringup6.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Audit rapido delle guardie `#ifdef __arm__` residue in `win32_shim.c` (`124`, `259`, `1295-1355`)**:
   - Leggi solo le righe `115-145`, `245-275`, `1285-1360` di `runtime/shim-port/elf/win32_shim.c` (e controlla se `CreateThread` / `arm_thread_main` / `tls` / `GetProcAddress` / `SCode` o altro usa `#ifdef __arm__`).
   - Se qualcuna di quelle righe deve essere attiva anche per `__i386__` (o `SC_PORT_EMULATED_TEB`), annotalo chiaramente e verifica prima con i fatti del passo 2.

2. **Diagnostica pulita di `0x00056F24` (SENZA `SC_BRIDGE_TRACE=1`)**:
   - Lancia prima lo smoke senza trace pesante per vedere il comportamento pulito e i millisecondi reali:
     ```sh
     (cd build/asset-sandbox && \
       SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
       SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       timeout 10 ../starcraft-x86) > build/qa/run-x86-bringup6-clean.log 2>&1
     ```
   - Controlla l'exit code, le righe `[fps]`, `SetDisplayMode`, `Smack`, `SCode`, e se si ferma a `0x00056F24` o a `0x0041CAC8` o prosegue!
   - Se si ferma ancora con `SIGSEGV` (`0x00056F24` o altro), catturalo in GDB (`handle SIGSEGV stop print nopass`, SENZA `SC_BRIDGE_TRACE=1`), salvando `info reg`, `bt`, `x/16i $pc-16`, `x/24wx $esp`, e mappa gli indirizzi sullo stack (`0x08341D70`, `0x085A17C0`, ecc.) con `llvm-nm -n`.
   - Correggi eventuali bug x86 dimostrati (D19 intatto!), ricompila e ripeti fino a raggiungere chiaramente il menu principale animato (`[fps]` stabili / frame / `SetDisplayMode` / menu screen!).

3. **Verifica reale con finestra SDL2 su `frigate` (Step 4.3 del piano)**:
   - Una volta confermato il menu in `dummy` (es. `[fps]` stabili e nessuna regressione prima del menu):
     - Verifica `DISPLAY` / `WAYLAND_DISPLAY` nell'ambiente.
     - Lancia una prova reale breve (`timeout 5`) da `build/asset-sandbox` con `SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" SC_SKIP_INTRO=1 SC_FPS_LOG=1` (e `SDL_VIDEODRIVER=x11` o Wayland nativo — prova il default o x11) **senza inviare alcun input desktop**.
     - Verifica nel log che apra la finestra reale SDL2, l'audio PulseAudio/PipeWire e stampi `[fps]` senza errori fatali.
   - Rimuovi eventuale file log gigantesco `build/qa/run-x86-bringup5-fix3.log` se supera centinaia di MB (lascia solo `tail` riassuntivo o rimuovilo per non sprecare disco).

4. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup6.md` e `reports/STATE-x86-bringup6.md` (max 70 righe) con:
     - tutte le milestone raggiunte (CRT, Storm DllMain, 4 MPQ, SetDisplayMode, SCode/blitter, Smacker, FPS, menu principale, prova SDL2 reale su `frigate`)
     - elenco esatto di tutti i file modificati e fix applicati
     - `## Prossimo passo`: preparazione struttura pulita Fase 2 (`runtime/common/` + `runtime/arch/<arch>/` + `tools/build.sh --arch`).

## Gate di accettazione (oggettivi)
1. Build completo (`build-shim-x86.sh`, `build-runtime-x86.sh`, `link-x86.sh`) e `tests/x86_runtime_check/check.sh` escono con `0`; `readelf -h` = `ELF32/EXEC/Intel 80386`; `--dump-map` = `0` collisioni.
2. Smoke test pulito (senza trace gigante) raggiunge stabilmente il **menu principale** con log `[fps]` (documentando eventuale comportamento di uscita/STOP noto come `0x0041CAC8` o loop infinito fino a timeout).
3. Prova reale di 5 secondi con finestra SDL2 su `frigate` documentata con log `[fps]` e backend video/audio reale.
4. `reports/REPORT-x86-bringup6.md` e `reports/STATE-x86-bringup6.md` completi.
