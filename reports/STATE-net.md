# STATE scx86-net

- Stadio: trasporto UDP unicast loopback compilato/testato; non è multiplayer LAN completo.
- Inviati/ricevuti datagrammi reali tramite `snpSend`/`snpReceive`; `snpFree` libera il buffer provider-owned. Peer loopback test: PASS.
- Bind: `SC_LAN_BIND` (IPv4, default 127.0.0.1), `SC_LAN_PORT` (default/0 effimera).
- `SnpGame`/`GAMELIST`/`SNETADDR` nativi non verificati sufficientemente: no annuncio/lista/select game implementati; GameList vuota e select fail-closed.
- External messaging, ReportGameResult, CheckDataFile e League: non attestare come compatibili; contratti non conclusi e/o Battle.net-specifici.
- `battle.snp`: PE32 i386 con SnpBind/Query; import Winsock ordinali, Storm ordinali e ADVAPI32. Protocollo/servizi account, UI, league e replay restano fuori supporto.
- Evidenze dettagliate, riferimenti IDA e limiti: `reports/REPORT-net.md`.
- Output: `build/qa/net/x86_snp_lan_test`, `snp_elf.o`, `stdcall-ret.txt`.
