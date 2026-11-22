# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, IAT, vtable o crash guest, l'oracolo primario sono i database IDA (`build/lift-full/StarCraft.orig.exe.i64`, `build/lift-full/storm.orig.dll.i64`), oppure `llvm-objdump -d` sui PE originali e `rg` mirato su `build/lift-full/*.llasm`. Controlla sempre `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti; mai allentare `reasm_indirect_resolve` con target arbitrari.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll`.

---

# Missione `scx86-bringup4`: Verifica allineamento stack 16-byte in tutti i trampolini x86 e bring-up fino al menu principale

Leggi prima:
- `reports/STATE-x86-bringup3.md` e `reports/REPORT-x86-bringup3.md`
- `runtime/x86/indirect_dispatch_x86.S`, `runtime/x86/com_bridge_x86.S`, `runtime/arm/gen_bridge_stubs.py` (sezione `--arch x86`)

## File di proprietà
- `runtime/x86/*`
- `runtime/arm/gen_bridge_stubs.py` (mantenendo invariato `--arch arm`)
- `runtime/arm/{cpu_llasm.c,game_main_arm.c,bridge_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (solo fix mirati `#ifdef __i386__` se necessari)
- `runtime/shim-port/elf/*` (solo fix mirati `#ifdef __i386__` se dimostrati)
- `tools/build-shim-x86.sh`, `tools/build-runtime-x86.sh`, `tools/link-x86.sh`
- `build/obj-x86/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup4.md`
- `reports/STATE-x86-bringup4.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Verifica e completa l'allineamento stack a 16 byte (`%esp % 16 == 0` prima di ogni `call` C)**:
   - Su Linux i686 moderno (`clang -m32`), le funzioni C assumono `%esp % 16 == 12` al momento dell'ingresso (quindi `%esp % 16 == 0` subito prima dell'istruzione `call`, inclusi eventuali `push` di argomenti!).
   - Verifica la patch appena aggiunta da bringup3 in `runtime/arm/gen_bridge_stubs.py` (`x86_bridge_common`), **E controlla anche**:
     - `runtime/x86/indirect_dispatch_x86.S` (`reasm_indirect`, `reasm_extern`)
     - `runtime/x86/com_bridge_x86.S` (`arm_com_bridge_common`)
     - (Nota: usando `mov %esp, %ebp` + `and $-16, %esp` + padding prima dei push, ogni trampolino garantisce `%esp % 16 == 0` al `call` indipendentemente dall'allineamento d'ingresso, e poi `mov %ebp, %esp` + `pop %ebp` ripristina esattamente lo stack prima di `jmp *%eax`!)
   - Ricompila e rilinka:
     ```sh
     TMPDIR=/dev/shm tools/build-shim-x86.sh && TMPDIR=/dev/shm tools/build-runtime-x86.sh && TMPDIR=/dev/shm tools/link-x86.sh
     ```
   - Verifica anche `tests/x86_runtime_check/check.sh`!

2. **Smoke test iterativo in `build/asset-sandbox` fino al menu principale**:
   ```sh
   (cd build/asset-sandbox && \
     SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
     SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     timeout 15 ../starcraft-x86)
   ```
   - Diagnostica con GDB / `SC_BRIDGE_TRACE=1` / `REASM_TRACE=1` e risolvi iterativamente qualsiasi blocco x86 successivo finché supera:
     - `DllMain` Storm + `CRT` StarCraft (`WinMain`)
     - apertura MPQ (`Stardat.mpq`, `BroodWar.mpq`, `patch_rt.mpq`)
     - `SetDisplayMode` + `SCode`/blitter/Smacker
     - **menu principale** (log `[fps]` o finestra attiva per più secondi, o eventuale STOP noto `0x0041CAC8` come su ARM `menu8`).
   - Se raggiunge il menu in `dummy`, esegui una prova rapida (5 secondi con `timeout 5`) con finestra reale SDL2 su `frigate` (usando `DISPLAY`/`WAYLAND_DISPLAY` dell'ambiente, `SDL_VIDEODRIVER=x11` o Wayland, senza iniettare input desktop).

3. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup4.md` e `reports/STATE-x86-bringup4.md` (max 70 righe).
   - Se tocchi ~175k token, fermati e scrivi subito report + STATE prima di superare 180k.

## Gate di accettazione (oggettivi)
1. `tests/x86_runtime_check/check.sh` PASS; `tools/build-shim-x86.sh && tools/build-runtime-x86.sh && tools/link-x86.sh` PASS; `readelf -h` = `ELF32/EXEC/Intel 80386`; `--dump-map` = 0 collisioni.
2. Smoke test in `build/asset-sandbox` supera `VirtualAlloc`, `CRT`, MPQ, `SetDisplayMode` e raggiunge il menu principale (con log FPS).
3. Prova con finestra reale SDL2 su `frigate` eseguita e documentata.
4. `reports/REPORT-x86-bringup4.md` e `reports/STATE-x86-bringup4.md` completi.
