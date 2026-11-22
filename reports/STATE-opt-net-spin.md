# Stato — ottimizzazione polling SNP

- **Implementato:** gate monotono per-socket su `spiReceive` in `runtime/shim-port/elf/snp_elf.c`.
- **Configurazione:** `SC_NET_POLL_MS` default `1`; `0` ripristina il polling senza gate.
- **Verifiche:** compilazione TU i686 + x86-64 PASS; test UDP loopback PASS in entrambe le configurazioni; `git diff --check` PASS.
- **Conteggio sintetico:** test con 100 receive vuote ravvicinate: 3 `recvfrom` totali col default, 103 con gate disabilitato (100 errori `EAGAIN`). Baseline gameplay della missione: 44.560 `recvmsg` in 3 s; gameplay non ripetuto.
- **Decisioni autonome:** nessuna modifica ai callback/send/free/pacchetti/GameList; input ambiente invalido ricade sul default 1 ms.
- **File toccati:** `runtime/shim-port/elf/snp_elf.c` e questi due report. Nessun revert o modifica agli altri file sporchi.
- **Dettagli/evidenze:** [`REPORT-opt-net-spin.md`](REPORT-opt-net-spin.md).
