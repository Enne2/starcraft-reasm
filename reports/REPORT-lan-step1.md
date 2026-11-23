# LAN-1 — Event wakeup e `SC_LAN_PEERS`

## Implementazione

- `runtime/shim-port/elf/snp_elf.c`: aggiunti parsing rigoroso di `SC_LAN_PEERS` (`IPv4:porta,...`, fino a 32 peer; errore = lista svuotata e log), `snp_peer_count()`/`snp_peers_foreach()` e fan-out di `spiSend(count==0)` verso tutti i peer. `SC_SNP_TRACE=1` registra peer count e conteggio fan-out.
- Aggiunta una sonda UDP non bloccante in thread interno: se c’è un datagramma pendente, segnala una volta l’evento; `spiReceive` lo segnala anche dopo la ricezione. Il thread si attiva solo se il quinto argomento è non nullo e `SetEvent` è risolvibile. Handle rifiutato o funzione non disponibile producono log esplicito.
- `runtime/shim-port/elf/snp_elf.h`: API dei peer documentata come indirizzo IPv4 `sockaddr_in` di 16 byte.
- `tests/x86_snp_lan_test.c`: test fan-out a due peer, iterazione helper e wakeup osservato prima di consumare il datagramma; resta il roundtrip UDP esistente.
- Ordine e numero degli slot `SnpNetFunctions` invariati.

## Evidenze IDA e limite sul wakeup

Analizzato `storm.orig.dll` dalla copia privata `/tmp/sc-ida-lan2a/storm.orig.dll` (nessuna annotazione o modifica al database IDA).

- In `sub_1503E320`, i cinque argomenti sono inoltrati allo slot `+0x1c` dal blocco `0x1503E4F1–0x1503E505`; l’istruzione di chiamata è `call dword ptr [edx+1Ch]` a `0x1503E505` (test del risultato a `0x1503E508`). Il callback riceve quindi cinque argomenti; il candidato event è il quinto parametro dello stdcall, caricato da `[ebp+arg_10]` a `0x1503E4F1` e pushato per primo a `0x1503E4F7`. Il wrapper `storm_117` passa il valore letterale `0` per questo argomento nella chiamata a `sub_1503E320` a `0x1503E73C`.
- L’unico chiamante trovato, `storm_117`, invoca `sub_1503E320` a `0x1503E73C` passando **0** come sesto argomento del wrapper, inoltrato come quinto argomento del callback. Di conseguenza, nel percorso statico osservato `spiInitialize` riceve NULL: non c’è un HANDLE valido da segnalare.
- Storm ha un evento interno distinto: `sub_1503D110` lo crea con `CreateEventA` a `0x1503D121`; il worker `sub_1503D080` attende quell’evento con `WaitForSingleObject(..., INFINITE)` a `0x1503D0B4`. Storm lo segnala, per esempio, in `storm_128` a `0x1503A78B`. L’evidenza non collega quell’evento interno all’argomento di `spiInitialize`.
- `WaitForSingleObject` è presente e usato. `WaitForMultipleObjects` non compare nella tabella completa degli import né nella ricerca IDA effettuata; non si afferma che sia impossibile un uso indiretto.
- Verifica shim: `win32_shim.c:1397` cerca il valore HANDLE esatto nella tabella; `SetEvent` (`:1447`) chiama `ev_get(h)` senza conversioni. Il provider chiama direttamente l’export con ABI `REASM_STDCALL`; non usa `arm_bridge_lookup_thunk`. Gli oggetti ARM confermano riferimento debole `SetEvent` nel provider e definizione `SetEvent` nello shim.

## Test e build

- Comandi gate i686 (include del toolkit locale):
  ```sh
  clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g -I "$HOME/dev/reasm-toolkit/runtime/elf" -I runtime/shim-port/elf -c runtime/shim-port/elf/snp_elf.c -o build/qa/lan-1/snp_elf.o
  clang --target=i686-unknown-linux-gnu -m32 -fno-pie -O1 -g -include stdint.h tests/x86_snp_lan_test.c build/qa/lan-1/snp_elf.o -pthread -no-pie -o build/qa/lan-1/x86_snp_lan_test
  for i in 1 2 3 4 5; do ./build/qa/lan-1/x86_snp_lan_test > build/qa/lan-1/final-$i.log 2>&1 || exit 1; done
  ```
  **Output sintetico:** `PASS_count=5`; due peer UDP reali, fan-out `count==0`, helper, roundtrip e wakeup prima del receive; latenza misurata **0,92–0,94 ms** (<50 ms). Il test sostituisce `SetEvent` con uno stub che controlla il valore HANDLE.
- Build x86: da `/tmp/lan-1-build`, `./tools/build.sh --arch x86 --skip-entry` → `creato: /tmp/lan-1-build/build/starcraft-x86` (**PASS**). Eseguita nella copia per non sovrascrivere gli output x86 condivisi.
- Build ARMv7: `ENTRY_DIR=$PWD/build/lift-full/entry-armv7 SYSROOT=$PWD/build/sysroot ./tools/build-runtime-armv7.sh` → `creato: .../build/starcraft-armv7` (**PASS**). Ripetuta dopo aver corretto la dichiarazione stdcall di `SetEvent`; gli output sono in `build/runtime-arm/` e `build/starcraft-armv7/`.
- Sandbox: `SC_PRESENT=cpu ./tools/run-isolated.sh start --id lan1 --skip-intro 1 --smack 1` → Xvfb `:106`, game vivo; dopo 3 s: 148 righe log, 0 errori fatali, 2 campioni FPS; `SC_PRESENT=cpu ./tools/run-isolated.sh stop --id lan1` → stopped. Zero occorrenze `SnpQuery`/`SnpBind`/`spiInitialize`: la prova non raggiunge il networking e non valida l’integrazione LAN nel gioco.
- Problemi transitori risolti: il primo compile isolato non aveva l’include del toolkit (`reasm_elf.h`); una successiva compilazione ha rilevato `parse_port` temporaneamente rimosso, poi ripristinato. La prima esecuzione del test ha segnalato peer list malformata per un errore nel costruttore della stringa di test, corretto prima dei run finali. I run finali e entrambe le build sono riusciti. Warning Clang su dichiarazioni `memset`/`malloc` preesistenti nel TU; non bloccanti.

## Decisioni autonome

- Ho mantenuto D19: tratto il quinto argomento come HANDLE candidato solo se non nullo, lo inoltro senza trasformarlo e segnalo i fallimenti. L’IDA mostra che il caller osservato passa NULL: per quel percorso il wakeup provider resta disabilitato, invece di segnalare l’evento interno Storm, che è un oggetto diverso. Il test usa un HANDLE non nullo fittizio per verificare il percorso condizionale.
- La build x86 è stata isolata in `/tmp` per non toccare gli output x86 condivisi. La build ARM richiesta ha prodotto gli output standard in `build/runtime-arm/` e `build/starcraft-armv7/`.

## Aperto

- Chiarire con evidenza runtime/ABI perché il caller osservato passa NULL e se esiste un altro percorso di inizializzazione con HANDLE valido. Finché non è provato, nessuna integrazione con l’evento interno Storm viene fatta.
- La fan-out spedisce datagrammi UDP ma non implementa discovery/beacon né il protocollo completo SNet; il layout `SNETADDR` e l’interoperabilità LAN reale restano da verificare.
- Nessun comando Git è stato eseguito.
