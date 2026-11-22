# Piano tecnico Multiplayer — LAN (UDP) & Battle.net / PvPGN (`battle.snp`)

**Data:** 2026-11-22  
**Workspace di riferimento:** `/home/enne2/dev/starcraft-reasm-generic`  
**File chiave esistenti:**
- `runtime/shim-port/elf/snp_elf.c`, `runtime/shim-port/elf/snp_elf.h`
- `runtime/shim-port/elf/win32_shim.c` (`LoadLibraryA`, `GetProcAddress`, `RegOpenKeyExA`/`RegQueryValueExA`, `SetTimer`/`KillTimer`, `GetTickCount`, `QueryPerformanceCounter`, `win32_snp_vfile_*`)
- `runtime/shim-port/bridge_shims.def`
- `tests/x86_snp_lan_test.c`
- `reports/REPORT-net.md`, `reports/STATE-net.md`
- `build/lift-full/storm.orig.dll.i64`, `build/asset-sandbox/standard.snp`, `build/asset-sandbox/battle.snp`

---

## 0. Regole operative obbligatorie per l'agente esecutore

1. **Oracolo primario (IDA Pro):** mai dedurre layout, offset di struct, arità `stdcall` (`ret N`), porte o framing di rete a tentoni. Decompilare e verificare in IDA:
   - `build/lift-full/storm.orig.dll.i64` (SNet `#101..#140+` e `sub_1503DE90`, `0x1503E320`, `0x1503AE50`, `0x1503C0A0`, `0x15039EA0`),
   - `build/asset-sandbox/standard.snp` (per il formato reale UDP LAN di `SnpGame`, `GAMELIST`, `SNETADDR`, pacchetti di discovery/join),
   - `build/asset-sandbox/battle.snp` (solo per analisi Battle.net / PvPGN).
2. **D19 fail-closed:** ogni callback/slot con contratto non dimostrato resta fail-closed con log esplicito; mai fabbricare dati di rete o inventare firme `stdcall`.
3. **Guest-ABI obbligatoria per tutti gli slot `SnpNetFunctions` (`snp_elf.c:414–456`):**
   - Ogni puntatore inserito in `SnpNetFunctions` (`g_slots[1..27]`) viene invocato dal codice tradotto con `call *slot`: **deve** essere uno stub guest-ABI ottenuto via `arm_bridge_lookup_thunk("snp_...")` e dichiarato in `bridge_shims.def` con arità `stdcall` esatta. Restituire un puntatore C diretto corrompe `%_cpu` / `_esp` (fault `pc=0x1`).

---

## 1. Stato attuale del multiplayer LAN (UDP)

**Esito sintetico:** lo shim implementa oggi un **trasporto unicast UDP loopback verificato a livello di callback**, ma **non è ancora possibile cercare, creare o giocare una partita LAN tra due istanze** di StarCraft.

### 1.1 Cosa è già implementato (`runtime/shim-port/elf/snp_elf.c`)

- **Identità e `caps.dat` virtuale:** `win32_snp_vfile_*` espone un archivio MPQ v1 in memoria per `standard.snp` con:
  - `caps.dat` a due entry: `id = 0` (`Native POSIX Loopback`, necessario per sbloccare Single Player in `#117`/`sub_1503DE90`) e `id = 0x5544504E` (`"Local Area Network (UDP)"`, che supera il filtro di enumerazione `#105` a `0x1503E2AC`).
  - `(signature)` minima (16 byte v0) per superare `0x1503DDD8`.
- **CAPS dichiarate (`g_caps`, `0x24` byte):** `flags=0x20000002`, `max_message_size=500`, `max_queue_size=16`, `max_players=8`, `bytes_per_second=1000`, `latency=50`, `turns_per_second=8`, `turns_in_transit=2`.
- **Socket UDP non bloccante (`spiInitialize`, slot `+0x1C`):**
  - Apre un socket `AF_INET` / `SOCK_DGRAM` con `SO_BROADCAST` e `O_NONBLOCK`.
  - Configurabile via env: `SC_LAN_BIND` (IPv4 numerico, default `127.0.0.1`), `SC_LAN_PORT` (porta decimale, default `0` effimera).
