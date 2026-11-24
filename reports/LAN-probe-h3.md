# H3 — Forense SNet: risultato e limite della prova

## Esito

**La diagnosi dei primi tentativi era invalida:** con `SC_DISC_PORT=16031`, `open_discovery_socket()` bindava su 16031, ma `disc_broadcast()` continuava a inviare broadcast e peer a 6111 (`SNP_DISC_PORT_DEFAULT`). Le due istanze non potevano quindi completare advertising/discovery/join. Questo è un difetto di configurazione del provider e non evidenza a favore dell’ipotesi sul lockstep. È stato corretto nella copia privata.

Dopo la correzione, la build e una nuova esecuzione isolata hanno verificato il percorso discovery → lista → selezione della partita e traffico SNet bidirezionale. **Non è stato provato che entrambe le istanze arrivino alla mappa e restino in partita per 60 s**, né che sia stato identificato il messaggio applicativo esatto che avvia il turno 0. Non attribuisco quindi al provider una correzione del lockstep.

## Correzione e build

In `runtime/shim-port/elf/snp_elf.c`:

- aggiunto `g_disc_port`, valorizzato dal `port` effettivamente bindato in `open_discovery_socket()`;
- broadcast globale, broadcast loopback e peer discovery usano ora `g_disc_port` al posto di 6111 fisso;
- aggiunta la traccia `[snp-pkt]` su `snp_send`/`snp_receive`: tempo monotono, direzione, lunghezza, endpoint e primi 32 byte per datagrammi fino a `0x40`.

Comando richiesto `./tools/build.sh --arch x86 --skip-entry`: **PASS**, binario prodotto in `build/starcraft-x86`. Resta un warning preesistente in `input_elf.c:54` sulla condizione `host_elf_ticks`.

Patch: `build/qa/h3/LAN-FIX-H3.patch`.

## Prova valida dopo il fix

Due istanze Xvfb/audio dummy:

| Istanza | Bind | Discovery/game | Peer discovery |
|---|---|---|---|
| A | `127.0.0.31` | `16031/16032` | `127.0.0.32:16031` |
| B | `127.0.0.32` | `16031/16032` | `127.0.0.31:16031` |

Conferme nei log:

- A annuncia `p16test` (datagramma discovery `len=0x58`); B riceve l’annuncio con checksum ricalcolato uguale al sigillo, aggiunge `p16test` alla GameList e registra `spiGetGameInfo hit`.
- I probe inviati da B raggiungono la porta `16031`: `discovery sendto ... dst=0x1F00007F:0x00003E9F` (127.0.0.31:16031). A riceve probe dello stesso tipo. Questo verifica direttamente la correzione delle porte non standard.
- Sui socket di gioco gli endpoint sono `127.0.0.31:16032` e `127.0.0.32:16032`; i log contengono pacchetti SNet in entrambe le direzioni.
- Le schermate sono state acquisite solo in file e misurate con `tools/shot-stat.sh`, senza immagini nel contesto. I frame hanno continuato a essere presentati, ma le sole misure non provano testualmente HUD, unità o overlay `Waiting for players`.

Le tracce contengono 1.575 record A (intervallo monotono 61.252 s) e 750 record B (60.278 s), tutti con `len <= 0x40`. I file `trace-*-raw.log` conservano l’ordine e ogni record; `trace-A.log` e `trace-B.log` aggregano i datagrammi identici.

### Differenza da approfondire, non conclusione di root cause

Dai callback provider osservati:

- A: 435 TX da 12 byte del gruppo heartbeat SNet; solo 8 RX del corrispondente gruppo da B. A riceve invece 405 TX da 12 byte emessi da B.
- Datagrammi applicativi da 13 byte con payload `05`: A TX 560 / RX 160; B TX 160 / RX 163.
- Nei campioni corrispondenti TX/RX i byte coincidono; non c’è un tipo applicativo che, dai soli record acquisiti, risulti corrotto dal provider. Le sequenze heartbeat si stabilizzano a `0xFB` lato A e `0xF9` lato B.

Questa asimmetria **non dimostra perdita UDP**: la traccia è nel callback `spiReceive`, dunque non distingue tra datagramma non accodato/ricevuto dal socket e Storm che non richiama il callback. È però un indizio più forte di una possibile lacuna nel pump/wakeup di ricezione che di byte alterati in `spiSend`.

## Correlazione IDA: Storm/SNet, non un messaggio identificato con certezza

Database IDA privato della copia SHA-256 `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`.

- `storm:0x15039EA0` costruisce il framing SNet con header di 12 byte e payload; assegna lunghezza, contatori per-peer, byte di tipo/peer e checksum. Questo header interno non è il formato discovery UDP.
- `storm:0x1503A140` produce anche frame di controllo di 12 byte senza payload (`+0x0B = 2`) in funzione delle code/contatori per-peer; quindi le ripetizioni osservate non sono, da sole, prova di un messaggio applicativo mancante.
- `storm:0x1503D080` attende un evento con `WaitForSingleObject(..., INFINITE)` a `0x1503D0B4`, poi richiama `sub_15039370` e `sub_1503B190` (`0x1503D0D1/0x1503D0D6`).
- `storm:0x1503B190` acquisisce i datagrammi attraverso il callback di ricezione della tabella a `0x1503B1B2`; controlla lunghezza minima 12, lunghezza dichiarata e checksum (`0x1503B28E–0x1503B2C5`) e aggiorna code/contatori per-peer. La logica di ricezione/ritrasmissione continua in `0x1503B3DD–0x1503B5CD`.
- `storm:0x15039370` interroga separatamente il callback external-receive. Il provider lo restituisce sempre FALSE; il suo contratto non è stato provato e non lo equiparo alla `spiReceive` che trasporta i pacchetti SNet della traccia.
- In una precedente analisi IDA il percorso `storm_117` passa NULL come quinto argomento a `spiInitialize` (`0x1503E73C`), mentre l’evento interno atteso dal worker è distinto. La correlazione runtime di questo evento non è stata fatta in H3.

**Verdetto delimitato:** i tentativi senza join erano falsati dal port discovery fisso; dopo il fix, il trasporto e il join fino a `spiGetGameInfo` funzionano. La traccia non consente di nominare con certezza il pacchetto di avvio del turno 0. Il punto puntuale per una ripresa sarebbe verificare perché `sub_1503B190`/worker (`0x1503D080`) non consegna al peer B un volume di heartbeat comparabile a quello trasmesso, e quale condizione/counter per-peer resta in attesa. Nessun ulteriore esperimento è stato eseguito.

## Decisioni autonome

- Ho trattato i test precedenti alla correzione di `SC_DISC_PORT` come non validi per advertising/join, non come falsificazione del protocollo SNet.
- Ho applicato solo il fix per usare la porta discovery configurata e la strumentazione richiesta; non ho inventato una semantica o una correzione per il lockstep.
- Ho fermato le due istanze isolate al termine della raccolta. Nessun Git e nessuna operazione sul repository condiviso sono stati usati.
