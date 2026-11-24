# LAN-3 — `spiGetGameInfo` / `spiSelectGame`: join fino alla lobby, lockstep NON ancora avviato

Data: 2026-11-24. Repo: `/home/enne2/dev/starcraft-reasm-generic`, binario `build/starcraft-x86`.
Dipende da `reports/REPORT-lan-step2b.md` (GameList e advertising funzionanti).

## Esito onesto

**Raggiunto**: il client in lista partite seleziona la partita dell'host, il join viene accettato,
l'host mostra `lanb has joined the game.` e **entrambe le istanze caricano la mappa** (HUD, unità,
overlay pre-partita). `spiGetGameInfo` e `spiSelectGame` rispondono correttamente e i pacchetti SNet
viaggiano nei due sensi su UDP 6112.

**NON raggiunto**: la partita non parte. Il countdown dell'overlay resta a `0:00` con
`Waiting for players` su entrambe le istanze: il lockstep SNet non scambia il primo turno. Quindi
**non** è verificata una partita melee ≥60 s e il criterio di accettazione dello step 3 è aperto.

In una seconda esecuzione, con la stessa sequenza, il join è fallito con `The game host has left.`
(il client non ha visto l'host nel momento del join): il percorso è sensibile al timing.

## Implementato

| slot | contratto | cosa fa ora |
|---|---|---|
| `+0x14` `spiGetGameInfo` | 4 argomenti, `ret 0x10` (`storm:1503CC83`, `snp:1D008420`) | cerca il nodo per nome (case-insensitive, `snp:1D00847D`) o per id, azzera e copia i `0x13C` byte del record nel buffer del chiamante (shallow, come `snp:1D00849D mov ecx,4Fh`), nessuna allocazione |
| `+0x34` `spiSelectGame` | 6 argomenti, `ret 0x18` (`storm:15039044-1503906A`) | registra in trace i sei puntatori e cerca il nome di una partita in lista dentro i quattro contesti passati da Storm (`var_60`, `var_10`, `var_BC`, `var_24`, dimensioni dedotte dal call-site); TRUE solo se trova la voce. Nessuna struct inventata, nessuna ownership trasferita |
| `+0x04` `spiCompareNetAddresses` | semantica non provata da un call-site | prima restituiva sempre TRUE; ora restituisce `differences == 0`. Correzione logica, **non ancora esercitata** da nessun call-site nei flussi osservati (0 chiamate nei log) |
| `bridge_shims.def` / `shim_rename.txt` / `bridge_dynamic.def` | — | allineati al nuovo nome/arità (`snp_select_game`, 6 argomenti): senza questo la build falliva con `DYNAMIC senza prototipo` e restava in uso la tabella vecchia |

## Evidenze del join riuscito

Host (`lan-a`, `127.0.0.1`), log e schermo:

```
[snp-trace] send len=0x00000016 a=0x0200007F:0x000017E0     # verso 127.0.0.2:6112
[snp-trace] recv len=0x00000013 da=0x0200007F:0x000017E0
```
Schermata host: lobby con `Home Team: lana`, `Away Team: lanb`, messaggio `lanb has joined the game.`

Client (`lan-b`, `127.0.0.2`):

```
[snp-trace] spiGetGameInfo hit: lana id=0x00000001
[snp-trace] send len=0x00000010 a=0x0100007F:0x000017E0     # verso 127.0.0.1:6112
```
Schermata client: lobby con entrambi i team, poi mappa caricata (`Bottleneck`) con HUD e unità.

Entrambi i processi hanno i due socket bindati correttamente:
`127.0.0.1:6111/6112` e `127.0.0.2:6111/6112` (`ss -lunp`), 40+ datagrammi ricevuti per lato.

## Dove si blocca (per chi riprende)

Il transport è provato nei due sensi, quindi il blocco è al livello SNet/Storm:

1. `Waiting for players` con clock `0:00`: nessuno dei due lati considera completo lo scambio del
   turno 0. Da verificare nel decompilato quale pacchetto SNet chiude la sincronizzazione iniziale e
   se Storm lo attende su `spiReceive` o su un evento interno.
2. `spiReceiveExternalMessage` viene chiamata in raffica e risponde sempre `FALSE`
   (contratto non provato). Se Storm usa quel canale per il "ready" dei peer, il rifiuto spiega lo
   stallo: **prima cosa da indagare** (call-site di `+0x30` in Storm, `snp:1D0099??`).
3. `spiGetPerformanceData` resta `FALSE`: nessun campione di latenza, quindi `turns_per_second` e
   `turns_in_transit` restano ai default delle CAPS.
4. Il percorso è sensibile al timing: una delle due esecuzioni ha perso l'host durante il join
   (`The game host has left.`). Da capire se è un problema di TTL (10 s) o di probe cadenzati.

## Riproduzione

```sh
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.1 SC_SNP_TRACE=1 SC_LAN_PEERS=127.0.0.2:6111 \
  ./tools/run-isolated.sh start --id lan-a --skip-intro 1 --smack 1
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.2 SC_SNP_TRACE=1 SC_LAN_PEERS=127.0.0.1:6111 \
  ./tools/run-isolated.sh start --id lan-b --skip-intro 1 --smack 1
# entrambe: Multiplayer(140,200) > Original(250,210) > LAN(150,102) > Ok(518,391) > profilo(row 120,96 + Enter)
# host:     Create Game(125,342) > Ok(518,391)   -> advertiser
# client:   seleziona la voce in lista(clic) > Ok(518,391)  -> join, lobby, caricamento mappa
# host:     Ok(518,391) -> avvio partita
```
Nota sull'input: i clic vanno inviati come `down`/`up` separati da ~250 ms
(`tools/scctl move/down/up`); con `click` a latenza zero la UI del gioco non li registra.

## File toccati

`runtime/shim-port/elf/snp_elf.c`, `runtime/shim-port/bridge_shims.def`,
`runtime/shim-port/shim_rename.txt`, `runtime/common/bridge_dynamic.def`, questo report.
Nessuna modifica a `host_elf.c` (file di un'altra sessione attiva) né a codice altrui.
