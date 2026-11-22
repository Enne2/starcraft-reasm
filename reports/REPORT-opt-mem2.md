# Report — riduzione BSS opt-mem2

## Modifiche

- `runtime/shim-port/elf/win32_shim.c`: `g_vm_regions` non è più un array statico di `VM_REGION_CAP` (16.384) elementi. Ora è un vettore dinamico riservato a chunk da 256 record e cresciuto con `realloc` mentre `g_vm_mutex` è detenuto. La capacità massima e gli ordinamenti/ricerche restano invariati; le funzioni non conservano puntatori attraverso operazioni che possono riallocare. Fallimento di allocazione viene propagato come fallimento dello split/inizializzazione.
- `runtime/common/game_main_arm.c`: `g_sc_prof` è ora un puntatore; il buffer di 4.096 slot viene azzerato con `calloc` solo se `SC_PROF` è abilitato. Se l'allocazione fallisce, il profiler non si avvia.

## Verifiche

- C11 `-Wall -Wextra`, Clang `--target=i386-unknown-linux-gnu -m32`: compilazione di `win32_shim.c` e `game_main_arm.c` PASS. Restano warning preesistenti della shim (dichiarazioni libc/variabili e ABI); attributi stdcall soppressi nella compilazione host per non inquinare con warning ABI non supportata.
- `x86_vm_pool_test` compilato ed eseguito in `build/qa/opt-mem2/`: PASS.
- `x86_io_readahead_test` compilato ed eseguito in `build/qa/opt-mem2/`: PASS.
- `x86_win32_shim_test` compilato in `build/qa/opt-mem2/`, esecuzione FAIL al controllo preesistente della traccia RaiseException (`tests/x86_win32_shim_test.c:125`); non è stato possibile confermare il PASS richiesto. Il test è isolato dalla tabella VM, ma il motivo specifico del mismatch non è stato stabilito.
- La compilazione host x86_64 richiesta non è completa/verificata: la prova iniziale della shim è stata fermata dall'header `win32_game.h`, che limita esplicitamente la shim a ELF32 i386 o ARMv6. L'errore è riportato senza aggirare il vincolo di architettura. Non è stata verificata la dimensione post-modifica delle sezioni `.bss` tramite link completo.
- `git diff --check`: PASS. Nessun build globale eseguito.

## Decisioni autonome

Ho mantenuto il limite `VM_REGION_CAP` e scelto blocchi dinamici contigui, anziché una tabella di puntatori/chunk frammentati: i puntatori agli elementi sono usati solo sotto il mutex e non attraversano una possibile crescita. Il profiler resta completamente non allocato quando `SC_PROF` è unset. Ho lasciato intatti gli altri file già modificati/non tracciati nella working tree.
