# Report — scx86-gap-net: SNet contracts and LAN discovery

## Esito

**Esito parziale, fail-closed.** Ho riesaminato `storm.orig.dll` su copia privata e riconfermato il test UDP già presente. Non ho aggiunto discovery né implementato `spiLockGameList`/`spiSelectGame`/ExternalMessage: l’analisi disponibile non prova il layout SnpGame/GAMELIST/SNETADDR, le firme complete e il protocollo di discovery richiesti da D19. Fabbricare un record visibile alla UI o un framing di rete sarebbe una falsa compatibilità. Di conseguenza non è dimostrato né dichiarato il multiplayer LAN end-to-end.

## Decisioni autonome

- Ho scelto il comportamento conservativo: preservare i callback non provati (GameList vuota, selezione e callback external non implementati) invece di introdurre ABI o wire-format ipotetici.
- Ho mantenuto la prova limitata a UDP unicast callback-to-callback e non ho avviato istanze grafiche; l’input desktop è vietato dal preludio e il provider non offre ancora una discovery utilizzabile dalla UI.
- Non ho modificato sorgenti: il test corrente conferma il trasporto, ma non giustifica modifiche al protocollo o ai contratti.

## Fonti e analisi Storm

- Binario: `build/lift-full/storm.orig.dll`; SHA-256 `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`.
- Copia privata analizzata: `build/qa/gap-net/storm.orig.private.dll`, stesso SHA-256; image base IDA `0x15000000`.
- Disassemblato archiviato: `build/qa/gap-net/storm-snet-disasm.txt`.
- `0x1503E320`: Storm carica `SnpBind`, lo invoca stdcall con 2 argomenti; controlla puntatore tabella e `dwSize >= 0x50`, azzera tabella da 0x70 byte e copia `min(dwSize,0x70)`. A `0x1503E50A` invoca il callback a `+0x1c` con 5 argomenti.
- `0x1503AE50` (SNet create game, ordinal 138): a `0x1503AF1E–0x1503AF3A` invoca `+0x50` con quattro argomenti (due buffer da 128 byte e due lunghezze 128); il risultato influenza il flusso di creazione. Questo prova il call-site del callback in quello slot, non il layout di una lista di giochi.
- `0x1503C0A0` (ordinal 122) tratta array per-peer e framing SNet interno; `0x15039EA0` costruisce un header interno di 12 byte. Non sono prova di ABI SnpGame/GAMELIST né di SNETADDR o del protocollo di discovery.
- `0x1503DE90` prova SnpQuery a 5 argomenti e l’uso del record `caps.dat`/`PROVIDERINFO`; non chiarisce i contratti GameList richiesti.

### NetFunctions: mappa degli offset

La tabella qui sotto documenta il layout completo da `dwSize` a `+0x6c` usato dal provider. La copia Storm a `0x1503E4CE–0x1503E4EC` prova la dimensione/limite della tabella, mentre **non ho dimostrato da call-site nativi ogni nome e arità della riga**: `?` indica esattamente questo limite, non un contratto ricostruito.

