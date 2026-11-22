# Stato — port A2+A3+A4

- **Completato:** aggiornato `runtime/shim-port/elf/win32_shim.c` con shadow tracking, pool VM da 24 MiB e read-ahead da 64 KiB su `pread64`.
- **Preservati:** fix Win32 locali, inclusi `CREATE_NEW`, `reasm_open_ci`, EnumWindows, file mapping, gettid e `win32_shell_dlg`.
- **Test copiati:** `tests/x86_vm_pool_test.c`, `tests/x86_vm_pool_test/check.sh`, `tests/x86_io_readahead_test.c`, `tests/x86_io_readahead_test/check.sh`.
- **Test esteso:** `tests/x86_io_readahead_test.c` verifica anche che pipe e stdout non entrino nella cache.
- **Test PASS:** `build/qa/port-win32/x86_vm_pool_test`; `build/qa/port-win32/x86_io_readahead_test`; `build/qa/port-win32/x86_win32_shim_test` (`-DSC_PORT_EMULATED_TEB`).
- **Non toccati:** `runtime/common/`, `host_elf.c`, `smack_*`, `tools/`, `build/starcraft-x86`.
- **Non verificato:** build completa e runtime end-to-end del gioco; build host ARM.
- **Nota ambiente:** il target non è un repository Git (`.git` assente); verifiche tramite confronto file e test.
- **Decisioni autonome:** merge conservativo per preservare le correzioni del target; cache limitata a file regolari read-only; nessuna modifica ai file comuni.

Per ripetere i test, dalla root del progetto:
```sh
gcc -m32 -O2 -pthread -ffunction-sections -fdata-sections -Iruntime/common -Iruntime/shim-port/elf -I"$HOME/dev/reasm-toolkit/runtime/elf" tests/x86_vm_pool_test.c -Wl,--gc-sections -o build/qa/port-win32/x86_vm_pool_test && build/qa/port-win32/x86_vm_pool_test
gcc -m32 -O2 -pthread -ffunction-sections -fdata-sections -Iruntime/common -Iruntime/shim-port/elf -I"$HOME/dev/reasm-toolkit/runtime/elf" tests/x86_io_readahead_test.c -Wl,--gc-sections -o build/qa/port-win32/x86_io_readahead_test && build/qa/port-win32/x86_io_readahead_test
gcc -m32 -DSC_PORT_EMULATED_TEB -O2 -pthread -ffunction-sections -fdata-sections -Iruntime/common -Iruntime/shim-port/elf -I"$HOME/dev/reasm-toolkit/runtime/elf" tests/x86_win32_shim_test.c runtime/shim-port/elf/win32_shim.c -Wl,--gc-sections -o build/qa/port-win32/x86_win32_shim_test && build/qa/port-win32/x86_win32_shim_test
```
