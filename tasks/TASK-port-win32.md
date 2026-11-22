# Preludio obbligatorio
- Oracolo primario: IDA / `storm.orig.dll` / `StarCraft.orig.exe`, sorgenti in `~/dev/starcraft-reasm-smacker/`.
- **Proprietà esclusiva di questo agente (NON toccare file altrui):**
  - `runtime/shim-port/elf/win32_shim.c`
  - `tests/x86_vm_pool_test*`, `tests/x86_io_readahead_test*`
- **NON modificare `runtime/common/`, `host_elf.c`, `smack_*`, `tools/`, `build/starcraft-x86`.**
- NON lanciare altri sottoagenti.

# Missione: Porting A2-win32 + A3 (`VirtualAlloc` 24 MiB pool) + A4 (`64 KiB` `pread64` read-ahead cache) in `win32_shim.c`

Leggi `/home/enne2/dev/starcraft-reasm-smacker/HANDOFF-PORTING.md` (sezioni 1 e 4.4).

1. **Analisi differenziale accurata (`b77bf0d..HEAD` vs nostro `win32_shim.c`):**
   - Estrai `git -C ~/dev/starcraft-reasm-smacker diff b77bf0d..HEAD -- runtime/shim-port/elf/win32_shim.c` per isolare esattamente:
     - **A2 (`daaf929`)**: chiamate `arm_shadow_set_range` su `VirtualAlloc`/`VirtualFree` (nota: poiché `game_memory_arm.h` viene aggiornato in parallelo dall'agente dispatch, guarda le firme esatte in `~/dev/starcraft-reasm-smacker/runtime/common/game_memory_arm.h` e compila i tuoi test standalone puntando a `-I ~/dev/starcraft-reasm-smacker/runtime/common` se necessario, senza toccare `runtime/common/`!).
     - **A3 (`4abab68`)**: pool `VirtualAlloc` / `VirtualFree` fino a `24 MiB` (`MEM_RELEASE` freelist RW).
     - **A4 (`af57767`)**: cache read-ahead `64 KiB` (`pread64`) per handle `CreateFileA` read-only + prototipo `fsync`.
   - **ATTENZIONE CRITICA — PRESERVA TUTTI I NOSTRI FIX IN `runtime/shim-port/elf/win32_shim.c`:**
     - `CREATE_NEW` (`O_CREAT|O_EXCL`) & creazione componente foglia in `reasm_open_ci` (fondamentale per `Save/*.sng`!).
     - `EnumWindows` i386 tramite `arm_guest_calln`, `FindWindowA`/`FindWindowExA`/`GetLastActivePopup`/`WaitForInputIdle`, `RaiseException`/`RtlUnwind` fail-closed (`exit 70`), `GetCurrentThreadId` via `SYS_gettid`, `GetTimeZoneInformation`, `CreateFileMappingA`/`MapViewOfFile`/`UnmapViewOfFile` (che rifiuta i vfile `standard.snp`), `FlushFileBuffers` (`fsync`), `SetEndOfFile` (`ftruncate`), `IsDBCSLeadByte`, dispatch a `win32_shell_dlg`.
     - **Regola di coerenza A4 (`HANDOFF-PORTING.md` §4.4):** verifica che `WriteFile`, `SetEndOfFile`, `SetFilePointer` (se scrivibile o su seek/close), `CloseHandle` invalidino o aggiornino la cache read-ahead (`file_cache_invalidate`) e che la cache sia usata solo per file regolari read-only (mai vfile o pipe/stdout!).
2. **Test di verifica:**
   - Copia `tests/x86_vm_pool_test*` e `tests/x86_io_readahead_test*` da `~/dev/starcraft-reasm-smacker/tests/`.
   - Compila ed esegui in `build/qa/port-win32/`:
     - `x86_vm_pool_test` (compilando `win32_shim.c` se il test linka con esso, o eseguendo `tests/x86_vm_pool_test/check.sh`)
     - `x86_io_readahead_test` (`tests/x86_io_readahead_test/check.sh`)
     - `tests/x86_win32_shim_test.c` (verifica zero regressioni su tutti i nostri fix Win32: `CREATE_NEW`, `EnumWindows`, `CreateFileMappingA`, `GetCurrentThreadId`, ecc.!).
3. **Consegna:**
   - Scrivi `reports/REPORT-port-win32.md` e `reports/STATE-port-win32.md` (max 70 righe).
