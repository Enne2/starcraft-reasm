# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: per qualsiasi dubbio su semantica, VA, vtable o crash guest, l'oracolo primario sono i database IDA del progetto (`build/lift-full/StarCraft.orig.exe.i64` e `build/lift-full/storm.orig.dll.i64`). Controlla sempre `ida_list` prima di operare su IDA (potrebbe già esserci una sessione read-only aperta sul nostro `storm.orig.dll.i64`); NON toccare sessioni IDA aperte per altri progetti/lavori.
2. **Autonomia**: non usare `ask_user`, non chiudere nessun turno con una domanda o con un piano in attesa di conferma. Prendi decisioni conservative, documentale in `## Decisioni autonome` nel report e procedi fino ai gate.
3. **Fail-closed (D19)**: mai stub silenziosi per API non implementate o kernel SCode sconosciuti.
4. **Divieti**:
   - NON lanciare altri sotto-agenti.
   - NON eseguire operazioni `git` (né commit né push).
   - NON modificare `vendor/`, `~/dev/sc-reasm`, `~/dev/starcraft-armv6`, `runtime/shim-port/` (salvo bug x86 dimostrato e documentato, preferibilmente `#ifdef __i386__`), `runtime/arm/indirect_dispatch.S`.
   - NON leggere per intero `build/lift-full/*.ll` (usa solo `head`/`rg` mirati).

---

# Missione `scx86-bringup`: Fix mapping dati x86, bring-up completo fino al menu principale su `frigate`

Leggi prima:
- `reports/STATE-x86-link.md` e `reports/REPORT-x86-step3.md` (punto di ripartenza esatto!)
- `work/link/REPORT-arm-menu.md` (riferimento comportamento atteso fino a `menu8`)
- `runtime/arm/gen_game_layout.py`

## File di proprietà
- `runtime/arm/gen_game_layout.py` (mantenendo invariato il comportamento ARM!)
- `runtime/x86/*`
- `runtime/arm/{cpu_llasm.c,game_main_arm.c,bridge_arm.c,indirect_dispatch.c,guest_call_arm.c,storm_sdraw_arm.c}` (solo fix mirati `#ifdef __i386__` se necessari)
- `tools/build-runtime-x86.sh`, `tools/link-x86.sh`
- `build/obj-x86/runtime/`, `build/starcraft-x86`, `build/qa/`
- `reports/REPORT-x86-bringup.md`
- `reports/STATE-x86-bringup.md` (max 70 righe)

## Obiettivi e ordine di esecuzione

1. **Fix `runtime/arm/gen_game_layout.py` per sezioni dati x86 (preservando ARM)**:
   - Ispeziona con `LC_ALL=C readelf -S` / `llvm-nm` le sezioni e i simboli dati in `build/lift-full/entry-x86/StarCraft.entry.o` e `storm.entry.o` (e confronta con cosa fa `gen_game_layout.py` per `.data..L_data`).
   - Nota cosa succede su LLVM `i686` vs `ARM`: in `StarCraft.ll` / `storm.ll`, i segmenti dati guest (`@_data`, `@_rdata`, ecc.) hanno nomi di sezione o linking diversi da `.data..L_data` (oppure `opt`/`llc` su `i686` fonde le globali costanti/dati in `.data` / `.rodata` / `.bss`, o `dispatch-ir.py` / `build-entry-x86.sh` può preservare i nomi sezione, OPPURE `gen_game_layout.py` deve mappare `*StarCraft.entry.o(.data .rodata*)` / simboli precisi ai VA `0x0050C000` e `0x15059000`!).
   - Verifica esattamente dove si trovano `stru_1505BDC0` / `@_data` in `storm.entry.o` e `StarCraft.entry.o`! Attenzione anche a non far finire eventuali tabelle `reasm_va_va`/`reasm_va_fn` nel segmento dati guest se non devono andarci (controlla come ARM separa `.data..L_data` da `.data` / `.rodata` di `StarCraft.entry.o`!).
   - Se serve modificare `tools/build-entry-x86.sh` (es. `-fdata-sections` o sezione dedicata nei `.ll` o `gen_game_layout.py`), fallo in modo pulito e deterministico senza toccare ARM.
   - Rigenera runtime e link (`tools/build-runtime-x86.sh`, `TMPDIR=/dev/shm tools/link-x86.sh`) e verifica con `LC_ALL=C readelf -lW` / `llvm-nm` che gli indirizzi di `stru_1505BDC0` (`0x1505bdc0`) e delle sezioni dati tradotte di StarCraft (`0x0050C000..`) e Storm (`0x15059000..`) siano esattamente ai VA guest!
   - Verifica anche `python3 tools/dispatch-ir.py --dump-map build/starcraft-x86` (0 collisioni!).

