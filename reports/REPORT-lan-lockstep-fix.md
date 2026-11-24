# Lockstep LAN: causa trovata e corretta (perdita di datagrammi nel percorso di ricezione)

Data: 2026-11-24. Repo `starcraft-reasm-generic`. Binario `build/starcraft-x86` (i686), sandbox Xvfb,
audio dummy, due istanze sullo stesso host.

## Sintomo

Dopo il join e il caricamento della mappa, entrambe le istanze mostravano l'overlay
`Waiting for players` con il clock fermo a `0:00`: il lockstep non scambiava il turno 0.

## Causa

Il percorso di ricezione del provider perdeva datagrammi con il traffico SNet a raffica:

1. **Throttle che nascondeva dati già in coda** — `receive_packet()` applicava `SC_NET_POLL_MS`
   (default 1 ms) e, se il tempo era sotto soglia, restituiva 0 **senza guardare il socket**. Storm
   chiama `spiReceive` in raffica: ogni chiamata "a vuoto" costava fino a 1 ms di ritardo anche con
   pacchetti in coda.
2. **Buffer di ricezione del kernel al default** (~208 KB): con decine di pacchetti da 12-13 byte per
   frame su entrambe le direzioni, e il ritardo introdotto dal punto 1, il buffer si riempiva e il
   kernel scartava. Le tracce mostravano asimmetria netta (es. un lato inviava 435 frame di controllo
   e l'altro ne riceveva 8) e `ss -u` con coda non vuota.

Con il buffer che si svuota e i pacchetti scartati, la barriera iniziale di SNet non si chiude mai e
il gioco resta in attesa.

## Correzione (`runtime/shim-port/elf/snp_elf.c`)

- il throttle **salta solo se non c'è nulla da leggere**: prima una `recvfrom(..., MSG_PEEK)`; se
  trova un datagramma la ricezione procede subito;
- `SO_RCVBUF` portato a **4 MB** sul socket di gioco (era il default del kernel).

Contorno (stessa sessione, verificato separatamente):

- **bug delle porte**: `disc_broadcast()` usava la costante `SNP_DISC_PORT_DEFAULT` (6111) per
  broadcast globale, broadcast di loopback e peer, quindi con `SC_DISC_PORT` non standard annunci e
  probe finivano su 6111 e le due istanze non si vedevano. Ora usa `g_disc_port`; anche la chiave dei
  nodi usa `g_game_port`. Dopo questa correzione, con porte isolate 16051/16052, la partita appare in
  lista e il join arriva a `spiGetGameInfo` (prima: lista vuota).

## Verifica

Due esecuzioni indipendenti, stesso flusso (host crea, client entra, host avvia), istanze su
`127.0.0.51` / `127.0.0.52`, discovery `16051`, gioco `16052`.

**Run 1** (con `SC_SNP_TRACE=1`):

- entrambe in partita: HUD attivo, unità visibili, **nessun** overlay `Waiting for players`;
- traffico simmetrico e costante: **+227 pacchetti in 15 s su entrambi i lati** (≈15/s), cioè lo
  scambio dei turni avviene;
- osservazione continua per 90 s senza stalli, coda dei socket UDP a 0 (`ss -u`).

**Run 2** (senza trace, quindi senza il rumore dei log):

- entrambe in partita per **oltre 60 s** con rendering stabile
  (`A` 0,1938 → 0,1938, `B` 0,1675 → 0,1675, oscillazioni < 0,0005);
- un ordine impartito all'istanza `A` (selezione + click destro) è stato accettato:
  frame di `A` cambiato (0,1938 → 0,2018) e `B` vivo e stabile;
- nessun crash, nessuna disconnessione.

**Prima della correzione** lo stesso flusso si fermava sistematicamente a `0:00`
(`reports/REPORT-lan-step3.md`), con i pacchetti scambiati ma il clock immobile.

## Ipotesi esplorate in parallelo e scartate (evidenze nei file allegati)

| ipotesi | esito | evidenza |
|---|---|---|
| H1 `spiReceiveExternalMessage` (+0x30) causa lo stallo | **falsificata** nella forma stretta: restituire FALSE con uscite nulle è il caso "nessun messaggio"; il canale è coinvolto nella barriera ma non è la causa | `reports/LAN-probe-h1.md` |
| H2 `spiGetPerformanceData` (+0x18) | **falsificata**: Storm non usa nemmeno il valore di ritorno del callback (`0x15037372`); `+0x58` è nullo nella tabella originale | `reports/LAN-probe-h2.md` |
| H3 forense del protocollo SNet | ha **trovato lo stesso bug delle porte** in modo indipendente e ha documentato l'asimmetria dei pacchetti che ha portato alla diagnosi | `reports/LAN-probe-h3.md` |
| H4 TTL/timing, refresh dei nodi | ipotesi non confermata come causa; il patch proposto (refresh del tick del nodo sul traffico SNet) è ragionevole ma **non integrato** perché non verificato | `reports/LAN-probe-h4.md` |

## Riproduzione

```sh
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.51 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
  SC_LAN_PEERS=127.0.0.52:16051 ./tools/run-isolated.sh start --id a --skip-intro 1 --smack 1
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.52 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
  SC_LAN_PEERS=127.0.0.51:16051 ./tools/run-isolated.sh start --id b --skip-intro 1 --smack 1
# poi: Multiplayer(140,200) > Original(250,210) > LAN(150,102) > Ok(518,391) > profilo
#      host: Create Game(125,342) > Ok(518,391);  client: riga lista(150,96) > Ok(518,391)
#      host: Ok(518,391) per avviare
```
`tools/qa/lan-e2e.sh` e `tools/qa/lan-enter.py` automatizzano la sequenza; i clic devono essere
`move`/`down`/`up` con pausa ~250 ms (con `click` a latenza zero la UI non li registra).

## Limiti dichiarati

- Due istanze sullo stesso host, loopback: non è una prova su due macchine fisiche.
- Non ho misurato il determinismo dei turni (checksum di stato): la prova è "scambio simmetrico e
  stabile + input accettato", non uguaglianza di stato simulato.
- Il patch di H4 (refresh dei nodi) resta fuori: non verificato, e la correzione sopra basta.
- La correzione è nel provider condiviso: la build ARM va riverificata (non eseguita in questa fase).
