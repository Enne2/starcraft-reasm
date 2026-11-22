# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/StarCraft.orig.exe` e `storm.orig.dll` (e `rg` mirato su `build/lift-full/*.llasm`) per verificare ogni call-site e arità stdcall!
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: comportamento fedele a Win32 o errore dichiarato; mai finte eccezioni o stub silenziosi pericolosi.
4. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (NON toccare `bridge_shims.def`, `shim_rename.txt`, `bridge_unk.def`, `build/starcraft-x86`, `build/obj-x86/`, `host_elf.c`, `snp_elf.c`, `com_*.c`.)
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-winproc`: Finestre (`EnumWindows`/`FindWindow*`), SEH (`RaiseException`/`RtlUnwind`), `GetCurrentThreadId`, `CreateFileMappingA` & Win32 completeness in `win32_shim.c`

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/win32_shim.c` (sei l'UNICO agente a modificare `win32_shim.c`!)
- `tests/x86_win32_shim_test.c` (nuovo)
- `build/qa/winproc/` (output compilazione/test privati)
- `reports/REPORT-winproc.md`
- `reports/STATE-winproc.md` (max 70 righe)

## Obiettivi
1. **Window enumeration & focus (`5` API in `bridge_unk.def`)**:
   - Ispeziona i call-site di `EnumWindows`, `FindWindowA`, `FindWindowExA`, `GetLastActivePopup`, `WaitForInputIdle` in `StarCraft.orig.exe` / `storm.orig.dll`.
   - Implementali in `win32_shim.c` interrogando la finestra registrata in `win32_ui` (es. `win32_ui_main_hwnd()` / `SWarClass`):
     - `FindWindowA` / `FindWindowExA`: confronta classe (`SWarClass`) / titolo e ritorna `HWND` registrato o `0`.
     - `EnumWindows`: per-port usa `arm_guest_call2` (o verifica l'arità `BOOL CALLBACK EnumWindowsProc(HWND, LPARAM)` con 2 argomenti stdcall — controlla `guest_call_arm.c`!) oppure ritorna subito se non ci sono altre finestre, gestendo correttamente il callback guest su `__arm__`/`__i386__`!
     - `GetLastActivePopup(hwnd)` → `hwnd` (o popup attivo se presente).
     - `WaitForInputIdle(hProc, ms)` → `0` (input già pronto).
2. **SEH (`RaiseException`, `RtlUnwind`)**:
   - Verifica i call-site in `StarCraft.orig.exe` / `storm.orig.dll` (`llvm-objdump -d` / `llasm`).
   - Implementa `RaiseException` e `RtlUnwind` in modo sicuro (log dettagliato con codice eccezione/flags/argomenti + comportamento coerente per SEH Win32 su TEB `fs:[0]` o fail-closed esplicito se eccezione non gestita invece di crashare con "API NON IMPLEMENTATA").
3. **Completezza Win32 parziali in `win32_shim.c`**:
   - **`GetCurrentThreadId()`**: attualmente ritorna sempre `1`! Usa il TID reale per-thread (`syscall(SYS_gettid)` o `__thread uint32_t` monotono per thread, garantendo che il main thread e i worker MPQ abbiano TID distinti, fondamentale per ownership di `CRITICAL_SECTION` / `Storm`!).
   - **`GetTimeZoneInformation`**: popola `Bias` / nomi standard reali da `localtime_r` / `tzset` (invece di tutti zeri).
   - **`CreateFileMappingA` / `MapViewOfFile` / `UnmapViewOfFile`**: ATTENZIONE alla nota `qmem` su `standard.snp`! Verifica come `storm.orig.dll` (`0x15014C9C`) usa `CreateFileMappingA` su `standard.snp` e sugli altri file MPQ/mappa: se `standard.snp` è un vfile (`win32_snp_vfile_*`), falla fallire pulitamente o gestiscila in coerenza con `(signature)` in modo da NON rompere `standard.snp`, mentre per i file reali supporta `CreateFileMappingA` + `MapViewOfFile` (`mmap`)!
   - **`IsDBCSLeadByte`**, **`FlushFileBuffers`**, **`SetEndOfFile`**: verifica se `FlushFileBuffers`/`SetEndOfFile` possono usare `fsync`/`ftruncate` reali sui fd aperti.
4. **Compila e testa (`clang --target=i686-unknown-linux-gnu -m32 -fno-pie`)**:
   - Verifica che `win32_shim.c` compili pulito con gli stessi flag di `tools/build-shim-x86.sh` (`-DSC_ELF_REAL_GDI=1 -DSC_PORT_EMULATED_TEB=1 -include stdint.h`) in `build/qa/winproc/win32_shim.o`.
   - Scrivi e esegui `tests/x86_win32_shim_test.c` testando tutte le nuove funzioni (incluso `GetCurrentThreadId` distinto su 2 `pthread`, `FindWindowA`, `CreateFileMappingA`/`MapViewOfFile` su file reale, `GetTimeZoneInformation`, verifica `stdcall ret N` via `llvm-objdump`).
5. **Report, STATE e `qmem_store`**:
   - Documenta esattamente ogni funzione aggiunta/migliorata e la lista `SHIM(...)` per `bridge_shims.def` in `reports/REPORT-winproc.md` + `reports/STATE-winproc.md` + `qmem_store`.
