# STATE saveload
- Modulo preliminare creato: `runtime/shim-port/elf/win32_shell_dlg.c/.h`.
- Test TU: `tests/x86_saveload_test.c`; artefatti i686 in `build/qa/saveload/`.
- Shim e test compilati i686; `llvm-objdump` verifica pop stdcall: 12, 8, 4, 4, 24, 12 byte.
- Il test non è stato linkato/eseguito. Nessuna finestra/audio avviati.
- IDA rifiuta l'apertura del PE; report basato su import table, objdump e llasm.
- Call-site e procedura d'integrazione: `reports/REPORT-saveload.md`.
- Non modificati manifest, dispatcher, `win32_shim.c`, build/obj-x86.
