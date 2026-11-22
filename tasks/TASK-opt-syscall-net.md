# Missione: eliminare lo spin di `recvmsg` (EAGAIN) nel provider SNP

## Contesto misurato (frigate, 2026-11-22)
In gameplay (25 FPS) `strace -c` su 3 s: **44.560 `recvmsg`, 44.426 con EAGAIN** (~14.850/s, ~600 per frame).
Causa: `spiReceive` in `runtime/shim-port/elf/snp_elf.c` fa `recvmsg()` su socket non bloccante ad ogni
chiamata, e StarCraft la chiama centinaia di volte per frame.

## Obiettivo
Ridurre le syscall **senza cambiare la semantica**: `spiReceive` deve continuare a restituire
"nessun pacchetto" (FALSE / valore attuale) quando non c'è nulla, e consegnare il pacchetto quando c'è.
Implementare un **gate temporale monotono**: se l'ultimo tentativo è avvenuto da meno di
`SC_NET_POLL_MS` (default **1 ms**, 0 = disabilitato → comportamento attuale), restituire subito
"nessun dato" **senza syscall**. Quando il gate scade, fare il `recvmsg` vero e aggiornare il timestamp.
- usare un clock **portabile** (`clock_gettime(CLOCK_MONOTONIC)` se già usato nel file, oppure il ticks
  helper del runtime) — niente assembly, niente `__rdtsc`.
- il gate deve essere **per-socket**: due peer nella stessa istanza non devono interferire.
- nessun cambiamento al formato dei pacchetti, a `spiSend`, `spiFree`, `spiInitialize`, o alla
  gestione `SnpNetAddress`/GameList.

## Vincoli
- Modificare SOLO `runtime/shim-port/elf/snp_elf.c` (+ eventuale test in `tests/x86_snp_lan_test/`).
- C11 portabile, CPU-agnostico, nessun assembly, nessun header non portabile.
- Nessuna modifica a `host_elf.c`, `win32_shim.c`, tool, `build/starcraft-x86`.

## Verifiche richieste
1. **Test esistente**: `tests/x86_snp_lan_test` (o il binario in `build/qa/net/`) deve continuare a PASSARE
   su loopback (spiSend/spiReceive/spiFree), con `SC_NET_POLL_MS=0` e con default 1.
2. **Nuovo test**: contare le syscall o le chiamate "gated" — es. esporre una variabile di conteggio
   (`snp_poll_syscalls`, solo per test) o verificare che N chiamate ravvicinate producano ≤1 `recvmsg`
   (misurabile con `strace -c` nel test o con un contatore interno). Riporta i numeri.
3. Compilazione i686 + host x86-64 del TU: PASS.

## Deliverable
- Patch in `runtime/shim-port/elf/snp_elf.c`.
- `reports/REPORT-opt-net-spin.md` e `reports/STATE-opt-net-spin.md` con conteggi prima/dopo.
