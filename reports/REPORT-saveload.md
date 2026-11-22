# Report — scx86-saveload

## Esito e limiti
Implementazione preliminare isolata in `runtime/shim-port/elf/win32_shell_dlg.{c,h}`. Non ho modificato i manifest o `win32_shim.c` (fuori proprietà). La compilazione i686 dei due translation unit passa; l'esecuzione del test non è stata eseguita/linkata. IDA non ha aperto il PE (`ida_open`: Failed to open database), quindi le evidenze binarie qui sotto vengono da `llvm-objdump`, import table e LLASM; non dichiarare questa analisi come revisione IDA completata.

## Call-site / evidenze
`llvm-readobj --coff-imports build/lift-full/StarCraft.orig.exe` conferma import di `FindExecutableA`, `SHGetPathFromIDListA`, `SHGetSpecialFolderLocation`, `ShellExecuteA`; `GetSaveFileNameA` è import di Storm (`externals-storm.txt`), non dell'EXE.

- EXE `0x004EDF59`: call IAT `[0x004FE26C]` (ShellExecuteA); `0x004EDF6F`: call `[0x004FE268]` (SHGetPathFromIDListA).
- EXE `0x004EDF58`/LLASM `loc_4EDF58`: SHGetSpecialFolderLocation; `0x004EDF75`: success continuation dopo SHGetPathFromIDListA.
- EXE `0x0044A336` e `0x0044D839`: ShellExecuteA, preceduti dai rispettivi `FindExecutableA` a `0x0044A308`/`0x0044D803` (call tramite EDI dopo load IAT).
- Storm LLASM `loc_150357CB`: `GetSaveFileNameA`, con OPENFILENAMEA impostato in funzione precedente; il sorgente LLASM mostra il call a `seg01_code.llinc:311670-672`.
- Non risultano import o riferimenti a `GetOpenFileNameA` nei file lift cercati. Per i dati reali dei CSIDL/OPENFILENAMEA, non è stata completata una validazione IDA; il mapping implementato è prudenziale e va verificato sull'uso effettivo.

Call-site riproducibili: `build/lift-full/run-exe/seg01_code.llinc` circa 1277590-1277731, 414547-414621, 431960-432045; `build/lift-full/run-storm/seg01_code.llinc:311662-311672`.

## Implementazione e caveat
- SHGet* usa token PIDL sintetici (puntatori a stringhe persistenti) e `SC_ROOT` con directory Save/AppData/Programs.
- File dialog deterministic-only: richiede `SC_SAVE_FILE`/`SC_LOAD_FILE`; senza variabile torna FALSE (nessun dialogo bloccante). L'aggiunta dell'estensione avviene se assente.
- ShellExecuteA è bloccata con log e `SE_ERR_FNF` (2); FindExecutableA fallisce con stesso codice.
- Caveat: mkdir/path/CSIDL mapping richiede ulteriore verifica: CSIDL_PERSONAL non equivale necessariamente alla directory Save del gioco. Inoltre `snprintf` controlla solo errore negativo, non troncamento. Prima dell'integrazione correggere/validare questi punti e aggiungere al set shim del build.

## Test ABI
Comando: `clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O0 -Wno-incompatible-library-redeclaration -c runtime/shim-port/elf/win32_shell_dlg.c -o build/qa/saveload/win32_shell_dlg.o`; test translation unit compilato analogamente in `build/qa/saveload/test.o`.
`llvm-objdump -d` verifica `ret 0xc` SHGetSpecialFolderLocation, `ret 0x8` SHGetPathFromIDListA, `ret 0x4` entrambi i dialoghi, `ret 0x18` ShellExecuteA, `ret 0xc` FindExecutableA.

## Integrazione finale richiesta
Aggiungere al set `SHIM(...)`:
```
SHIM(GetSaveFileNameA, 1, 1, 0)
SHIM(GetOpenFileNameA, 1, 1, 0)
SHIM(SHGetSpecialFolderLocation, 3, 3, 0)
SHIM(SHGetPathFromIDListA, 2, 2, 0)
SHIM(ShellExecuteA, 6, 6, 0)
SHIM(FindExecutableA, 3, 3, 0)
```
Aggiungere `win32_shell_dlg` alla lista `shim_files` di `tools/build-shim-x86.sh` e collegare `win32_*_lookup`/exports nel dispatch (`win32_shim.c`), a cura dell'integrazione. Generatore deve produrre rename per i nomi Win32 secondo il manifest; non ho modificato `shim_rename.txt` come richiesto.
