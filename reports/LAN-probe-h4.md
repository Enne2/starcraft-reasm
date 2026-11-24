# H4 — robustezza discovery/timing

## Esito

**Ipotesi non falsificata, criterio di successo non raggiunto.** È stato trovato e corretto un bug indipendente che rendeva non validi i test su porte custom: `disc_broadcast()` inviava ancora a 6111 anche con `SC_DISC_PORT=16041`. I tre tentativi precedenti non possono essere usati per valutare la discovery custom. Dopo il fix ho ricompilato e ripetuto il flusso una volta: le istanze bindano correttamente `16041/16042`, ma il flusso UI non ha prodotto il callback di advertising `+0x40` né un join. Il timing TTL/host resta quindi non testato; non dichiaro risolto né falsificato il problema.

## Evidenze e ipotesi

Il report precedente documenta TTL di 10.000 ms dal tick `SnpGame+0x20`, aggiornato dagli annunci; purge durante l’enumerazione. L’analisi del provider originale conferma la chiave `sockaddr_in` con porta normalizzata a 6112 e il purge a `1D008719–1D008725`. Il ramo probe risponde con l’annuncio tipo 0 conservato (`1D008780`, `1D0087A5–1D0087C1`). Il callback `+0x44` ritira l’annuncio; il call-site Storm osservato è `1503AAE8`, condizionato dal ruolo host a `1503AAD9–1503AADF` (evidenza in `reports/ANALYSIS-snp-contracts.md`).

La copia di `standard.snp` è stata aperta in IDA come `/tmp/lan-h4/build/qa/h4/standard.snp`; la decompilazione di `1D008D80` mostra il cambio al tipo 1 e il checksum (`1D008DAC–1D008DF0`), invio al callback di rete `1D008E11` e liberazione dell’annuncio `1D008E42–1D008E48`. La routine di teardown `1D009070` invia separatamente il datagramma da 8 byte (`1D0090B9–1D009115`, `1D009140–1D009168`).

## Modifiche

Solo `runtime/shim-port/elf/snp_elf.c`:

- aggiornamento del tick `+0x20` quando arriva un datagramma SNet da un indirizzo già presente, dopo normalizzazione alla porta configurata; nessuna voce nuova viene creata dal traffico SNet;
- heartbeat dell’annuncio host ogni 3 s mentre l’advertising è attivo, oltre alla risposta ai probe già implementata; frequenza inferiore al TTL provato di 10 s;
- discovery su `g_disc_port`, inizializzato da `SC_DISC_PORT` in `open_discovery_socket()`: broadcast a `255.255.255.255`, `127.255.255.255` e peer configurati usano tutti la stessa porta; questo corregge il bug che forzava i datagrammi in uscita a 6111;
- trace di `+0x44` con return address host, stato active e nome del gioco per osservare contesto e momento del ritiro.

Il TTL e il comportamento di stop non sono stati alterati: non c’è evidenza dinamica che autorizzi a sopprimere il ritiro o a disabilitare globalmente il purge. Il trace di `+0x44` è predisposto ma non è stato osservato nei tre tentativi, perché nessuno ha creato un gioco.

## Statistica dei tentativi

Ogni ciclo ha avviato A (`127.0.0.41`) e B (`127.0.0.42`), usando `SC_DISC_PORT=16041`, `SC_LAN_PORT=16042` e peer incrociati `:16041`. Audio dummy e display Xvfb privati; tutti i processi sono stati arrestati. Nessun bind è stato fatto sulle porte 6111/6112.

| Ciclo | Esito misurato | Evidenza |
|---|---|---|
| 1 | **Pre-join / non valido per il criterio**: A resta viva, B termina durante la sequenza UI; nessun advertising o join | `build/isolated/h4-a1/game.log`, `h4-b1/game.log`; screenshot numerico A mean `0.169397`, B nero `mean=0` |
| 2 | **Pre-join / non valido per il criterio**: entrambe vive al controllo, ma nessun callback `+0x40`, nessun join | `build/isolated/h4-a2/game.log`, `h4-b2/game.log`; solo `SnpBind` e socket discovery/game |
| 3 | **Pre-join / non valido per il criterio**: come ciclo 2 | `build/isolated/h4-a3/game.log`, `h4-b3/game.log`; solo `SnpBind` e socket discovery/game |

Totali pre-fix: **partita 0/3; stallo 0/3; host perso durante join 0/3; tentativi non validi 3/3**. La porta errata invalidava la discovery; inoltre i log non mostrano `+0x40`, quindi questi tentativi non dimostrano che il solo bug di porta abbia causato il blocco UI.

Retest post-fix: una coppia (`h4-fix-a`, `h4-fix-b`) con i medesimi bind e porte; `ss` conferma UDP `127.0.0.41/.42:16041` e `:16042`. Entrambi i processi erano vivi. I log non mostrano advertising/probe/join, perciò anche questo retest non è una prova di gameplay né della correzione end-to-end. Screenshot numerici: A `mean=0.169433`, B `mean=0.169460`; nessuna interpretazione visiva/OCR. `tesseract` non è installato. Tutti i processi sono stati arrestati.

## Build e deliverable

- `./tools/build.sh --arch x86 --skip-entry` dopo il fix di `g_disc_port` — **PASS** (warning preesistente `input_elf.c:54`, `host_elf_ticks` sempre truthy).
- Patch rispetto allo snapshot sorgente pre-modifica: `build/qa/h4/LAN-FIX-H4.patch`.
- Log build: `build/qa/h4/build-final.log`.

## Cosa resta aperto

1. Completare la prova end-to-end: il retest post-fix non ha raggiunto l’advertising; servono due istanze effettivamente in lobby/mappa per verificare i log `peer alive via SNet`, `announce heartbeat`, ritiro `+0x44` e permanenza ≥60 s.
2. Build e verifica ARM non eseguite: il brief ha prescritto per H4 la build x86; non è stata estesa l’analisi oltre l’ipotesi autorizzata.

## Decisioni autonome

Ho mantenuto invariato il TTL e non ho ignorato `spiStopAdvertisingGame`: senza prova del suo invocarsi durante il caricamento, sopprimerlo rischierebbe di lasciare annunci fantasma. Ho mantenuto i test sulle sole porte riservate (16041/16042), corretto la discovery affinché rispetti `SC_DISC_PORT`, e implementato il refresh tick/heartbeat. Non ho modificato il repository condiviso né usato Git; le sessioni Xvfb e i processi di gioco sono stati arrestati.