- **Invio / ricezione / rilascio:**
  - `spiSend` (`+0x38`, `ret 0x10`): itera `count` puntatori `SnpNetAddress *` interpretando i 16 byte come `struct sockaddr_in` Linux ed esegue `sendto`.
  - `spiReceive` (`+0x2C`, `ret 0x0C`): esegue `recvfrom(..., MSG_DONTWAIT, ...)` allocando `struct SnpPacket` (`16 B address + 4 B len + payload`).
  - `spiFree` (`+0x0C`, `ret 0x0C`): libera `address` (base di `SnpPacket`).
  - `spiCompareNetAddresses` (`+0x04`): confronto byte-per-byte a 16 byte.
- **Test attuale (`tests/x86_snp_lan_test.c`):** **PASS** — due endpoint UDP su loopback scambiano datagrammi reali tramite `spiSend`/`spiReceive`/`spiFree` (ma **non** verifica il protocollo SNet completo né due processi StarCraft).

### 1.2 Inventario completo dei 27 slot `SnpNetFunctions` (`dwSize = 0x70`)

| Offset | Slot | Funzione shim (`snp_elf.c`) | Stato attuale | Cosa serve verificare/completare |
|---|---|---|---|---|
| `+0x04` | `spiCompareNetAddresses` | `snp_compare_addresses` | Implementato (16 B) | Verificare valore atteso di `*differences` / return su Storm (`SNet`) |
| `+0x08` | `spiDestroy` | `snp_destroy` | Chiude socket, `return 0` | Verificare se Storm attende `TRUE` (`1`) su cleanup |
| `+0x0C` | `spiFree` | `snp_free` | Implementato (`free`) | Verificato |
| `+0x10` | `spiFreeExternalMessage` | `snp_free_external` | Fail-closed (`FALSE`) | Necessario solo per Battle.net / external msg |
| `+0x14` | `spiGetGameInfo` | `snp_get_game_info` | Fail-closed (`FALSE`, 4 arg `ret 0x10`) | Verificare uso in lobby/dettaglio partita LAN (`SNetGetGameInfo` / `0x1503CC83`) |
| `+0x18` | `spiGetPerformanceData` | `snp_get_performance_data` | Fail-closed (`FALSE`) | Usato da Storm per misure latenza / adattamento turn-rate |
| `+0x1C` | `spiInitialize` | `snp_initialize` | Implementato (socket UDP) | Verificare i 5 argomenti (`client`, `user`, `battle`, `module`, `event` HANDLE per async wakeup!) |
| `+0x20` | `spiInitializeDevice` | `snp_initialize_device` | `FALSE` | Corretto per UDP (nessun modem/cavo) |
| `+0x24` | `spiLockDeviceList` | `snp_lock_device_list` | `TRUE`, `*devices=NULL` | Corretto per UDP |
| `+0x28` | `spiLockGameList` | `snp_lock_game_list` | **Stub vuoto (`*games=NULL`)** | **BLOCCANTE LAN:** restituire lista `SnpGame` da discovery/broadcast |
| `+0x2C` | `spiReceive` | `snp_receive` | Implementato (`recvfrom`) | Segnalazione `event` di `spiInitialize` e interoperabilità `SNETADDR` |
| `+0x30` | `spiReceiveExternalMessage` | `snp_receive_external` | Fail-closed (`FALSE`) | Solo se usato da Battle.net / external |
| `+0x34` | `spiSelectGame` | `snp_unknown_select_game` | **Stub fail-closed (`FALSE`)** | **BLOCCANTE LAN:** join / selezione partita in lobby (`arità` e `arg` da verificare in Storm/standard.snp) |
| `+0x38` | `spiSend` | `snp_send` | Implementato (`sendto`) | Verificare formato reale `SnpNetAddress` (`SNETADDR`) con Storm |
| `+0x3C` | `spiSendExternalMessage` | `snp_send_external` | Fail-closed (`FALSE`) | Battle.net / chat esterna |
| `+0x40` | `spiStartAdvertisingLadderGame` | `snp_start_advertising_ladder` | Registra `g_game` locale | Solo ladder/SP (`#138`); verificare eventuale uso in LAN |
| `+0x44` | `spiStopAdvertisingGame` | `snp_stop_advertising` | Pulisce `g_game` locale | Fermare broadcast/beacon LAN quando host chiude lobby |
| `+0x48` | `spiUnlockDeviceList` | `snp_unlock_device_list` | `TRUE` | Corretto per UDP |
| `+0x4C` | `spiUnlockGameList` | `snp_unlock_game_list` | `TRUE` no-op | Rilasciare / sbloccare eventuale lista allocata in `spiLockGameList` |
| `+0x50` | `spiStartAdvertisingGame` | `snp_start_advertising` | **Solo locale (`g_game`)** | **BLOCCANTE LAN:** inviare beacon UDP / rispondere ai probe di discovery |
| `+0x54` | `spiReportGameResult` | `snp_unknown_report_result` | Fail-closed (`FALSE`) | Ladder / fine partita Battle.net |
| `+0x58` | `spiCheckDataFile` | `snp_check_data_file` | `FALSE`, `*available=0` | Verificare chiamata in multi/Battle.net |
| `+0x5C..+0x6C` | `spiLeague*` (5 slot) | `snp_league_*` | Fail-closed (`FALSE`) | Solo Battle.net ladder/replay |

