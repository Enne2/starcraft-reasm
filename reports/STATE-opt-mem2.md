# Stato — opt-mem2

- Implementazione g_vm_regions lazy/chunked e g_sc_prof calloc-on-enable applicata.
- Verificati: compilazione C11 i386 per entrambi i sorgenti; `x86_vm_pool_test` PASS; `x86_io_readahead_test` PASS; `git diff --check` PASS.
- Da risolvere/verificare: `x86_win32_shim_test` fallisce il controllo RaiseException alla riga 125; compilazione host x86_64 non verificata (shim ha guard architetturale ELF32 i386/ARMv6); nessun controllo `.bss` da link completo.
- Build globale non eseguito.
- Report completo: `reports/REPORT-opt-mem2.md`.
