# STATE — gap-win32

- `EnumWindows`: callback i386+ARM tramite `arm_guest_calln`; controllo callback/helper e gestione distinta di EAX/status.
- SEH: `RaiseException`/`RtlUnwind` loggano `fs:[0]` + catena mappata (max 16 record), poi exit 70; nessun unwind finto.
- `SC_SAVE_AUTO=1`: fallback per `.sav` (`Save/autosave.sav`) e `.rep` (`Maps/replays/auto.rep`); estensioni non supportate restano non selezionate.
- Test: `x86_win32_shim_test` PASS; `x86_saveload_test` PASS.
- Binary privato ELF32/EXEC i386: `build/qa/gap-win32/starcraft-x86-win32`.
- Runtime isolato non raggiunge la finestra: SIGSEGV prima del menu (guest `0x15044C73`, fault `0x1505FC20`); screenshot nero `build/qa/gap-win32/bootcamp-unreached.png`.
- IDA/objdump: import StarCraft `EnumWindows` IAT `0x004FE32C`; xref `0x004841DE`, `0x004D18B1`, `0x004DCC77`. `RaiseException` call `0x004FBB03`; `RtlUnwind` thunk `0x0040AACE`.
- Storm `GetSaveFileNameA`: call `0x150357C5` in `storm_342` (`0x15035620`), titolo “Save Screen Capture”, filtro/estensione PCX. Non prova Save Mission; Load/Save in-game non verificati.
- Runner `gap-win32` arrestato con `stop`; niente finestre/audio sul desktop.

## Decisioni autonome

Conservati D19 e il fail-closed. Fallback auto ristretto a `.sav`/`.rep` per non dirottare il dialogo PCX di Storm. Nessuna affermazione di Save/Load reale: il crash impedisce la verifica.
