# Report — TASK-p-winproc

## Sintesi

Modificato esclusivamente `runtime/shim-port/elf/win32_shim.c`; aggiunto `tests/x86_win32_shim_test.c`. Output isolati in `build/qa/winproc/`. Le API Win32 nuove o corrette sono registrate nella lookup interna dello shim; non ho modificato `bridge_unk.def`, `bridge_shims.def` o gli altri file vietati.

## Oracolo e call-site

- `llvm-objdump -d --start-address=0x15014c00 --stop-address=0x15014d30 build/lift-full/storm.orig.dll` mostra a **storm 0x15014C9C** la call indiretta tramite IAT `calll *0x15045124`, preceduta da sei push (ordine: `hFile`, zero, `PAGE_READONLY=2`, zero, zero, zero): arità `CreateFileMappingA` = 6, confermata dal prototipo Win32 e da `SHIM(CreateFileMappingA, 6, 6, 0)`. A **0x15014CB6** la successiva call IAT prende cinque argomenti (`MapViewOfFile`); la vista viene mappata con accesso `FILE_MAP_READ` e dimensione zero. Questo percorso su `standard.snp` non passa a un fd reale: `CreateFileA` lo apre tramite `win32_snp_vfile_open`; la nuova `CreateFileMappingA` riconosce `win32_snp_vfile_own` e fallisce pulitamente.
- `llvm-readobj --coff-imports` conferma gli import richiesti da StarCraft e storm: `EnumWindows`, `FindWindowA`, `GetLastActivePopup`, `WaitForInputIdle`, `FindWindowExA`, `RaiseException`, `RtlUnwind`, `GetCurrentThreadId`, `GetTimeZoneInformation`, `CreateFileMappingA`, `MapViewOfFile`, `UnmapViewOfFile`, `FlushFileBuffers`, `SetEndOfFile`, `IsDBCSLeadByte`.
- Le LLASM elencano questi import esterni (`build/lift-full/StarCraft.llasm`, `storm.llasm`), ma le call vengono risolte attraverso IAT indirette. IDA non ha accettato l'apertura di `StarCraft.orig.exe` (`ida_open` errore: Failed to open database) e non c'erano sessioni già attive: quindi non dichiaro verificati singolarmente gli indirizzi dei call-site di ogni import. L'analisi dell'uso effettivo è completa per il call-site Storm sopra, non per tutti gli import.

## Modifiche

- **Finestre**: `FindWindowA` confronta classe/titolo della finestra attiva/foreground registrata da `win32_ui`; `FindWindowExA` tratta la sola finestra top-level (rifiuta `parent`/`after` non null); `GetLastActivePopup(hwnd)` restituisce `hwnd`; `WaitForInputIdle` restituisce `WAIT_OBJECT_0`. `EnumWindows` invoca il callback guest con due argomenti tramite `arm_guest_calln` su ARM. Su i386 non esiste nel runtime un trampoline guest-callback utilizzabile dallo shim: ritorna `FALSE` senza saltare a un indirizzo guest come fosse un puntatore host.
- **SEH**: `RaiseException` (4 argomenti) e `RtlUnwind` (4 argomenti) registrano `[win32-seh]` con codice/flags e argomenti. Non viene simulato un unwind: quando raggiunte, entrambe terminano il processo con exit 70, fail-closed e senza trampolino “API NON IMPLEMENTATA”. La semantica TEB `fs:[0]` non è stata implementata.
- **Thread ID**: `GetCurrentThreadId` usa `syscall(SYS_gettid)` numerico Linux i386/ARM, non un valore costante. Due pthread nel test hanno ID distinti.
- **Timezone**: `GetTimeZoneInformation` azzera il buffer Win32 da 172 byte, popola `Bias` da `timezone` e i nomi standard/daylight in UTF-16 da `tzname`; il delta DaylightBias resta zero perché l'ABI freestanding non fornisce in modo portabile il delta effettivo. Ritorna stato coerente con nomi distinti/uguali (2/0).
- **Mapping**: mapping file-backed con handle privato, `mmap`/`munmap`, limiti 32-bit e gestione del ciclo `CloseHandle` del mapping. I vfile SNP vengono rifiutati; i fd reali sono supportati. Mapping >4 GiB e offset non page-aligned sono rifiutati dal kernel/per vincolo 32-bit.
- **File I/O**: `FlushFileBuffers` usa `fsync`; `SetEndOfFile` tronca il fd alla posizione corrente con `ftruncate`. Per vfile SNP entrambe falliscono. `IsDBCSLeadByte` resta sempre falso, coerentemente con `GetACP()==1252` (Windows-1252, SBCS).

### SHIM già presenti in `runtime/shim-port/bridge_shims.def`

`CreateFileMappingA`, `FlushFileBuffers`, `GetCurrentThreadId`, `GetTimeZoneInformation`, `IsDBCSLeadByte`, `MapViewOfFile`, `SetEndOfFile`, `UnmapViewOfFile`.

Le API finestra e SEH menzionate sopra restano dichiarate UNK nel manifest condiviso (file non autorizzato); ora sono risolte dalla lookup di `win32_shim.c`. Non ho aggiunto nuovi `SHIM(...)` al manifest.

## Verifica

- Oggetto richiesto compilato con target i686 e i flag del build shim: `build/qa/winproc/win32_shim.o`.
- Test autonomo compilato ed eseguito: `build/qa/winproc/x86_win32_shim_test` → `x86_win32_shim_test: PASS`. Verifica thread ID distinti, class/title lookup e casi non corrispondenti, limiti `FindWindowExA`, popup/input idle, comportamento callback i386, rifiuto vfile SNP, mapping/mmap di file reale, unmap/close, fsync/ftruncate, struttura timezone, DBCS e terminazione fail-closed SEH in child process.
- `llvm-objdump -d` sullo shim ha confermato `ret N` stdcall: `EnumWindows 8`, `FindWindowA 8`, `FindWindowExA 16`, `GetLastActivePopup 4`, `WaitForInputIdle 8`, `CreateFileMappingA 24`, `MapViewOfFile 20`, `UnmapViewOfFile 4`, `GetCurrentThreadId 0`, `GetTimeZoneInformation 4`, `IsDBCSLeadByte 4`, `FlushFileBuffers 4`, `SetEndOfFile 4`. `RaiseException` e `RtlUnwind` sono intenzionalmente noreturn: non hanno `ret`, terminano con exit 70.

## Decisioni autonome

- Per callback `EnumWindows` i386: non eseguire codice guest con ABI host errata; restituire `FALSE` finché il runtime non offre un guest-call trampoline i386.
- Per eccezioni/unwind non gestiti: fail-closed con log e exit 70 invece di ritornare un successo fittizio.
- Per `standard.snp`: trattarlo come vfile e rifiutare mapping POSIX, senza cambiare il provider SNP.

## Limiti residui

Non è implementato l'unwind SEH reale sul TEB; `EnumWindows` callback è supportato solo su ARM. I call-site di tutte le API importate non sono stati individuati individualmente, per il fallimento dell'apertura IDA; l'unico call-site di cui ho decomposto la sequenza di argomenti è quello Storm `0x15014C9C` (più `MapViewOfFile` a `0x15014CB6`). Nessun test integrato di avvio StarCraft è stato eseguito.
