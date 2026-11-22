# Stato del link e dell'esecuzione ARM — verifica 2026-11-20

## Esito: NON eseguibile (nessun binario ARM esiste)

Non c'è alcun `starcraft-armv6` su disco: il lavoro si ferma a `StarCraft.o` (13 MB) e
`storm.o` (3,6 MB). Provando a linkare si ottengono errori strutturali, non di dettaglio.

## Catena di link verificata (funziona)

`clang --target=armv6-unknown-linux-gnueabihf --sysroot=build/sysroot` → `ld.lld` (da rustup,
unico linker disponibile che conosca `armelf_linux_eabi`) → `libc.so.6` del sysroot →
eseguito con `qemu-arm-static -L build/sysroot`: stampa `link-chain-ok`, exit 0.
Nota: `arm-none-eabi-ld` della toolchain zero95 **non** serve (supporta solo `armelf`, bare metal).

## Aggiornamento: gli shim esistevano già, mancava il rename

Correzione rispetto alla prima analisi. Gli shim **sono compilati**, in due set:

- `build/runtime-arm/shim-port/obj/*.o` — 14 oggetti C (win32_shim, gdi_elf, com_ddraw,
  com_dsound, input_elf, dlg_elf, smack_*, snp_elf, host_elf, main_elf, win32_game/ui, storm_ext)
- `build/runtime-arm/game/obj/*.o` — lato **guest ABI** (bridge_arm, bridge_stubs.S,
  bridge_symbols, bridge_table, game_main_arm, guest_call_arm, game_memory_arm, cpu_llasm,
  llasm_*, x87_crt_arm, win32_ext_arm)

Il motivo per cui «non venivano usati»: il bridge chiama i thunk `AddAccessAllowedAce__c`,
mentre gli oggetti shim espongono il nome originale `AddAccessAllowedAce`. La rinominata è
prevista da `runtime/arm/gen_shims_bridge.py` ed è materializzata in
`build/runtime-arm/game/shim_rename.txt`, ma **non era stata applicata** agli oggetti.
Applicandola con `llvm-objcopy --redefine-syms` (151 simboli `__c` creati), i simboli
mancanti passano da **443 a 8**.

Pipeline corretta e riproducibile: `tools/link-arm.sh`.

## Cosa manca davvero: 8 funzioni di glue

Dichiarate in `runtime/arm/elf_support_arm.h`, **mai implementate** in nessun `.c`:

| simbolo | ruolo |
|---|---|
| `winmain_trap` | entra in WinMain dal `main` host (entry point del gioco) |
| `reasm_iat_install` | installa la tabella IAT nella memoria emulata |
| `reasm_call_on_stack` | chiamata con argomenti sullo stack emulato |
| `reasm_storm_ordinal_va` | risolve un ordinal di storm.dll in VA |
| `g_imports`, `g_imports_n` | tabella degli import (dati) |
| `build_guard_check` | controllo di identità della build |
| `loc_404C21` | funzione del lift referenziata per nome ma assente dall'oggetto |

## Perché non si può linkare così com'è (analisi iniziale, superata)

1. **Nessun entry point.** Il `.ll` contiene **71.112** funzioni, tutte dichiarate
   `define private fastcc void @loc_XXXXXX(%_cpu* %cpu)`. Sono `private` → nel `.o` non
   esistono come simboli globali (la `.symtab` ha solo 2 entry in `.rodata`). Manca quindi
   un `main`/`_start` esportato: serve un **wrapper scritto dentro lo stesso modulo** che
   allochi la struct `%_cpu` e chiami l'entry del gioco.
2. **Import Win32 non coperti.** Simboli esterni richiesti:

   | Sorgente | undefined |
   |---|---|
   | StarCraft.o | 400 |
   | storm.o | 306 |
   | **totale unici** | **706** |
   | definiti da `libruntime-arm.a` | 247 |
   | **coperti** | **73** |
   | **MANCANTI** | **443** |

   Classificazione dei 443 mancanti:
   - USER32 (finestre, messaggi, cursori, menu): ~67
   - GDI (DC, bitmap, penne, testo, regioni): ~22 + altre nel campione
   - kernel32/advapi32/ole/wave (file, heap, processi, registro, audio): ~72
   - **`crt_unlifted_<addr>`** (funzioni CRT che SRW non è riuscito a liftare, blocco
     MSVC ≥ 0x1503FFE4 per storm, ≥ 0x4F7B18 per l'exe: x87/SSE non tradotti)
   - resto: stringhe/compare, dialogs, COM, ecc.
3. **Il link attuale si ferma prima** solo perché `smoke_main.o` (dentro l'archivio) viene
   pescato per soddisfare `main` e tira dentro le SDL: 13 undefined fittizi. Va escluso o
   sostituito dal wrapper vero.

## Piano minimo per vedere StarCraft sul server X dell'emulatore

Fase 1 — processo vivo fino a WinMain e finestra creata:
- wrapper `main` nel modulo LLVM (nuova funzione esportata, stessa TU): alloca `%_cpu`,
  imposta stack/registri iniziali, chiama `loc_4F7C8D` (entry CRT dell'exe) e gestisce il
  ritorno; identico wrapper per storm (`loc_1503FFE4`-ish) se serve come DLL.
- shim USER32 minimi: `RegisterClassA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`,
  `GetMessageA`/`PeekMessageA`, `DispatchMessageA`, `DefWindowProcA`, `BeginPaint`/`EndPaint`,
  `GetClientRect`, `SetTimer`/`KillTimer`, `GetSystemMetrics`, `LoadCursorA`/`SetCursor`,
  `TranslateMessage`, `PostQuitMessage`.
- shim GDI minimo su framebuffer (o SDL): `CreateCompatibleDC`, `CreateDIBSection`,
  `SelectObject`, `BitBlt`, `SetDIBitsToDevice`, `DeleteObject`, `GetDeviceCaps`.
- kernel32: `VirtualAlloc/VirtualFree`, `CreateFileA`/`ReadFile`/`SetFilePointer` (per gli MPQ),
  heap, `CreateThread` (o stub sincrono), `QueryPerformanceCounter`, `Sleep`, `GetLocalTime`.

Fase 2 — CRT non liftato:
- i `crt_unlifted_*` vanno implementati (o mappati) a mano: sono le routine x87/SSE che SRW
  ha lasciato fuori; con il runtime `llasm_float`/`llasm_divide` già presenti si può coprire
  la maggior parte (`__ftol2`, `__CIpow`, `fdiv`, `fclex`, `fldpi`).

Fase 3 — prove:
- `qemu-arm-static -L build/sysroot starcraft-armv6` per il primo crash/exit;
- poi dentro la VM (guest `raspi0`) con symlink degli MPQ da `~/dev/final-starcraft/Starcraft/`
  e `DISPLAY=:0` sul server X già attivo (`minimal-x.service`).

## Nota di metodo

Il conteggio dei simboli va rifatto dopo ogni modifica al lift: le funzioni `private` non
compaiono in `nm` e questo maschera errori. Comando usato:
`llvm-nm --undefined-only --extern-only StarCraft.o` e `--defined-only` sull'archivio.