---

## 2. Roadmap per completare il multiplayer LAN (UDP)

### 2.1 Ricostruzione esatta dall'oracolo IDA (`standard.snp` + `storm.orig.dll`)
Prima di scrivere codice di rete:
1. Aprire `build/asset-sandbox/standard.snp` in IDA (provider UDP nativo di Blizzard) e verificare:
   - La **porta UDP** standard del provider UDP (storicamente `6112`; in test locali multi-istanza sullo stesso host serviranno IP distinti tipo `127.0.0.1` / `127.0.0.2` su porta `6112`, oppure un override configurabile per discovery se due istanze condividono lo stesso IP!).
   - Il **layout binario di `struct SnpGame` / `GAMELIST`** restituito da `spiLockGameList` e letto da Storm (`SNet*` in `storm.orig.dll.i64`), incluse dimensione nodo, puntatore `next`, `game_id`, `game_name`, `game_statstring`, `host_address` (`SNETADDR`), `elapsed_time`, `creation_time`.
   - Il **layout di `SNETADDR` (16 byte)** in `standard.snp`: verificare se i 16 byte coincidono 1:1 con `struct sockaddr_in` Windows/Linux (`sin_family` little-endian `AF_INET=2`, `sin_port` big-endian, `sin_addr`, `sin_zero[8]`) o se `standard.snp` usa campi diversi / header extra.
   - Il **wire format dei pacchetti di discovery/advertisement UDP** in `standard.snp` (`spiStartAdvertisingGame` + thread/timer di beacon o risposta a broadcast query in `spiReceive`/`spiLockGameList`).
   - La firma `stdcall` e la semantica esatta di **`spiSelectGame`** (`+0x34`) e **`spiGetGameInfo`** (`+0x14`) in `storm.orig.dll.i64` e `standard.snp`.
2. Verificare in `storm.orig.dll.i64` il **5° argomento (`HANDLE event`) di `spiInitialize` (`+0x1C`)**: Storm usa un evento Win32 (`CreateEventA` + `WaitForMultipleObjects`) per risvegliare il thread SNet quando arrivano pacchetti in `spiReceive`! Se oggi `SetEvent(g_recv_event)` non viene chiamato all'arrivo di datagrammi UDP, il thread SNet può dormire fino al timeout (250 ms) aggiungendo enorme latenza!

### 2.2 Implementazione incrementale consigliata per la LAN
1. **Event wakeup su `spiInitialize`/`spiReceive`:** salvare l'event HANDLE passato a `spiInitialize` e chiamare `SetEvent` quando un thread/poll riceve dati (o durante il polling non bloccante), riducendo i timeout da 250 ms.
2. **Thread/lista di discovery UDP (broadcast + unicast):**
   - `spiStartAdvertisingGame` salva i metadati reali (nome, descrizione/statstring, `SNETADDR` host, timestamp `GetTickCount()`), abilita la risposta a richieste di discovery su porta UDP `6112` (e opzionale broadcast periodico sulla subnet / `SC_LAN_BROADCAST`).
   - `spiLockGameList` aggiorna i nodi `SnpGame` con TTL (rimuovendo lobby scadute), restituendo la lista collegata reale sotto mutex fino a `spiUnlockGameList`.
   - Nota test su singolo host: per provare due istanze sullo stesso PC `frigate`, `SC_LAN_BIND` su IP loopback distinti (`127.0.0.1` e `127.0.0.2`, entrambi su porta `6112`) permette di avere due socket sulla stessa porta UDP standard senza conflitti!
