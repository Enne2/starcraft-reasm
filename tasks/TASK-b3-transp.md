# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `gdb`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/b3-transp/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (`--id b3-transp`, zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Bug 3 — Trasparenza interfacce e menu (sfondo nero opaco invece che semitrasparente)

Nei menu e dialoghi (`Select Game Type`, `Registry`, `MENU` in-game, `Save Game`), i pannelli che nell'originale sono semitrasparenti/tinti compaiono con lo sfondo completamente nero opaco.

1. **Isolamento del call-site esatto:**
   - Compila una copia strumentata privata in `build/qa/b3-transp/` (partendo da `runtime/shim-port/elf/{com_ddraw.c,gdi_elf.c,dlg_elf.c}` e `runtime/common/storm_sdraw_arm.c` con log temporanei su `s_Blt`, `s_BltFast`, `storm_313`, `storm_334`, `FillRect`, `BitBlt`, `SC_SCODE_DUMP=1`) e linka `build/qa/b3-transp/starcraft-x86-b3` usando gli `.o` di `build/obj-x86/` come base.
   - Esegui con `SC_BIN="$PWD/build/qa/b3-transp/starcraft-x86-b3" ./tools/run-isolated.sh start --id b3-transp --skip-intro 1 --smack 1`, naviga fino a `Select Game Type`, `Registry`, e (se utile) `MENU` in-game, cattura gli screenshot.
   - Dal log / GDB / disassemblato `storm.orig.dll` + `StarCraft.orig.exe`, identifica **quale chiamata esatta** disegna l'area rettangolare interna nera di quei pannelli (es. `storm_334` SCode con LUT tint? `storm_313`? `s_Blt` con flag non gestito? `storm` `SDlg*`? `DDBLT_COLORFILL` / `ROP` / `GDI`?).
   - Se passa per `storm_334` SCode (es. un kernel con LUT o blend o flag non gestito da `run_scode` o `fast-path`!), cattura e disassembla l'intero bytecode SCode di quel kernel e confrontalo byte-per-byte con l'originale!
2. **Prototipo & Verifica visiva su binario privato:**
   - Implementa la correzione esatta nella tua copia privata in `build/qa/b3-transp/src/`, ricostruisci `starcraft-x86-b3`, cattura `gametype-fixed.png`, `registry-fixed.png`, e `menu-ingame-fixed.png` e leggili con `read` per dimostrare che lo sfondo è semitrasparente/tinto invece di nero opaco!
3. **Consegna:**
   - Ferma tutte le istanze `b3-transp*`.
   - Salva il diff pulito in `build/qa/b3-transp/fix.patch` (relativo alla root).
   - Scrivi `reports/SOL-b3-transp.md` e `reports/STATE-b3-transp.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
