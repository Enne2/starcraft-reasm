# LAN-2B — GameList e advertising reali: due istanze si vedono in lista

Data: 2026-11-24. Repo: `/home/enne2/dev/starcraft-reasm-generic`. Binario: `build/starcraft-x86` (i686).
Specifica: `reports/ANALYSIS-snp-contracts.md` (IDA su `standard.snp` base `0x1D000000` e
`storm.orig.dll` base `0x15000000`).

## Esito

**Gate raggiunto.** Due istanze dello stesso binario, una su `127.0.0.1` e una su `127.0.0.2`,
si vedono nella lista partite della schermata *Multiplayer → Original → Local Area Network (UDP)*:
l'istanza B mostra la partita creata da A (`Game Title`, `Game Type`, `Map Name`, `Map Size`,
`Game Speed` corretti), 1/2 giocatori.

Il join non è ancora possibile: mancano `spiGetGameInfo` e `spiSelectGame` (step LAN-3). Premendo Ok
la UI mostra `Unable to join game.` e il log riporta `spiGetGameInfo -> FALSE`, come atteso.

## Contratti allineati al binario originale

L'analisi IDA ha smentito due assunzioni del piano precedente (`PLAN-MULTIPLAYER.md`):

| slot | piano precedente | contratto provato | azione |
|---|---|---|---|
| `+0x40` | `spiStartAdvertisingLadderGame`, 2 argomenti | **advertising vero**, **10 argomenti** (`ret 0x28`; `storm:15037BF8`, `snp:1D009019`) | implementato: costruisce il datagramma tipo 0 e lo invia |
| `+0x50` | `spiStartAdvertisingGame`, 4 argomenti (stringhe) | callback **diversa**: 4 argomenti con due **buffer di output** da 128 byte (`storm:1503AF1E`) | fail-closed, non legge più i buffer come input |
| `+0x34` | `spiSelectGame`, arità ignota, stub `(void)` | **6 argomenti** (`ret 0x18`; `storm:1503906A`) | arità corretta (lo stub senza argomenti lasciava 24 byte sullo stack) |
| `+0x4C` | `unlockGameList` no-op | scrive il poll hint **500 ms** se riceve un indirizzo (`snp:1D009BAE`) | implementato |
| `+0x44` | rimozione locale | invia il **ritiro** (tipo 1) e libera l'annuncio (`snp:1D008DAC`) | implementato |

## Implementazione (`runtime/shim-port/elf/snp_elf.c`)

- **Due socket**: gioco su **UDP 6112** (prima era effimera: i peer hanno la porta normalizzata a
  6112 e il join non avrebbe raggiunto nessuno) e **discovery su UDP 6111**, entrambi bindati
  sull'IP di `SC_LAN_BIND` (default `127.0.0.1`), `SC_DISC_PORT`/`SC_LAN_PORT` per override.
- **Wire format**: header 20 byte (`checksum`, `len`, `type` 0/1/2, `field06`, `product`, `version`,
  `flags`) + `name\0 statstring\0 blob`; probe da 20 byte; risposta al probe = annuncio unicast.