2. **Controllo doppio del primo crash (`arm_bridge_common_c` / `PC/fault 0x0e000000`)**:
   - In `REPORT-x86-step3.md` il crash era: `SIGSEGV, PC/fault 0x0e000000`, `InitializeCriticalSection @ 0x101cee70`, `cpu=0x0e000000`, `[ESP]=0x101cee70`, `[ESP+4]=0x18333b3c`.
   - Verifica se sistemato il VA di `stru_1505BDC0` (`0x1505bdc0`) quel SIGSEGV sparisce, OPPURE se `0x0e000000` era dovuto anche a un problema di `InitializeCriticalSection` o del trampolino `x86_bridge_common` (es. `arm_bridge_common_c` ritorna la continuation o poppa il return address sullo stack guest? Ricontrolla cosa succede dopo `InitializeCriticalSection`: `ret 4` guest poppa `0x101cee70` che era `[ESP]` o `[ESP]` era l'indirizzo di ritorno?! No! `0x101cee70` è un indirizzo HOST nel `.text` di Storm tradotto, non una VA guest `0x1503xxxx`! Se `0x101cee70` era sullo stack GUEST `0x021fff98`, significa che o la call guest aveva pushato un puntatore host, o `InitializeCriticalSection` è stato chiamato da un `call` nativo? Verifica subito con GDB / `SC_BRIDGE_TRACE=1`!).

3. **Smoke test headless iterativo in `build/asset-sandbox`**:
   - Esegui:
     ```sh
     (cd build/asset-sandbox && \
       SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" \
       SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       timeout 15 ../starcraft-x86)
     ```
   - Diagnostica e risolvi ogni eventuale bug x86-specifico (ABI, allineamento stack, `SCode`/blitter, `libsmacker` 32-bit, loader, segnali, `SetDisplayMode`, MPQ, menu loop) finché il gioco:
     - supera CRT init + `DllMain` Storm
     - carica gli MPQ (`Stardat.mpq`, `BroodWar.mpq`, `patch_rt.mpq`)
     - esegue `SetDisplayMode` e il blitter/Smacker
     - raggiunge il **menu principale** (log FPS `[fps]` o finestra attiva per diversi secondi, o fino all'eventuale stop `0x0041CAC8` osservato anche su ARM).
   - Se raggiunge il menu principale in `dummy`, fai anche una prova rapida con `SDL_VIDEODRIVER=x11` (o default Wayland/XWayland su `frigate`) per 3-5 secondi con `timeout 5` verificando se la finestra SDL2 reale si apre senza errori! (Se `DISPLAY`/`WAYLAND_DISPLAY` non è disponibile o fallisce, documentalo chiaramente senza forzare input desktop.)

4. **Report e STATE**:
   - Scrivi `reports/REPORT-x86-bringup.md` e `reports/STATE-x86-bringup.md` (max 70 righe) con tutte le evidenze, comandi esatti, numeri/FPS, eventuali indirizzi IDA studiati e fix applicati.

## Gate di accettazione (oggettivi)
1. `tools/build-runtime-x86.sh` e `tools/link-x86.sh` passano con exit code `0`.
2. `LC_ALL=C readelf -h build/starcraft-x86` = `ELF32 / EXEC / Intel 80386`; `dispatch-ir.py --dump-map build/starcraft-x86` = 0 collisioni; simboli dati tradotti mappati ai VA guest (`0x0050C000..` / `0x15059000..`).
3. Smoke test in `build/asset-sandbox` raggiunge il menu principale (CRT + Storm DllMain + MPQ + SetDisplayMode + Smacker/menu loop con log FPS), oppure documenta in modo verificato eventuali tappe oltre il menu (es. STOP `0x0041CAC8`).
4. `reports/REPORT-x86-bringup.md` e `reports/STATE-x86-bringup.md` completi.
