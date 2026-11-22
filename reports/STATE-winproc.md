# STATE — winproc

- `win32_shim.c`: finestre, fail-closed SEH, Linux TID per-thread, timezone, mapping file-backed, `fsync`/`ftruncate`; SNP vfile mapping rifiutato.
- Test: `tests/x86_win32_shim_test.c`; PASS con clang i686, pthread, mmap e ABI stdcall (`build/qa/winproc/`).
- Call-site verificato: storm `0x15014C9C` CreateFileMappingA (6 argomenti); `0x15014CB6` MapViewOfFile (5). Tutti gli altri import verificati in import table/LLASM, ma non associati a indirizzi call-site individuali.
- Limiti: SEH reale non implementato (exit 70); EnumWindows callback i386 ritorna FALSE, ARM usa `arm_guest_calln`; DaylightBias zero; IDA open fallito.
- Manifest SHIM correlati già esistenti: `CreateFileMappingA`, `FlushFileBuffers`, `GetCurrentThreadId`, `GetTimeZoneInformation`, `IsDBCSLeadByte`, `MapViewOfFile`, `SetEndOfFile`, `UnmapViewOfFile`. Nessun file manifest modificato.
- Decisioni autonome: evitare callback guest i386 senza trampoline; fallire chiuso su SEH; preservare standard.snp rifiutando mapping su vfile.