- **Checksum**: `snp_wire_sum` (Fletcher mod 255 all'indietro, `snp:1D00F060`) e `snp_wire_seal`
  (`snp:1D008BD0`). Nota verificata: il sigillo dell'originale **non** rende nulla la somma
  sull'intero datagramma (2 casi su 168 nel test numerico), quindi la validazione qui ricalcola il
  sigillo e lo confronta col campo — cioè accetta esattamente ciò che scrive un mittente originale.
  Il controllo `test ax,ax` del ricevitore originale resta non riproducibile (documentato nel codice).
- **Nodo `SnpGame` da 0x13C byte** con gli offset provati (`+0x0C` host inline, `+0x20` tick TTL,
  `+0x28` name[128], `+0xA8` statstring[128], `+0x128` next, `+0x12C` blob, `+0x130` blob_len,
  `+0x134` product, `+0x138` version), verificati a compilazione con `__builtin_offsetof`.
- **TTL 10000 ms**, purge a ogni Lock; chiave = `sockaddr_in` con porta normalizzata a 6112;
  ownership dei nodi del provider (Lock restituisce un prestito, Unlock non libera).
- **Probe** emesso da Unlock con cadenza minima 400 ms (anche con lista vuota, come l'originale).
- **Reception**: il socket di discovery viene drenato da `snpLockGameList`, `snpUnlockGameList`,
  `snpReceive` e dal thread di poll interno; se lo shim riceve un evento non nullo il thread parte,
  altrimenti resta la via dei callback (Storm passa `NULL`, vedi `REPORT-lan-step1.md`).
- **Loopback multi-istanza**: gli annunci/probe vanno a `255.255.255.255:6111` (broadcast globale),
  `127.255.255.255:6111` (broadcast di loopback, altrimenti due istanze su `127.0.0.x` non si
  vedrebbero) e ai peer di `SC_LAN_PEERS`.

## Prove eseguite

```
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.1 SC_SNP_TRACE=1 SC_LAN_PEERS=127.0.0.2:6111 \
  ./tools/run-isolated.sh start --id lan-a --skip-intro 1 --smack 1
SC_PRESENT=cpu SC_LAN_BIND=127.0.0.2 SC_SNP_TRACE=1 SC_LAN_PEERS=127.0.0.1:6111 \
  ./tools/run-isolated.sh start --id lan-b --skip-intro 1 --smack 1
# lan-a: Multiplayer > Original > Local Area Network (UDP) > Ok > profilo > Create Game > Ok
# lan-b: idem fino alla lista partite
```

Log di `lan-b`:

```
[snp-trace] discovery rx len=0x00000052 type=0x00000000 declared=0x00000052 seal=0x0000EE11 calc=0x0000EE11
[snp-trace] game list +: lana stat=,44,12,3,1,f,,1,4ed98029,1,,lana Bottleneck 0x0000000C
[snp-trace] spiLockGameList head=0xE0500620 count=0x00000001
```

Schermata di `lan-b` (verificata a immagine, 320 px): voce `lana` in lista, pannello dettagli con
`Game Title: lana`, `Game Type: Top vs Bottom`, `Teams: 1 vs 1`, `Map Name: Bottleneck`,
`Map Size: 128x128`, `Game Speed: Normal`, contatore `1/2`.

Tentativo di join (Ok sulla voce): dialog `Unable to join game.` e `spiGetGameInfo -> FALSE` —
coerente con l'assenza degli slot dello step LAN-3.

## Cosa resta aperto (step LAN-3)

1. `spiGetGameInfo` (+0x14, 4 argomenti): copiare il nodo selezionato nel buffer `out316` del
   chiamante (shallow copy come l'originale, `ret 0x10`).
2. `spiSelectGame` (+0x34, 6 argomenti): identificare quale dei quattro contesti temporanei porta la
   voce selezionata (l'originale apre un dialogo proprio, `snp:1D009C47 call storm_208`); serve un
   decompilato più profondo dei sei argomenti prima di implementare.
3. Verifica desync: due istanze devono avanzare di turno in lockstep per ≥60 s.

## Limiti dichiarati

- Le prove usano due istanze su **loopback** dello stesso host: la scoperta passa anche dal
  broadcast di loopback, che non è ciò che accade su una LAN reale (dove basta il broadcast globale).
  Un test su due macchine fisiche non è stato eseguito.
- Il checksum è validato con la regola del mittente; il controllo del ricevitore originale non è
  riprodotto (motivo sopra). L'interoperabilità con `standard.snp` originale non è stata provata.
- Nessun test ARM in questa fase: la modifica è in codice condiviso e la build ARM è stata usata dallo
  step LAN-1; la riverifica ARM va fatta dopo lo step LAN-3.
