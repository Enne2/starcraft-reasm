# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/storm.orig.dll` (es. `0x1503xxxx` SNet) e/o IDA su copia privata di `storm.orig.dll` per ricavare i contratti binari ESATTI delle `spi*` mancanti! Controlla `ida_list` prima di usare IDA e non toccare sessioni altrui.
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: implementa una `spi*` SOLO dopo averne provato sul binario originale l'arità `stdcall`, gli argomenti e il layout delle struct (`SnpGame` / `GAMELIST` / `SNETADDR`); lascia esplicitamente non implementato ciò il cui contratto non è provato (o che è Battle.net-only).
4. **Isolamento**: per eventuali test tra 2 istanze StarCraft usa `SC_BIN="$PWD/build/qa/gap-net/starcraft-x86-net" ./tools/run-isolated.sh` con due ID separati (`lan-a`, `lan-b`) e chiudi sempre con `stop`.
5. **Divieti**: NON toccare file fuori proprietà (`win32_shim.c`, `host_elf.c`, `com_*.c`, `build/starcraft-x86`, `build/obj-x86/`). Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-gap-net`: Analisi SNet `storm.orig.dll` e completamento Multiplayer LAN (`spiLockGameList`, `spiSelectGame`, `spiSendExternalMessage`, test 2 istanze)

## File di proprietà (esclusivi)
- `runtime/shim-port/elf/snp_elf.c`
- `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c`
- `build/qa/gap-net/` (oggetti compilati, test binary, eventuale binario privato `starcraft-x86-net`)
- `reports/REPORT-net2.md`
- `reports/STATE-net2.md` (max 70 righe)

## Obiettivi
1. **Reverse engineering completo delle struct e firme `spi*` in `storm.orig.dll`**:
   - Decompila/disassembla in `storm.orig.dll` i chiamanti SNet di:
     - `spiLockGameList` (slot nella tabella `NetFunctions` + offset struct `SnpGame`/`GAMELIST` attesa da Storm!)
     - `spiUnlockGameList`
     - `spiSelectGame` (argomenti e cosa Storm si aspetta in output: `SNETADDR`? `dwFlags`? player info?)
     - `spiSendExternalMessage` / `spiReceiveExternalMessage` / `spiFreeExternalMessage`
     - `spiCheckDataFile` / `spiReportGameResult`
   - Cerca gli offset esatti nella `NetFunctions` table (`+0x00 .. +0x70`) in `0x1503xxxx` di `storm.orig.dll`!
2. **Implementa il discovery LAN UDP e `spiLockGameList`/`spiSelectGame` in `snp_elf.c`**:
   - Per ogni callback il cui contratto è dimostrato dal binario Storm, implementa il comportamento LAN UDP reale su `127.0.0.1` / LAN configurabile (`SC_LAN_BIND`, `SC_LAN_PORT`, lista porte/broadcast `SC_LAN_PEERS` per loopback multi-istanza sulla stessa macchina!).
   - Verifica le arità `stdcall` (`ret N`) con `llvm-objdump -d` rispetto ai call-site `storm.orig.dll`!
3. **Test unitario multi-peer + test opzionale 2 istanze StarCraft**:
   - Estendi `tests/x86_snp_lan_test.c` per testare `spiStartAdvertisingGame` -> discovery -> `spiLockGameList` -> `spiUnlockGameList` -> `spiSelectGame` -> `spiSend`/`spiReceive`/`spiFree` (e `ExternalMessage` se coperto).
   - Se `build/qa/gap-net/starcraft-x86-net` linka pulito (riusando `.o` di `build/obj-x86/` in sola lettura), prova anche **Multiplayer → Original → UDP/IPX** su due istanze isolate (`lan-a` e `lan-b`) con `./tools/run-isolated.sh` per verificare se la partita creata da `lan-a` appare nella lista di `lan-b`!
4. **Report, STATE e `qmem_store`**:
   - Chiudi tutte le istanze isolate, scrivi `reports/REPORT-net2.md` + `reports/STATE-net2.md` (con la tabella completa degli slot `NetFunctions` `+0x00..+0x70` verificati su Storm!), e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
