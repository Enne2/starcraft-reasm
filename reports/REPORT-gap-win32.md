# Report — gap-win32

## Esito sintetico

Implementato il callback i386 di `EnumWindows` con `arm_guest_calln`, aggiunta la traccia diagnostica limitata della catena SEH emulata e il fallback esplicito `SC_SAVE_AUTO=1` per sole estensioni `.sav`/`.rep`. I due test richiesti passano. La prova interattiva reale di Boot Camp e di Save/Load non è riuscita: il binario privato termina prima di creare una finestra, con SIGSEGV nella traduzione di Storm. Non sono stati osservati né un salvataggio `.sav` né un caricamento.

## Modifiche

- `runtime/shim-port/elf/win32_shim.c`
  - `EnumWindows` invoca il callback guest con `arm_guest_calln(callback, 2, {hwnd, lparam}, &status)` su i386 e ARM; rifiuta callback nullo/helper assente e interpreta separatamente il valore EAX restituito dal callback e il codice `status`.
  - `RaiseException` e `RtlUnwind` registrano `fs:[0]` dal TEB emulato e percorrono al massimo 16 record. Ogni record è letto solo se allineato e mappato; la funzione termina comunque con exit 70. Nessun unwind viene simulato.
- `runtime/shim-port/elf/win32_shell_dlg.c`
  - `SC_SAVE_AUTO=1`, quando manca la variabile `SC_SAVE_FILE`/`SC_LOAD_FILE`, seleziona `Save/autosave.sav` per `.sav` e `Maps/replays/auto.rep` per `.rep`; altre estensioni non sono reindirizzate. Questo evita di trasformare il dialogo PCX di screenshot di Storm in un salvataggio `.sav`.
- `tests/x86_win32_shim_test.c`
  - Copertura callback (HWND/lParam, ritorno vero/falso, callback nullo e errore di risoluzione) e test dei log della catena SEH per entrambe le API, mantenendo exit 70.
- `tests/x86_saveload_test.c`
  - Copertura modalità manuale, auto `.sav` per Save/Load e rifiuto della deviazione automatica `.pcx`.

## Evidenze sul binario originale

- `IDA` su copie QA dei binari, più `llvm-objdump -d`/`-p` e ricerca mirata nei file `.llasm`:
  - `StarCraft.orig.exe` importa `EnumWindows` a IAT `0x004FE32C`; xref a `0x004841DE`, `0x004D18B1` e `0x004DCC77`. Le funzioni decompilate passano `EnumFunc` (`0x004DC6D0`) e un `lParam` derivato dal PID.
  - `RtlUnwind` è importato a `0x004FE0B0`, con thunk `0x0040AACE`; `RaiseException` è importato a `0x004FE25C`, chiamato da `__raise_exc` (`0x004FB90E`, call a `0x004FBB03`). Le dichiarazioni sono presenti in `build/lift-full/StarCraft.llasm` e `storm.llasm`.
  - `storm.orig.dll` importa `GetSaveFileNameA` a `0x150453BC`; IDA individua il call-site `0x150357C5` nella funzione `storm_342` (`0x15035620`). La funzione configura il titolo `Save Screen Capture`, filtro Paintbrush/PCX ed estensione `.pcx`: questa prova identifica un dialogo per screenshot, non il dialogo Save Mission. Non risulta importato `GetOpenFileNameA` nei binari ispezionati. Il percorso in-game Save/Load resta quindi non classificato, perché la UI non è stata raggiunta.
- `runtime/common/guest_call_arm.c` conferma la firma `int arm_guest_calln(uint32_t proc, unsigned nargs, const uint32_t *args, int *status)`; il ramo `__i386__` usa `x86_call_guest_fast` per entrare nel codice LLASM.
- Il TEB emulato espone `X86_ReadFsDword(0)`. Il walker SEH legge i record attraverso `X86_ReadMemProcedure` solo dopo `arm_va_is_mapped`; non tenta di eseguire handler né di effettuare unwind.

## Verifica

- `build/qa/gap-win32/x86_win32_shim_test` — **PASS**.
- `build/qa/gap-win32/x86_saveload_test` — **PASS**.
- Binario privato `build/qa/gap-win32/starcraft-x86-win32`: ELF32/EXEC, Intel 80386; link effettuato usando gli oggetti esistenti in `build/obj-x86/` senza ricostruirli.
- Nota di perimetro: il primo test Save/Load usava ancora il path hard-coded `build/qa/saveload/sandbox`; `SHGetSpecialFolderLocation` crea le directory. Ho poi corretto il test su `build/qa/gap-win32/sandbox`; i test finali usano solo il percorso assegnato. Non ho rimosso la directory legacy, eventualmente creata/toccata, perché fuori dai file di proprietà.
- Avvio con `SC_BIN=... ./tools/run-isolated.sh start --id gap-win32 --skip-intro 1 --smack 1`: il processo muore prima della finestra. Log: SIGSEGV, PC host `0x188717A4`, procedura guest `0x15044C73` (offset `0x3814`), fault address `0x1505FC20`. Screenshot isolato `build/qa/gap-win32/bootcamp-unreached.png` nero. L’istanza è stata chiusa con `stop`; nessun input è stato inviato al desktop dell’utente e l’audio era dummy.

## Limiti e follow-up

Non è stato possibile entrare in Boot Camp, cliccare Save Mission/Load Mission o verificare scrittura e ricaricamento di un `.sav`. La schermata nera e il crash precoce sono un limite della build/runtime, non evidenza sul funzionamento dei menu di salvataggio. D19 è rimasta invariata: `RaiseException`/`RtlUnwind` continuano a fallire chiusi con exit 70.

## Decisioni autonome

- Ho mantenuto fail-closed per SEH e non ho simulato un unwind privo di supporto reale.
- Ho limitato `SC_SAVE_AUTO` alle estensioni `.sav` e `.rep`: l’unico call-site individuato per `GetSaveFileNameA` è un dialogo PCX per screenshot, quindi il fallback generico sarebbe semanticamente pericoloso.
- Poiché il gioco termina prima della finestra, non ho inviato input al runner né dichiarato verificato il percorso di Save/Load in game; ho acquisito e ispezionato lo screenshot nero e poi arrestato l’istanza.

## File aggiornati

`runtime/shim-port/elf/win32_shim.c`, `runtime/shim-port/elf/win32_shell_dlg.c`, `tests/x86_win32_shim_test.c`, `tests/x86_saveload_test.c`, `reports/REPORT-gap-win32.md`, `reports/STATE-gap-win32.md`. Gli artefatti finali di compilazione/prova sono in `build/qa/gap-win32/`; resta la nota sul path legacy sopra.