| Offset | Campo/slot nell’ordine NetFunctions | Prova nel binario Storm / stato |
|---:|---|---|
| `+0x00` | `dwSize` | Provato in `0x1503E4DD–0x1503E4EC`; min `0x50`, copia max `0x70` |
| `+0x04` | `spiCompareNetAddresses` | Nome/arità call-site: ? |
| `+0x08` | `spiDestroy` | Nome/arità call-site: ? |
| `+0x0c` | `spiFree` | Nome/arità call-site: ? |
| `+0x10` | `spiFreeExternalMessage` | Nome/arità/ownership: ? |
| `+0x14` | `spiGetGameInfo` | Nome/arità call-site: ? |
| `+0x18` | `spiGetPerformanceData` | Nome/arità call-site: ? |
| `+0x1c` | `spiInitialize` | 5 argomenti stdcall, call-site `0x1503E50A` |
| `+0x20` | `spiInitializeDevice` | Nome/arità call-site: ? |
| `+0x24` | `spiLockDeviceList` | Nome/arità/layout: ? |
| `+0x28` | `spiLockGameList` | Nome/arità/layout `SnpGame/GAMELIST`: ?; non implementato |
| `+0x2c` | `spiReceive` | Nome/arità/ownership dei buffer: ? |
| `+0x30` | `spiReceiveExternalMessage` | Nome/arità/ownership: ? |
| `+0x34` | `spiSelectGame` | Argomenti/output `SNETADDR`/flags/player info: ?; fail-closed |
| `+0x38` | `spiSend` | Nome/arità/layout address: ? |
| `+0x3c` | `spiSendExternalMessage` | Nome/arità/semantica: ?; non implementato |
| `+0x40` | `spiStartAdvertisingLadderGame` | Nome/arità call-site: ? |
| `+0x44` | `spiStopAdvertisingGame` | Nome/arità call-site: ? |
| `+0x48` | `spiUnlockDeviceList` | Nome/arità call-site: ? |
| `+0x4c` | `spiUnlockGameList` | Nome/arità/ownership: ?; non implementato oltre stub preesistente |
| `+0x50` | `spiStartAdvertisingGame` | 4 argomenti stdcall, call-site `0x1503AF1E–0x1503AF3A` |
| `+0x54` | `spiReportGameResult` | Arità/contratto: ?; fail-closed |
| `+0x58` | `spiCheckDataFile` | Arità/contratto: ?; fail-closed |
| `+0x5c` | `spiLeagueCommand` | Contratto non provato/Battle.net: non implementato |
| `+0x60` | `spiLeagueSendReplayPath` | Contratto non provato/Battle.net: non implementato |
| `+0x64` | `spiLeagueGetReplayPath` | Contratto non provato/Battle.net: non implementato |
| `+0x68` | `spiLeagueLogout` | Contratto non provato/Battle.net: non implementato |
| `+0x6c` | `spiLeagueGetName` | Contratto non provato/Battle.net: non implementato |
| `+0x70` | fine tabella (27 callback) | Dimensione totale `0x70`, non uno slot callback |

La lista dei nomi segue la dichiarazione ordinata `SnpNetFunctions` presente in `runtime/shim-port/elf/snp_elf.c`; le righe con `?` non sono una certificazione indipendente del nome/ABI da parte di Storm. Non posso quindi qualificare la tabella completa come “tutta verificata su Storm”.

## Implementazione attuale e verifica

- `runtime/shim-port/elf/snp_elf.c`: nessuna modifica in questo task. Rimane bind UDP configurabile, `spiSend`/`spiReceive`/`spiFree`; GameList restituisce lista vuota e `spiSelectGame`, external message, report/check-data e League restano non implementati/negati secondo il codice preesistente.
- Copia eseguita: `build/qa/gap-net/x86_snp_lan_test`, dal gate preesistente; PASS. Output: `PASS: two UDP peers exchanged real loopback packets through spiSend/spiReceive/spiFree`.
- Il test dimostra due endpoint UDP loopback e non esercita discovery, GameList, SelectGame, ExternalMessage o una sessione StarCraft.
- `./tools/run-isolated.sh list`: tutte le istanze elencate risultano `alive=0`; non ne ho create né terminate.
- Nessun test a due istanze StarCraft eseguito. I controlli UI richiesti (“Multiplayer → Original → UDP/IPX”) necessitano input desktop; inoltre la discovery/GameList non è implementata.

## File toccati

Solo `build/qa/gap-net/` (copia privata DLL, disassemblato, copia test binary) e i due report `reports/REPORT-net2.md`, `reports/STATE-net2.md`. Nessun sorgente, file vietato, oggetto di `build/obj-x86/`, binario `build/starcraft-x86`, git o sotto-agente è stato toccato.
