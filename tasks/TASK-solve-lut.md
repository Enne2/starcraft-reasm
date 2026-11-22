# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `gdb`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/solve-lut/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Risolvere la causa di `unk_6D05B0 == 0` (Bug 1 Hover + Bug 3 Trasparenza)

`reports/SOL-fix-ht.md` ha già dimostrato il fatto decisivo:
- `0x006D05B0` (`unk_6D05B0`, la palette da 1024 byte usata da `sub_4174A0` per generare la LUT 64 KiB `0x20AA0010`/`0x20AE0010`) è **tutta a zero (1024/1024 byte zero)**!
- Di conseguenza la LUT traslucida di `Registry` (`Select Game Type`, `MENU`) mappa i pixel su `0x00` (nero opaco) e la LUT di hover sovrascrive/oscura il testo invece di fare blending!
- `sub_4DD9E0` copia da `unk_5994E0` / `unk_51C008` in `unk_6D05B0` via `sub_4178B0` (`0x4DDB3A`), e `sub_4C99C0` inizializza con `sub_417C20(&v6, &unk_5994E0)` (`0x4C9A51`).

1. **Trova in GDB + IDA perché `0x006D05B0` è tutto zero:**
   - Lancia `build/starcraft-x86` in `./tools/run-isolated.sh --id solve-lut --skip-intro 1 --smack 1` (e prova anche con `--skip-intro 0` per vedere se `SC_SKIP_INTRO=1` cambia qualcosa!) e attacca `gdb -q -batch -p <pid>` (o lancia sotto `gdb` in `Xvfb`), mettendo breakpoint sui guest VA tradotti di:
     - `0x004178B0` (`sub_4178B0`), `0x00417C20` (`sub_417C20`), `0x004DD910`, `0x004DD9E0`, `0x004C99C0`!
     - Controlla se quelle funzioni vengono chiamate, cosa c'è in `0x005994E0` (`unk_5994E0`) e `0x0051C008` (`unk_51C008`) e nella risorsa PCX/palette in ingresso, e in IDA controlla **chi popola `0x005994E0` e `0x0051C008`** (`ida_xrefs` su `0x5994E0`, `0x51C008`, `0x6D05B0` usando `build/qa/fix-ht/oracle/StarCraft.orig.exe.i64`!).
   - Verifica anche `sub_4174A0` / `sub_4178B0` / `sub_417C20` per capire se il problema è:
     (a) `unk_5994E0` / `unk_51C008` mai popolata (es. colpa di `p_GetEntries` / `s_GetPalette` / `SC_SKIP_INTRO` / ordine palette / PCX loader / FPU `ftol`), oppure
     (b) `sub_4178B0` / `sub_417C20` azzera `0x006D05B0`, oppure
     (c) `sub_4174A0` genera `0x00` per un difetto x87/IR/runtime!
2. **Prototipo & Verifica su binario privato (`build/qa/solve-lut/starcraft-x86-lut`):**
   - Applica il fix nella tua copia privata `build/qa/solve-lut/src/` e compila/linka usando la ricetta esatta di `tools/link-x86.sh` (`$PWD/build/lift-full/entry-x86/{StarCraft,storm}.entry.o` **assoluti**, `--redefine-syms=runtime/shim-port/shim_rename.txt` per eventuali shim `.c`, `guest_data_link.ld`, `runtime_bounds.ld`).
   - Verifica con `SC_BIN="$PWD/build/qa/solve-lut/starcraft-x86-lut" ./tools/run-isolated.sh start --id solve-lut-fix --skip-intro 1 --smack 1`:
     - Cattura e leggi con `read`:
       1) `hover-{0,150,400,1000}ms.png` su `Single Player` (`move 220 120`),
       2) `gametype.png` (`Select Game Type`),
       3) `registry.png` (`Registry`),
       4) `menu-ingame.png` (`MENU` in Boot Camp).
     - Verifica che i pannelli siano **semitrasparenti/tinti** e NON neri opachi, e che l'hover non renda nere le scritte!
3. **Consegna:**
   - Ferma `./tools/run-isolated.sh stop --id solve-lut` e `solve-lut-fix`.
   - Salva il diff unificato (`diff -u` contro i file originali in `runtime/`) in `build/qa/solve-lut/fix.patch`.
   - Scrivi `reports/SOL-solve-lut.md` e `reports/STATE-solve-lut.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
