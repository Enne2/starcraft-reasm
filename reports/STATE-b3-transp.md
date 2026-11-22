# Stato b3-transp — diagnosi parziale

- Corretto il link privato: usare i path assoluti di `StarCraft.entry.o`/`storm.entry.o`, altrimenti i selector assoluti in `guest_data_link.ld` lasciano vuoti `.guest_exe_code` e `.guest_storm_code` (causa del crash precedente). Build valida, ELF32 i386 226,494,352 B.
- Avvio isolato riuscito; Select Game Type e Registry raggiunti. Screenshot letti: `build/qa/b3-transp/screenshots/{gametype-before,registry-before}.png`; entrambi mostrano pannello nero.
- Strumentazione: nessun `s_Blt`/`s_BltFast`/GDI FillRect/BitBlt; Storm #313/#334 è il percorso osservato.
- Call candidata Registry: caller originale `0x416F1F` (`sub_416ED0`, call Storm #334 a `0x416F1A`), descriptor width `0x16C`, rows `0x160`, flags `0x40`, `dst=0x205A0010`, `src=0x20520010`, LUT/base `0x20AE0010`. Ulteriore percorso da `sub_416F30` @ `0x417124`.
- Non ancora provato quale chiamata disegna il fill nero; nessun bytecode SCode selettivo acquisito, nessun fix/screenshot fixed. `fix.patch` vuoto.
- Runner `b3-transp` fermato; reports sotto 70 righe.

## Prossimo passo
Dump SCode one-shot pre-patch filtrato su `0x16C×0x160` (kernel `size1=0x4A`, `size2=0x2BC5`), mappare descriptor/caller all'originale, confrontare byte/opcode con Storm originale e solo allora correggere la copia privata e verificare screenshot.
