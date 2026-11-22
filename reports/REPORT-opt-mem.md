# Report — ottimizzazione memoria e churn

## Risultato

- `runtime/shim-port/elf/snp_elf.c`: `receive_packet` riceve ora prima in un buffer stack da 65.507 byte (massimo payload UDP/IPv4); solo dopo un datagramma valido alloca il blocco provider-owned e ne copia i dati. Il poll vuoto/non riuscito non chiama `malloc`; il limite UDP resta invariato.
- `runtime/shim-port/elf/win32_ui.c`: tabella finestre ora in chunk lazy da 32 `UiWin`, con 96 slot totali. Gli indirizzi dei record restano stabili anche durante callback annidate. Gli HWND sono ID interi sintetici, non puntatori agli slot; ricerca/ordine dei primi slot liberi e capacità restano invariati. `.bss` dell'oggetto i686: da 500.100 B (oggetto preesistente) a 44.872 B (nuovo), riduzione 455.228 B; `g_win` originale era 460.032 B.
- `tools/link-x86.sh` e `tools/link-arm.sh`: aggiunto `SC_SPLIT_DEBUG=1` opzionale. Esporta `$OUT.debug`, rimuove solo DWARF dal binario e aggiunge `.gnu_debuglink`; `.symtab`/`.strtab` sono mantenute. Non è stato lanciato il build globale.

## Verifiche

- Compilazione con Clang C11 `-Wall -Wextra`: `snp_elf.c` e `win32_ui.c` sia `i686-unknown-linux-gnu -m32` sia host x86_64. PASS; sul codice x86_64 restano warning ABI/puntatori preesistenti di `win32_ui.c`.
- Test UDP ricompilato in `build/qa/opt-mem/`: due peer loopback si scambiano i datagrammi (PASS); il test strumentato controlla anche che un `spiReceive` senza pacchetto non incrementi il contatore malloc (PASS).
- `tests/x86_shadow_mem_test.c` ricompilato in `build/qa/opt-mem/x86_shadow_mem_test`: PASS.
- `bash -n tools/link-x86.sh tools/link-arm.sh` e `git diff --check`: PASS.
- Su una copia in `build/qa/opt-mem/` di `build/starcraft-x86`, split debug: `dispatch-ir.py --dump-map` identico prima/dopo (102.392 voci, ordinate, 0 collisioni). `.debug_*` assenti dal binario split, `.symtab` e `.strtab` presenti; debug companion 3.623.712 B, binario ridotto da 17.196.196 a 16.473.836 B. L'originale non è stato modificato.

## Limiti e file non modificati

Le tabelle indicate nel task non sono nei file associati: `g_vm_regions` è in `runtime/shim-port/elf/win32_shim.c` (fuori proprietà, vietato modificarlo); `g_sc_prof` è in `runtime/common/game_main_arm.c` (fuori proprietà). `runtime/common/indirect_dispatch.c` non contiene `g_sc_prof`; `runtime/common/game_memory_arm.c` non contiene `g_vm_regions` (ha invece una shadow map da 1 MiB e un registro distinto di sole 32 regioni). Per rispettare il confine esclusivo richiesto, nessuno di questi file è stato toccato e quelle due riduzioni BSS restano aperte. Nessun avvio del gioco o build globale è stato eseguito.

## Decisioni autonome

Ho conservato il confine dei sei file autorizzati invece di intervenire sui file effettivi delle tabelle non corrispondenti. Per UDP ho scelto il buffer stack alla massima dimensione IPv4 consentita, non 2.048 byte, così da non troncare datagrammi validi né ridurre la capacità. Lo split è opt-in per non cambiare il comportamento dei link predefiniti.
