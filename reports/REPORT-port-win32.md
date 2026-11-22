# Port A2+A3+A4 in `win32_shim.c`

## Esito
Portate in `runtime/shim-port/elf/win32_shim.c` le ottimizzazioni A2, A3 e A4 dal repository `starcraft-reasm-smacker` (`daaf929`, `4abab68`, `af57767`). `runtime/common/` è rimasto intatto. Il target non contiene una directory `.git`, quindi non è stato possibile produrre un diff Git del working tree.

## Modifiche
- **A2 — shadow page table:** incluse le API di `game_memory_arm.h`; `VirtualAlloc`, `VirtualFree` (release/decommit), `VirtualProtect` ed eviction aggiornano i permessi shadow con le firme correnti (`arm_shadow_set_range(uint32_t,uint32_t,uint8_t)`).
- **A3 — pool VM:** pool di allocazioni `MEM_RESERVE|MEM_COMMIT`, RW, `MEM_RELEASE`, con riuso, azzeramento alla riallocazione e tetto totale di 24 MiB; eviction ripristina `PROT_NONE` e rimuove la shadow range.
- **A4 — read-ahead:** buffer per handle da 64 KiB alimentato da `pread64`; attivo solo per `CreateFileA` read-only `OPEN_EXISTING` e file regolari (fstat + controllo `S_IFREG`, seekability). Pipe e fd standard esclusi; i vfile SNP bypassano la cache. `WriteFile`/`SetEndOfFile` invalidano; `SetFilePointer` aggiorna la posizione logica; `CloseHandle` libera buffer e handle.
- Il merge a tre vie ha mantenuto i fix Win32 preesistenti: `CREATE_NEW` e foglia mancante in `reasm_open_ci`, shell dialog, EnumWindows via `arm_guest_calln`, fail-closed SEH, `gettid`, timezone, mapping file, `fsync`, `ftruncate`, DBCS e API finestra.

## Verifiche
Eseguite in `build/qa/port-win32/` (gcc `-m32`, `-pthread`, section GC):
- `x86_vm_pool_test`: **PASS**; pool 10.000 cicli circa 0,052 s; eviction oltre 24 MiB verificata dal test.
- `x86_io_readahead_test`: **PASS**; 576 letture casuali cold = 5 `pread64` invece di 1.152 `lseek+read`; test aggiuntivo conferma esclusione pipe/stdout.
- `x86_win32_shim_test`: **PASS** con `-DSC_PORT_EMULATED_TEB`.

Le compilazioni mostrano warning preesistenti di cast cdecl incompatibile per tre wrapper; nessun warning ha impedito i test.

## Limiti
Verifica eseguita sui test standalone i386; non eseguita una build completa/runtime del gioco né una prova host ARM. Il controllo `fstat` legge `st_mode` all'offset ABI Linux 32-bit (16), da verificare se si introduce un ABI host diverso.

## Decisioni autonome
Ho conservato il codice locale con merge a tre vie invece di sostituire l'intero shim con la copia di origine, perché il target contiene fix Win32 assenti o diversi nello snapshot sorgente. Ho limitato la cache a file regolari e lasciato invariati `runtime/common/` e i file esclusi dal task.
