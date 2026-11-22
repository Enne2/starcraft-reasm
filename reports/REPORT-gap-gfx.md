# Report — scx86-g-gfx2 (21/11/2026)

## Esito

Completato solo il sottoinsieme di build/diagnostica; la missione end-to-end resta **bloccata/non superata**.

- Compilati in `build/qa/gap-gfx/` `com_ddraw.c`, `gdi_elf.c`, `smack_elf.c`, `smack_audio.c` e `storm_sdraw_arm.c` con target i686. Header reale verificato in `/home/enne2/dev/reasm-toolkit/runtime/elf/reasm_elf.h`; per questo checkout il percorso `-I` corretto è quella cartella stessa.
- Creato `tests/x86_gfx_test.c` e compilato il suo oggetto ELF32. Il test non passa: terminazione con codice 1 sulla seconda `CreateSurface` (linea 45). Prima di poter giudicare il clipping/SetDIBits occorre correggere l'ABI dei function pointer COM (metodi DirectDraw sono `stdcall`; i cast nel test corrente sono cdecl). Non dichiarare quindi verificati i test pixel o SetDIBits.
- Link privato riuscito: `build/qa/gap-gfx/starcraft-x86-gfx`, ELF32 Intel 80386. Link effettuato senza modificare `build/starcraft-x86`, gli oggetti in `build/obj-x86` o gli entry object; gli oggetti shim di provenienza sono stati esclusi quando sostituiti dai corrispondenti oggetti di `build/qa/gap-gfx/`.
- Avvio richiesto con `SC_BIN=... ./tools/run-isolated.sh start --id gap-gfx --skip-intro 1 --smack 1`: processo di gioco uscito subito. `build/isolated/gap-gfx/game.log` registra SIGSEGV ARM guest, fault PC `0x188717A4`, procedura tradotta `0x15044C73 + 0x3814`, indirizzo `0x1505FC20`, guest EIP nullo. Non è raggiunto il menu né la schermata `26-briefing`; nessuno screenshot nuovo e nessuna diagnosi/fix ai ritratti.
- Eseguito `stop --id gap-gfx`; verifica finale: game e Xvfb spenti. Nessuna schermata Boot Camp, raccolta minerali o Refinery verificabile.
- Eseguito `llvm-objdump -d` sui binari originali richiesti: `build/lift-full/StarCraft.orig.exe` e `build/lift-full/storm.orig.dll` (formato COFF i386). Le sessioni IDA già aperte per questi originali non sono state toccate.
- Non modificati SCode, `proc_range`, D19, file vietati, né `build/starcraft-x86`. Nessuna azione desktop/input/audio reale.

## Percorsi modificati/creati

- `tests/x86_gfx_test.c`
- `build/qa/gap-gfx/` (oggetti, test e binario privati)
- `reports/REPORT-gap-gfx.md`
- `reports/STATE-gap-gfx.md`

`runtime/shim-port/elf/com_ddraw.c`, `gdi_elf.c`, Smack e SDraw non sono stati modificati in questa esecuzione: i tentativi non hanno prodotto una correzione dimostrata.

## Evidenze e prossimi passi

1. Correggere le dichiarazioni/call ABI `stdcall` nel test, quindi ripetere pixel test su SetClipList/GetClipList/IsClipListChanged, Set/GetClipper, Blt/BltFast e conversioni SetDIBits 8/24/32 con stride.
2. Indagare il fault guest del binario privato prima di riavviare; conservare il fail-closed e non alterare `proc_range`.
3. Solo dopo l'avvio stabile acquisire `26-briefing`, correlare i log mirati Smack/Storm/SetPalette/Blt, e proseguire Boot Camp fino alla Refinery.

## Decisioni autonome

- Ho scelto di fermare il lavoro gameplay dopo il crash guest e di non tentare scorciatoie (patch di fault, SCode non verificato o disattivazione di `proc_range`).
- Ho terminato l'istanza isolata e lasciato spenti gioco/Xvfb. Il test incompleto è conservato come base, ma il report non attribuisce significato di PASS ai suoi controlli non raggiunti.
