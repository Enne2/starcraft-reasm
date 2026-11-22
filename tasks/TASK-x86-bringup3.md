# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, IAT, vtable o crash guest, l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`). Se il server IDA MCP non apre il `.i64`, usa direttamente `llvm-objdump -d` / `readelf` su `build/lift-full/storm.orig.dll` e `StarCraft.orig.exe`, e `rg` mirato su `build/lift-full/storm.llasm` / `StarCraft.llasm`! Controlla sempre `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/shim-port/` (salvo bug x86 dimostrato e documentato, preferibilmente `#ifdef __i386__`), `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-bringup3`: Risolvere SIGSEGV `0x18140AEE` dopo `GetModuleHandleA` e arrivare al menu principale su `frigate`

Leggi prima:
- `reports/STATE-x86-bringup2.md` e `reports/REPORT-x86-bringup2.md` (contiene tutta l'analisi già fatta su `0x10041A10` / `0x18140AEE`!)
- `runtime/x86/indirect_dispatch_x86.S`, `runtime/arm/gen_bridge_stubs.py` (sezione `--arch x86`)

## File di proprietà
- `runtime/x86/*`
- `runtime/arm/gen_bridge_stubs.py` (mantenendo invariato `--arch arm`)
- `runtime/arm/{cpu_llasm.c,game_main_arm.c,bridge_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (solo fix mirati `#ifdef __i386__` se necessari)
- `tools/build-runtime-x86.sh`, `tools/link-x86.sh`, `runtime/arm/gen_pe_image_bytes.py`, `runtime/arm/gen_game_layout.py`
- `build/obj-x86/runtime/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup3.md`
- `reports/STATE-x86-bringup3.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Isola la causa esatta del salto a `0x18140AEE` (`"GetModuleHandleA"` in `.rodata`)**:
   - Guarda `llvm-objdump -d --start-address=0x10041a10 --stop-address=0x10041b40 build/starcraft-x86` e `llvm-objdump -d --start-address=0x18000b80 --stop-address=0x18000c20 build/starcraft-x86` (`x86_bridge_common`).
   - Lancia `gdb -q -batch` da `build/asset-sandbox` (con `SC_SMACKER_SO`, `SC_SKIP_INTRO=1`, `SC_BRIDGE_TRACE=1`, `REASM_TRACE=1`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`):
     - Breakpoint su `*0x10041A10` (dopo il ritorno di `GetModuleHandleA`), stampa `info reg`, `x/8wx ($ebx+16)` (`cpu->_esp` e `_ind_target` / `_ind_esp`), e poi `display/i $pc`, `stepi` (20-40 passi) fino al punto esatto in cui `EIP` cambia verso `0x18140AEE`!
     - Controlla attentamente se `storm.ll` / `storm.orig.dll` alla IAT di `GetModuleHandleA` o `GetProcAddress` (o `arm_bridge_lookup_thunk` / `guest_pe_bytes` / `bridge_table` / `0x18000BC1` in `x86_bridge_common`) ha scritto una stringa invece di un indirizzo o ha corrotto `eax`/`ecx`/`esp`!
   - Applica il fix pulito (rispettando D19 fail-closed e mantenendo `--arch arm` intatto), ricompila/rilinka (`tools/build-runtime-x86.sh` + `TMPDIR=/dev/shm tools/link-x86.sh`) e verifica!

2. **Itera lo smoke test in `build/asset-sandbox` fino al menu principale**:
   ```sh
   (cd build/asset-sandbox && \
     SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
     SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     timeout 15 ../starcraft-x86)
   ```
   - Continua a diagnosticare e fissare ogni blocco x86 successivo finché il gioco supera:
     - `DllMain` Storm + `CRT` StarCraft
     - apertura MPQ (`Stardat.mpq`, `BroodWar.mpq`, `patch_rt.mpq`)
     - `SetDisplayMode` + `SCode`/blitter/Smacker
     - **menu principale** (log `[fps]` o finestra attiva per più secondi, o eventuale STOP noto `0x0041CAC8` come su ARM `menu8`).
   - Se raggiungi il menu in `dummy`, esegui anche una prova rapida (5s con `timeout 5`) con finestra reale SDL2 su `frigate` (usando `DISPLAY`/`WAYLAND_DISPLAY` attuali, `SDL_VIDEODRIVER=x11` o Wayland, senza inviare alcun input desktop).

3. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup3.md` e `reports/STATE-x86-bringup3.md` (max 70 righe).
   - Se ti avvicini a ~175k token, ferma l'iterazione corrente e scrivi subito report + STATE prima di superare 180k.

## Gate di accettazione (oggettivi)
1. Causa radice di `0x18140AEE` dimostrata e corretta con fix pulito (D19 intatto).
2. `tools/build-runtime-x86.sh` e `TMPDIR=/dev/shm tools/link-x86.sh` escono con `0`; `readelf -h` = `ELF32/EXEC/Intel 80386`; `--dump-map` = `0` collisioni.
3. Smoke test in `build/asset-sandbox` raggiunge MPQ, `SetDisplayMode` e menu principale con log FPS (più prova finestra reale SDL2 su `frigate` se il menu dummy è raggiunto).
4. `reports/REPORT-x86-bringup3.md` e `reports/STATE-x86-bringup3.md` completi.
