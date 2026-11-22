# Report — gate temporale per `spiReceive`

## Esito

Implementato in `runtime/shim-port/elf/snp_elf.c` un gate per-socket basato su `clock_gettime(CLOCK_MONOTONIC)` (id POSIX `1` già usato dal runtime). `SC_NET_POLL_MS` viene letto all’apertura del socket: default `1`; `0` disabilita il gate. Un tentativo effettivo aggiorna il timestamp anche quando `recvfrom` non trova dati; le chiamate prima della scadenza restituiscono `FALSE` e azzerano gli output, senza syscall di ricezione. Il percorso esistente alloca il pacchetto solo dopo aver ricevuto un datagramma.

## Decisioni autonome

- Ho mantenuto invariati `spiSend`, `spiFree`, il callback `spiInitialize`, il formato dei pacchetti e la gestione degli indirizzi/GameList. La configurazione e lo stato del gate vengono inizializzati nel helper che crea il socket.
- Ho trattato valori `SC_NET_POLL_MS` vuoti o non numerici/overflow come configurazione non valida e applicato il default conservativo di 1 ms; `0` è il solo valore che disabilita il gate.
- Ho lasciato intatti gli altri cambiamenti già presenti nel working tree, compresa la modifica preesistente in `snp_elf.c` che riceve su buffer locale prima di allocare.

## Verifiche

- Compilazione TU i686: **PASS**, clang `--target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g` con gli include/avvisi del runtime.
- Compilazione TU x86-64 host: **PASS**, clang `-O1 -g`.
- Test loopback `tests/x86_snp_lan_test.c`, compilato in un eseguibile temporaneo collegato al TU aggiornato: **PASS** sia con default (variabile non impostata) sia con `SC_NET_POLL_MS=0`. Copre invio peer→provider, `spiReceive`, risposta `spiSend`, ricezione peer e `spiFree`.
- Per misurare le chiamate ravvicinate ho usato una copia temporanea del test (non modificata nel repository) che esegue 100 `spiReceive` senza pacchetto subito dopo il ricevimento valido. `strace -f -c -e trace=recvfrom`:

| Configurazione | `recvfrom` totali | Errori (poll vuoti) |
|---|---:|---:|
| Gate default 1 ms | 3 | 0 |
| `SC_NET_POLL_MS=0` | 103 | 100 |

  Le 3 chiamate nel caso default comprendono il self-test UDP e la ricezione sul socket peer; nessuna delle 100 chiamate vuote ravvicinate ha invocato `recvfrom`. Con gate disabilitato, tutte le 100 chiamate vuote hanno generato syscall. Il binario esistente `build/qa/net/x86_snp_lan_test` è stato eseguito prima delle modifiche: **PASS**, 3 `recvfrom` nel suo percorso originale.
- Il riferimento di gameplay fornito nella missione è 44.560 syscall `recvmsg` / 3 s (44.426 `EAGAIN`, ~14.850/s); non ho ripetuto una sessione gameplay. Il test loopback usa i686, dove `strace` riporta l’interfaccia `recvfrom`.
- `git diff --check`: **PASS**.

## File

- Modificato: `runtime/shim-port/elf/snp_elf.c`.
- Creati: `reports/REPORT-opt-net-spin.md`, `reports/STATE-opt-net-spin.md`.
- Non sono stati modificati sorgenti test, build script, altri file runtime, né i file contemporaneamente modificati da altri agenti.

## Limiti

La misura controllata è un test sintetico a 100 polling ravvicinati, non un benchmark completo in gameplay. Il gate limita la frequenza di polling per socket e può ritardare la consegna di un pacchetto arrivato subito dopo un tentativo fino alla scadenza configurata (1 ms di default), come richiesto dalla semantica del gate.
