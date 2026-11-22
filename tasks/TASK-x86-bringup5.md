# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, vtable o crash guest, l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`) oppure `llvm-objdump -d` sui PE originali e `rg` mirato su `build/lift-full/*.llasm`. Controlla sempre `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-bringup5`: Fix `%fs`/TEB e dichiarazioni `SHIM_STDCALL`, poi bring-up fino al menu principale su `frigate`

Leggi prima:
- `reports/STATE-x86-bringup4.md` e `reports/REPORT-x86-bringup4.md`
- `runtime/shim-port/elf/win32_shim.c` (solo intorno alle righe 1040-1080 e qualsiasi occorrenza di `__arm__` o `%fs` / `fs:`)
- `runtime/arm/bridge_arm.c` (controlla TUTTE le dichiarazioni `extern ... __c` per verificare se altre funzioni `S` stdcall mancano di `SHIM_STDCALL` su `__i386__`!)

## File di proprietà
- `runtime/x86/*`
- `runtime/arm/{bridge_arm.c,gen_bridge_stubs.py,cpu_llasm.c,game_main_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (fix mirati `#ifdef __i386__`, ARM invariato)
- `runtime/shim-port/elf/*` (fix mirati `#if defined(__arm__) || defined(__i386__)` / `#ifdef __i386__`)
- `tools/build-shim-x86.sh`, `tools/build-runtime-x86.sh`, `tools/link-x86.sh`
- `build/obj-x86/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup5.md`
- `reports/STATE-x86-bringup5.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Audit e fix immediato `%fs`/TEB + `SHIM_STDCALL`**:
   - Esegui `rg -n '__arm__|fs:|%fs|__i386__' runtime/shim-port/elf/ runtime/arm/` per trovare OGNI accesso diretto a `%fs` o ramo `#ifdef __arm__` che su `i386` deve usare il runtime emulato invece dell'host!
   - In `runtime/shim-port/elf/win32_shim.c` (~righe 1054-1071), instrada `GetLastError` e `SetLastError` sul TEB emulato anche quando compilato come runtime port (`__arm__` o `__i386__` con macro dedicata o `X86_ReadFsDword`/`X86_WriteFsDword` di `x86_helpers.h` — attenzione: non rompere `sc-reasm` se condividesse `win32_shim.c`, controlla se esiste già `-DSC_ELF_REAL_GDI=1` o passa `-DSC_PORT_EMULATED_TEB=1` in `tools/build-shim-x86.sh`, o usa `#if defined(__arm__) || defined(SC_PORT_EMULATED_TEB)`!).
   - In `runtime/arm/bridge_arm.c`, controlla TUTTE le dichiarazioni `extern ... __c` confrontandole con `runtime/shim-port/elf/*.c` (`S` vs non-`S` / `REASM_STDCALL` vs `cdecl`): aggiungi `SHIM_STDCALL` su `__i386__` a tutte quelle stdcall!

2. **Rebuild, test e smoke iterativo fino al menu principale**:
   - Esegui:
     ```sh
     TMPDIR=/dev/shm tools/build-shim-x86.sh && TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh
     tests/x86_runtime_check/check.sh
     ```
   - Esegui lo smoke in `build/asset-sandbox`:
     ```sh
     (cd build/asset-sandbox && \
       SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
       SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       timeout 15 ../starcraft-x86)
     ```
   - Diagnostica e risolvi iterativamente qualsiasi blocco successivo (`SetDisplayMode`, `SCode`/blitter, `libsmacker-1.2.0.so`, `com_ddraw`, `com_dsound`, `guest_call`, ecc.) finché `build/starcraft-x86` raggiunge il **menu principale** (animazione Smacker + log `[fps]` per diversi secondi, o eventuale STOP noto `0x0041CAC8` come su ARM `menu8`).
   - Se raggiungi il menu in `dummy`: esegui subito una prova da **5 secondi** (`timeout 5`) con finestra reale SDL2 su `frigate` (usando `DISPLAY`/`WAYLAND_DISPLAY` dell'ambiente, es. `SDL_VIDEODRIVER=x11` o default, `SC_SMACKER_SO=...`, `SC_SKIP_INTRO=1`, `SC_FPS_LOG=1`) senza inviare input desktop, documentando finestra, FPS e comportamento!

3. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup5.md` e `reports/STATE-x86-bringup5.md` (max 70 righe).
   - Se tocchi ~175k token, ferma subito l'iterazione e scrivi report + STATE prima di superare 180k.

## Gate di accettazione (oggettivi)
1. `build-shim-x86.sh`, `build-runtime-x86.sh`, `link-x86.sh` e `tests/x86_runtime_check/check.sh` escono con `0`; `readelf -h` = `ELF32/EXEC/Intel 80386`; `--dump-map` = `0` collisioni.
2. Smoke test in `build/asset-sandbox` raggiunge `SetDisplayMode`, Smacker e il menu principale con log `[fps]` (o documenta ogni tappa fino a lì).
3. Prova con finestra reale SDL2 su `frigate` (5s) eseguita e verificata se menu raggiunto.
4. `reports/REPORT-x86-bringup5.md` e `reports/STATE-x86-bringup5.md` completi.
