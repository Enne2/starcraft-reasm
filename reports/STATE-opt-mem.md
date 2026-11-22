# STATE — opt-mem

- **Fatto:** eliminata l'allocazione heap a ogni poll vuoto in `snp_elf.c`; receive stack pieno UDP IPv4, heap solo dopo ricezione valida.
- **Fatto:** `g_win` sostituito con chunk lazy stabili da 32 (cap 96); HWND integer sintetici. `.bss` oggetto i686 500.100 → 44.872 B.
- **Fatto:** `SC_SPLIT_DEBUG=1` opzionale in `tools/link-x86.sh` e `tools/link-arm.sh`; split verificato su copia, `--dump-map` invariato e simboli mantenuti.
- **Test PASS:** compilazione i686 e x86_64 di `snp_elf.c`/`win32_ui.c`; UDP loopback con assertion 0 malloc nel poll vuoto; `x86_shadow_mem_test`; `bash -n`; `git diff --check`.
- **Non completato per proprietà esclusiva:** riduzione `g_vm_regions` (in `runtime/shim-port/elf/win32_shim.c`) e `g_sc_prof` (in `runtime/common/game_main_arm.c`) richiede file non autorizzati. I nomi indicati nel task non corrispondono a `game_memory_arm.c`/`indirect_dispatch.c`.
- **Vincoli rispettati:** nessun build globale, nessuna modifica a `build/starcraft-x86`, nessun sotto-agente.
- **Report:** `reports/REPORT-opt-mem.md`.
