# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `gdb`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/fix-ht/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Risolvere insieme Bug 1 (hover testo nero) e Bug 3 (sfondo nero invece che semitrasparente)

Le indagini precedenti (`reports/SOL-b1-hover.md` e `reports/SOL-b3-transp.md`) hanno provato che **entrambi** i bug passano per le stesse identiche funzioni e la stessa LUT:
- `sub_416ED0` (`0x00416ED0`, chiama `storm_334` a `0x00416F1A`, return `0x00416F1F` = `0x080AFF90`)
- `sub_416F30` (`0x00416F30`, costruisce la LUT `0x20AE0010` / chiama `storm_334` a `0x00417124`)
- Bug 1 (hover `Single Player`): `0x140×0x60`, `dst=0x20590010`, `src=0x20607AA4`, `base=0x20AE0010`.
- Bug 3 (`Registry` / `Select Game Type`): `0x16C×0x160`, `dst=0x205A0010`, `src=0x20520010`, `base=0x20AE0010`, **0 chiamate a `s_Blt`/`FillRect`/`BitBlt`**!

1. **Analisi oracolo IDA (`sub_416ED0` e `sub_416F30`):**
   - Usa la copia privata già aperta o creane una in `build/qa/fix-ht/oracle/` (mai toccare file `build/lift-full/*.i64` altrui se non nostri; in `build/qa/b3-transp/oracle/` ci sono già `StarCraft.orig.exe` e `storm.orig.dll`).
   - Decompila `0x00416ED0` e `0x00416F30` (e i suoi chiamanti in `Select Game Type` / `Registry` / `Main Menu hover`):
     - Come viene costruita la LUT `0x20AE0010`? Da dove prende la palette (`GetPaletteEntries`? `IDirectDrawSurface::GetPalette`? ricordati che in `com_ddraw.c:378` `s_GetPalette` mette `*p = 0`!!)?
     - Cosa contengono `src`, `dst` e la LUT `0x20AE0010` durante:
       (a) `hover` su `Single Player` (`0x140×0x60`),
       (b) `Select Game Type` / `Registry` (`0x16C×0x160`),
       (c) `MENU` in-game!
     - Verifica se `storm_334` SCode (`run_scode`) o `com_ddraw.c` (`s_GetPalette`, `s_Lock`, `p_GetEntries`, `DDBLT_ROP`) o `smack_elf.c` (`SmackToBufferRect`) causa:
       - il testo nero/occluso a 150 ms in hover,
       - lo sfondo nero opaco dei pannelli in `Select Game Type`, `Registry` e `MENU`.
2. **Prototipo & Verifica visiva su binario privato (`build/qa/fix-ht/starcraft-x86-ht`):**
   - Ricorda la ricetta di link esatta da `tools/link-x86.sh` e `SOL-b3-transp.md`:
     - Passa i path **assoluti** di `$PWD/build/lift-full/entry-x86/{StarCraft,storm}.entry.o` (altrimenti il linker script lascia vuote `.guest_exe_code`/`.guest_storm_code`!).
     - Per shim `.c` modificati applica `llvm-objcopy --redefine-syms=runtime/shim-port/shim_rename.txt`.
     - Usa `build/obj-x86/runtime/guest_data_link.ld`, `runtime_bounds.ld`, `-Wl,--section-start=.text=0x18000000 -Wl,-z,muldefs`.
   - Testa con `SC_BIN="$PWD/build/qa/fix-ht/starcraft-x86-ht" ./tools/run-isolated.sh start --id fix-ht --skip-intro 1 --smack 1` e cattura + leggi con `read`:
     - `hover-{0,150,400,1000}ms.png` su `Single Player`,
     - `gametype.png`, `registry.png`, e (se raggiungibile rapidamente) `menu-ingame.png`.
3. **Consegna:**
   - Ferma `./tools/run-isolated.sh stop --id fix-ht`.
   - Salva il diff pulito relativo alla root del repo in `build/qa/fix-ht/fix.patch`.
   - Scrivi `reports/SOL-fix-ht.md` e `reports/STATE-fix-ht.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
