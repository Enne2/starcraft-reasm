# TASK LAN-1 — Event wakeup su `spiInitialize` + `SC_LAN_PEERS`

Repo: `/home/enne2/dev/starcraft-reasm-generic` (branch `main`). **NON fare git** (commit e push li fa il supervisore).
Le prove si fanno **solo** in sandbox Xvfb con audio dummy, in background: **mai** sullo schermo dell'utente (`:0`).

## Contesto misurato (non ri-verificarlo, usalo)
- Provider SNP nativo in `runtime/shim-port/elf/snp_elf.c` (828 righe, 27 slot `SnpNetFunctions`, `dwSize=0x70`).
- Già implementati: socket UDP non bloccante, `spiSend` (`sendto`), `spiReceive` (`recvfrom` MSG_DONTWAIT),
  `spiFree`, `spiInitialize`, advertising **solo locale** (`g_game`), `spiLockGameList` vuota, `spiSelectGame` fail-closed.
- Env esistenti: `SC_LAN_BIND` (IPv4 numerico, default 127.0.0.1), `SC_LAN_PORT` (default 0 = effimera),
  `SC_NET_POLL_MS`, `SC_SNP_TRACE`.
- Storm chiama `spiInitialize` (slot `+0x1c`) con **5 argomenti** stdcall al call-site `0x1503E50A`
  (`client, user, battle, module, event`), vedi `reports/REPORT-net.md`/`REPORT-net2.md` e `PLAN-MULTIPLAYER.md` §2.1.
  Il 5° argomento è un event HANDLE Win32 che Storm attende (timeout ~250 ms): senza `SetEvent` la latenza di rete è pessima.
- Gate esistente: `tests/x86_snp_lan_test.c` (compila con `runtime/shim-port/elf/snp_elf.c`) → oggi **PASS**
  ("two UDP peers exchanged real loopback packets"). Va esteso, non riscritto da zero.

## Obiettivo
1. **Event wakeup**: memorizzare l'HANDLE ricevuto da `spiInitialize` e chiamare `SetEvent(handle)` quando un
   datagramma è disponibile (nel percorso di `spiReceive`, e in un helper di poll interno se ne serve uno).
   Verifica nell'oracolo IDA (copia privata in `/tmp`, **non** il DB dell'utente) quale dei 5 argomenti è l'event
   e se Storm usa `WaitForSingleObject`/`WaitForMultipleObjects`: cita indirizzo + istruzione nel report.
   Attenzione: l'HANDLE è un handle **guest**; verifica in `runtime/shim-port/elf/win32_shim.c` che `SetEvent`
   accetti il valore così com'è (tabella handle) e che non serva `arm_bridge_lookup_thunk`.
2. **`SC_LAN_PEERS`**: lista `ip:port,ip:port,...` (IPv4 numerici). Semantica:
   - `spiSend(count==0)` (o flag esplicito documentato) invia a **tutti** i peer della lista;
   - helper pubblico `snp_peers_foreach()`/`snp_peer_count()` riusabile dal task successivo (beacon LAN);
   - logging con `SC_SNP_TRACE=1`.
3. **Test**: estendere `tests/x86_snp_lan_test.c`: (a) fan-out su 2 peer dichiarati in `SC_LAN_PEERS`;
   (b) `SetEvent` osservato entro <50 ms dall'arrivo del datagramma; (c) il test esistente resta PASS.

## File di tua proprietà (esclusivi)
- `runtime/shim-port/elf/snp_elf.c`, `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c`
- `build/qa/lan-1/` (tuoi oggetti/binari/log)
- `reports/REPORT-lan-step1.md`
Non toccare: `win32_shim.c`, `host_elf.c`, `com_*.c`, `bridge_shims.def`, `build/starcraft-x86`, `build/obj-x86/`.
Se `SetEvent` non è esportabile/utilizzabile dal nostro codice, **dichiara il limite** e proponi l'alternativa
(es. tick interno + `SC_NET_POLL_MS`) invece di inventare ABI.

## Vincoli
- **D19 fail-closed**: nessuna firma/struct inventata; ciò che non è provato resta negato con log esplicito.
- Ordine degli slot della tabella `SnpNetFunctions` **immutabile**.
- Build: `./tools/build.sh --arch x86 --skip-entry` e
  `ENTRY_DIR=$PWD/build/lift-full/entry-armv7 SYSROOT=$PWD/build/sysroot ./tools/build-runtime-armv7.sh`
  (l'entry ARM in `build/lift-full/entry/` è vecchio e produce un binario che crasha: **non usarlo**).
- Test in sandbox: `SC_PRESENT=cpu ./tools/run-isolated.sh start --id lan1 --skip-intro 1 --smack 1` … `stop --id lan1`.
  Se esce `ERROR: no free Xvfb display`, ripulisci i lock orfani: per ogni `/tmp/.X<n>-lock` leggi il PID dentro il
  file, se il processo non esiste rimuovi lock e `/tmp/.X11-unix/X<n>`; **mai** toccare `:0`.
- Verifica **numerica**: niente immagini nel contesto (usa log, `tools/shot-stat.sh`, contatori).
- Report finale `reports/REPORT-lan-step1.md`: cosa è stato implementato, evidenze IDA (indirizzi), comandi di
  test e output sintetico, cosa resta aperto.
