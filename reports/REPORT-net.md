# Report — scx86-net / SNP UDP

## Esito

È stato implementato e verificato il trasporto UDP unicast in `snp_elf.c`: bind configurabile con `SC_LAN_BIND` e `SC_LAN_PORT`, socket non bloccante, `spiSend`, `spiReceive` e rilascio dell’allocazione ricevuta tramite `spiFree`. La prova i686 con due endpoint loopback ha scambiato datagrammi in entrambe le direzioni.

**Non è multiplayer StarCraft LAN completo.** In base a D19 non ho inventato ABI/layout non dimostrati: l’elenco partite resta vuoto, `spiSelectGame` resta fail-closed, e i callback external/league non sono stati abilitati. Il test dimostra il trasporto callback-to-callback, non una partita end-to-end in StarCraft né compatibilità di rete con `standard.snp` originale.

## Decisioni autonome

- Ho limitato l’implementazione ai callback con un contratto sufficiente per una prova controllata; non ho fabbricato `SnpGame`/`GAMELIST`, framing di discovery o semantica di selezione.
- `SC_LAN_BIND` accetta un IPv4 numerico; default `127.0.0.1`. `SC_LAN_PORT` accetta una porta decimale, incluso `0` per porta effimera; default `0`. Per due processi su una macchina usare porta effimera e scoprire/trasferire il port out-of-band, oppure configurare porte distinte.
- I callback esterni mantengono il fail-closed dove i parametri non sono confermati. Non ho dichiarato alcuna compatibilità PvPGN/Battle.net.

## Evidenze IDA / binari

Database IDA aperto per `build/lift-full/storm.orig.dll`, image base `0x15000000`, SHA-256 `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`.

- `0x1503E320` (SNet provider bind/init): decompilato. Storm invoca `SnpBind` con `stdcall` a 2 argomenti e controlla il puntatore tabella/`dwSize` (`>=0x50`, copia fino a `0x70`). Il callback `spiInitialize` è chiamato indirettamente allo slot `+0x1c` con 5 argomenti (`0x1503E50A`).
- `0x1503AE50` (ordinal 138, create game): 12 parametri all’ingresso Storm; il callback table viene letto, ad esempio, a `+80` con quattro argomenti per un callback non identificato qui. Non equivale a una prova del layout di `SnpGame`.
- `0x1503C0A0` (ordinal 122) tratta array per-peer con elementi da 4 byte e produce puntatori/lunghezze di messaggi Storm. `0x15039EA0` costruisce il framing interno di messaggio Storm con header di 12 byte. Sono strutture del livello SNet, non una prova sufficiente del layout passato a `spiLockGameList` né dell’indirizzo provider `SNETADDR`.
- Le analisi richieste per tutte le API non sono concluse: i nomi/contratti di `spiSelectGame`, `spiReportGameResult`, `spiCheckDataFile`, `spiLeague*`, external-message e GameList non sono stati provati a sufficienza nei call-site. Nessuna firma non verificata è stata dichiarata come certa. `caps.dat` nel provider corrente resta formato di record provider-specifico; le CAPS in uso sono dimensione `0x24`, max message `500`, max queue `16`, max players `8`, bytes/sec `1000`, latency `50`, turns/sec `8`, in-transit `2` (valori preesistenti, non reinterpretati come prova di wire protocol).

### Arità `stdcall` dell’oggetto compilato

`llvm-objdump -d build/qa/net/snp_elf.o` mostra, per le dichiarazioni attuali, `ret 0x10` per `snp_send`, `ret 0x0c` per `snp_receive`/`snp_free`, `ret 0x0c` per lock GameList, `ret 0x08` per unlock, `ret 0x10` per check-data-file, `ret 0x08/0x0c/0x04/0x04/0x08` per le dichiarazioni league e `ret 0x14` per `snp_send_external`. Questo verifica la generazione stdcall del nostro oggetto, **non** l’aderenza di ciascuna firma al provider originale; in particolare le firme non verificate non vanno abilitate in produzione.

## Test eseguito

- Compilazione `snp_elf.c` con clang i686, `-m32 -fno-pie -O1 -g` e include/avvisi del build shim, output privato `build/qa/net/snp_elf.o`.
- `tests/x86_snp_lan_test.c` compilato e linkato in `build/qa/net/x86_snp_lan_test`.
- Esito: **PASS**. Il test crea socket provider e peer distinti su loopback; esercita `spiInitialize`, `spiStartAdvertisingGame`, invio peer→provider e `spiReceive`, risposta con `spiSend`, ricezione peer e `spiFree`; supera anche il socket self-test. Output finale: `PASS: two UDP peers exchanged real loopback packets through spiSend/spiReceive/spiFree`.
- Il test non copre GameList/SelectGame né UDP broadcast, interoperabilità con un secondo processo StarCraft, né `spiSendExternalMessage`.

## `battle.snp`

`build/asset-sandbox/battle.snp` è un PE32 i386 Blizzard, image base `0x19000000`, esporta `SnpBind` e `SnpQuery`; importa `WSOCK32.dll` tramite ordinali (18 import ordinali), `storm.dll` tramite numerosi ordinali e API di registro (`ADVAPI32`). Le stringhe includono gateway `*.battle.net`, messaggi di connessione, `League.cpp`, `LeagueUI.cpp`, `league.ini`, `leagueID` e `replayUploadURL`. Il catalogo di stringhe e gli import indicano dipendenze da protocollo/servizi Battle.net, sessione/account, UI/league/replay e networking Winsock; non bastano per ricostruire il protocollo né per dichiarare PvPGN compatibile.

Per un supporto futuro servono almeno: ABI completa dei callback SNP, analisi statica/dinamica del provider originale e dei flussi Storm; specifica del protocollo Battle.net utilizzato da questa versione e gestione gateway/sessione/autenticazione; dipendenze di account/CD-key e relativi limiti di licenza/sicurezza; test controllati contro un server compatibile (ad es. PvPGN solo dopo aver verificato supporto/versione); comportamento UI, ladder/league e upload replay. La sola implementazione UDP LAN non fornisce queste componenti.

## File di proprietà modificati

- `runtime/shim-port/elf/snp_elf.c`
- `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c` (nuovo)
- `build/qa/net/` (oggetti, eseguibile e output d’ispezione)
- `reports/REPORT-net.md`
- `reports/STATE-net.md`

Nessun altro file di progetto è stato modificato; nessun comando git, input desktop o sotto-agente è stato usato.
