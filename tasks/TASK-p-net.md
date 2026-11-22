# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/storm.orig.dll` (attorno a `0x1503xxxx` SNet / `spi*` / `SnpBind`) e `build/lift-full/StarCraft.orig.exe` per verificare firme `stdcall`, offset struct e protocolli esatti!
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: mai inventare firme non verificate; per callback non ancora testate live conserva controlli argomenti e logging chiaro.
4. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (NON toccare `win32_shim.c`, `bridge_shims.def`, `shim_rename.txt`, `bridge_dynamic.def`, `build/starcraft-x86`, `build/obj-x86/`.)
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-net`: Multiplayer LAN completo (`spiSend`/`spiReceive`/GameList/NetAddress in `snp_elf.c`) + analisi `battle.snp`

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/snp_elf.c`
- `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c` (nuovo)
- `build/qa/net/` (output compilazione/test privati)
- `reports/REPORT-net.md`
- `reports/STATE-net.md` (max 70 righe)

## Obiettivi
1. **Ispezione PE Storm SNet (`storm.orig.dll`)**:
   - Verifica nei call-site di `storm.orig.dll` (e `storm.llasm`) la signature `stdcall`, gli argomenti e i valori attesi per tutte le `spi*` attualmente mancanti in `snp_elf.c`:
     - `spiSend`, `spiReceive`, `spiFree`
     - `spiSendExternalMessage`, `spiReceiveExternalMessage`, `spiFreeExternalMessage`
     - `spiLockGameList`, `spiUnlockGameList`
     - `spiSelectGame`, `spiReportGameResult`, `spiCheckDataFile`
     - `spiLeagueCommand`, `spiLeagueSendReplayPath`, `spiLeagueGetReplayPath`, `spiLeagueLogout`, `spiLeagueGetName`
   - Verifica il formato esatto di `SNetGame` / `GAMELIST` / `SNETADDR` passato tra Storm e il provider SNP (`caps.dat`, `dwMaxPlayers`, `bytes_per_sec`, indirizzo IPv4/porta UDP `sockaddr_in`!).
2. **Implementa Multiplayer LAN UDP reale in `snp_elf.c`**:
   - Implementa `spiSend` / `spiReceive` / `spiFree` (UDP non bloccante su loopback/LAN configurabile via `SC_LAN_BIND` / `SC_LAN_PORT` + coda pacchetti allocati con `malloc` e liberati in `spiFree`).
   - Implementa `spiSendExternalMessage` / `spiReceiveExternalMessage` / `spiFreeExternalMessage`.
   - Implementa `spiLockGameList` / `spiUnlockGameList` / `spiSelectGame` (annuncio UDP broadcast / loopback delle partite locali create con `spiStartAdvertisingGame` così due istanze su `frigate` si vedono in LAN!).
   - Verifica se `spiCheckDataFile` / `spiReportGameResult` / `spiLeague*` hanno contratti verificabili su `storm.orig.dll` e documenta esattamente cosa resta Battle.net-only (`battle.snp`).
3. **Compila e testa in `build/qa/net/` (`clang --target=i686-unknown-linux-gnu -m32 -fno-pie`)**:
   - Verifica `snp_elf.c` compilato con gli stessi flag di `tools/build-shim-x86.sh` (`-O1 -g -fno-pie`).
   - Scrivi e esegui `tests/x86_snp_lan_test.c`: simula due peer UDP locali (host che fa `spiStartAdvertisingGame` + `spiSend`, client che fa `spiLockGameList` + `spiSelectGame` + `spiReceive` + `spiFree`) e verifica lo scambio reale di pacchetti e l'arità `stdcall` (`ret N` via `llvm-objdump`)!
4. **Analisi `battle.snp` & Report**:
   - Documenta in `reports/REPORT-net.md` cosa serve per supportare `battle.snp` (PvPGN/Battle.net) in futuro, tutti i risultati dei test LAN, e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