3. **`spiSelectGame` e `spiGetGameInfo`:** implementare con l'arità `stdcall` esatta verificata in IDA e aggiornare `runtime/shim-port/bridge_shims.def` (ricompilando gli shim e rigenerando `bridge_stubs.S` con `tools/build.sh`).
4. **Gate di verifica LAN:**
   - Test unitario multi-thread `tests/x86_snp_lan_discovery_test` su `127.0.0.1:6112` vs `127.0.0.2:6112` (advertising → lock/unlock game list → select game → scambio pacchetti `SNet`).
   - Prova end-to-end tra **due istanze `starcraft-x86`** su `127.0.0.1` e `127.0.0.2` (creazione lobby UDP, visibilità nella lista dell'altra istanza, join, avvio partita melee e avanzamento per ≥ 60 secondi senza stall o desync).

---

## 3. Rischi di timing, latenza e desync

StarCraft usa un motore **lockstep deterministico a turni**: tutti i peer devono eseguire gli stessi input al turno `T` prima di avanzare.

1. **Pacing vs determinismo su istanze omogenee (`x86` ↔ `x86` o `ARM` ↔ `ARM`):**
   - La logica di gioco liftata in `StarCraft.entry.o` esegue esattamente le istruzioni di `StarCraft.exe` (quasi interamente interi/fixed-point).
   - I timer host (`GetTickCount` → `CLOCK_MONOTONIC`, `QueryPerformanceCounter` in `win32_shim.c`) regolano FPS e scansione turni, **non** il PRNG di simulazione: differenze di wall-clock tra PC non causano desync di per sé.
2. **Rischi reali di stall, lag e `"Waiting for players"`:**
   - **Mancato `SetEvent` su `spiReceive` + `WaitForMultipleObjects` (`250 ms` timeout):** senza segnalazione immediata dell'evento di rete di `spiInitialize`, ogni turno può attendere il timeout periodico distruggendo la reattività (4 turni/s max invece di 8–24).
   - **`spiGetPerformanceData` (`+0x18`) stubbato a `FALSE`:** Storm usa i campioni di latenza per regolare `turns_per_second` / `turns_in_transit` (modalità Normal vs High/Extra High Latency). Se ritorna sempre `FALSE`, resta bloccato ai default statici di `g_caps` (`turns_per_second=8`, `turns_in_transit=2`, `latency=50`).
   - **`WM_TIMER` in `win32_shim.c` (`SetTimer`/`KillTimer`):** verificare che i timer registrati dal gioco/Storm vengano effettivamente consegnati al message loop quando scadono, se usati nei timeout di lobby/join.
   - **Code UDP (`max_queue_size=16`) e `recvfrom` singolo:** in `spiReceive`, svuotare in sicurezza senza drop e verificare se Storm gestisce autonomamente la ritrasmissione (SNet ha ack/resend interno sopra `spiSend`/`spiReceive` — verificare i pacchetti di retry in `storm.orig.dll.i64`).
   - **Sbilanciamento CPU tra host diversi:** se un peer rallenta (es. su Raspberry Pi Zero durante loading/audio/Smacker), l'intera partita si ferma in attesa dei suoi turni.
3. **ATTENZIONE — Rischio di desync reale nel cross-play `x86` ↔ `ARM`:**
   - In `runtime/common/x87_crt_arm.c` e negli helper `llasm`, le operazioni floating-point su ARM girano in IEEE-754 **double 64-bit**, mentre su `i686` usano **`x87` 80-bit extended precision** (se l'IR usa `x86_fp80` o `fadd`/`fmul`/`sin`/`cos`/`ftol` CRT).
   - Finché giocano insieme due `x86` (o due `ARM`), l'esecuzione è identica; **prima di abilitare partite miste `x86` ↔ `ARM`**, verificare su partita reale (o trace di checksum turno SNet) che nessun calcolo di gameplay/pathing/audio/init dipenda da arrotondamenti `80-bit` vs `64-bit`.

---

## 4. Supporto a Battle.net / server privati open source (PvPGN)

### 4.1 Server open source compatibile: PvPGN
- **PvPGN** (`pvpgn/pvpgn-server` / PvPGN-PRO) è un server Battle.net classico open source che gira nativamente su Linux e supporta ufficialmente **StarCraft / Brood War 1.16.1** (`bnetd` su TCP `6112`, file-backed senza necessità di MySQL/Lua per uso privato con amici; configurabile dietro NAT via `address_translation.conf`).
- Sui client **Windows/Wine originali 1.16.1** non serve modificare `battle.snp`: il client legge l'elenco gateway dal registro Windows in `HKCU\Software\Battle.net\Configuration` → valore `Battle.net gateways`.

### 4.2 Perché la nostra versione reasm oggi NON si collega a Battle.net / PvPGN
1. **`battle.snp` non è liftato né implementato:**
   - `build/asset-sandbox/battle.snp` (PE32 i386, image base `0x19000000`, esporta `SnpBind` e `SnpQuery`) **non** è tradotto in LLVM IR.
   - In `win32_shim.c:1097` e `snp_elf.c:716`, `battle.snp` è lasciato esplicitamente solo-risorse senza export (`LoadLibraryA`/`GetProcAddress` falliscono volutamente su `battle.snp`).
2. **Manca completamente `WSOCK32.dll` nello shim (`win32_shim.c` + `bridge_shims.def`):**
   - `battle.snp` importa **18 ordinali da `WSOCK32.dll`** (TCP/UDP/DNS: `WSAStartup`, `WSACleanup`, `WSAGetLastError`, `socket`, `closesocket`, `connect`, `send`, `recv`, `sendto`, `recvfrom`, `bind`, `select`, `ioctlsocket`, `setsockopt`, `gethostbyname`, `inet_addr`, `htons`/`ntohs`, `getsockname`, ecc. — verificare la lista esatta dalla IAT di `build/asset-sandbox/battle.snp`!).
   - Oggi nessuno di questi simboli Winsock è implementato in `win32_shim.c`.
3. **Manca la lettura dei gateway dal registro (`ADVAPI32.dll`):**
   - In `win32_shim.c:2292–2314`: `RegOpenKeyExA` accetta solo `"Blizzard Entertainment\\Starcraft"`, e `RegQueryValueExA` ritorna **sempre `ERROR_FILE_NOT_FOUND` (`2`)**.
   - Per connettersi a un server PvPGN custom (es. `192.168.1.50`), lo shim deve supportare la lettura di `Software\Battle.net\Configuration` → `Battle.net gateways` (`REG_MULTI_SZ`, configurabile via env es. `SC_BNET_GATEWAY="192.168.1.50,-1,My PvPGN"` o file INI) più l'eventuale salvataggio dell'ultimo gateway selezionato (`RegSetValueExA` / `RegCreateKeyExA`).
4. **Servizi interni di `battle.snp`:**
   - Handshake TCP `6112`, validazione versione/file (`IX86ver*.mpq`, `CheckRevision` — verificare quali file MPQ di check sono presenti in `build/asset-sandbox` e come `bnetd` di PvPGN verifica la `1.16.1`), hashing account/CD-key, canale chat/lobby, game creation/join UDP `6112`, eventuali stub puliti per `League` / `replayUploadURL`.

### 4.3 Strada architetturale consigliata per supportare PvPGN
- **Opzione consigliata (coerente con `storm.dll`): liftare e linkare anche `battle.snp` in LLVM IR:**
  1. Estrarre la lista esatta di tutti gli import di `build/asset-sandbox/battle.snp` (`WSOCK32.dll` ordinali, `storm.dll`, `KERNEL32`, `USER32`, `ADVAPI32`, ecc.) e verificare se servono altri ordinali Storm o Win32 non ancora presenti in `bridge_shims.def`.
  2. Implementare i 18 shim `WSOCK32.dll` su socket POSIX (incluso mapping errori `WSAGetLastError`, `SOCKET` non bloccante/`ioctlsocket(FIONBIO)`, `FD_SET` translation tra struct Winsock e `fd_set` POSIX!).
  3. Implementare nello shim di registro la chiave virtuale `Battle.net\Configuration` (`Battle.net gateways` in formato `REG_MULTI_SZ`, pilotabile da `SC_BNET_GATEWAY`).
  4. Liftare `battle.snp` in `battle.entry.o` (base `0x19000000`, `tools/dispatch-ir.py` + `gen_game_layout.py` per le sue sezioni `.data`/`.rdata`) e collegarlo come modulo in-process (stesso schema già collaudato per `storm.dll`), testando in locale contro `bnetd` di PvPGN (`loglevels = "fatal,error,warn,info,debug,trace"`).
- **Opzione alternativa (sconsigliata salvo blocco del lifting):** reimplementare da zero un provider `battle_elf.c` nativo che parli il protocollo BNETCS (`0xFF`) verso `bnetd`; evita il lifting di `battle.snp`, ma richiede di reimplementare a mano tutto il protocollo Battle.net/CheckRevision, i dialog/UI callback e i 27 slot `SnpNetFunctions` (inclusi external messages e league).
