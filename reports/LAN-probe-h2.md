# H2 — GetPerformanceData (+0x18) e scheduling dei turni

## Esito

**Ipotesi falsificata come causa dimostrata dello stallo.** L'implementazione `spiGetPerformanceData` attuale risponde sempre FALSE, ma il binario Storm usa il callback solo per un campione per giocatore e non usa il suo risultato di ritorno per configurare il ritmo dei turni. Non emerge evidenza IDA che `turns_per_second` dipenda da questo callback. Inoltre, il provider originale `standard.snp` lascia nullo `spiCheckDataFile` (+0x58): non è un callback di readiness richiesto dal percorso standard.

Ho applicato nella copia privata il fix alla porta discovery, ricompilato e avviato due sandbox Xvfb isolate. I socket risultano bindati su 127.0.0.21:16021/16022 e 127.0.0.22:16021/16022, ma la prova UI non è arrivata all'advertising/join; quindi non posso attestare una partita o un test funzionale della discovery. Il tentativo precedente basato su SC_DISC_PORT non è valido per giudicare la scoperta: `disc_broadcast()` usava costantemente 6111, nonostante il bind sulla porta configurata. La causa del test invalido è documentata; ciò non modifica la falsificazione statica dell'ipotesi H2. Non dichiaro risolto il lockstep.

## Metodo e copie analizzate

IDA ha aperto solo copie private:

- `/tmp/lan-h2/ida/storm.orig.dll`, image base `0x15000000`, SHA-256 `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`.
- `/tmp/lan-h2/ida/standard.snp`, image base `0x1D000000`, SHA-256 `7de1bc524067e0f457e5a74112faff93eea8a97fe798a44496e99d68db150f6d`.

## Evidenze IDA

### `+0x18 spiGetPerformanceData`

- `standard.snp!SnpBind` a `0x1D00E3D0–0x1D00E42B` restituisce la tabella `unk_1D016E48` per il provider indice 4; l'intestazione della tabella dichiara dimensione `0x70` (`0x1D016E48`). La voce all'offset `+0x18` è il puntatore `0x1D0083A0`.
- In Storm, `0x1503E4EC` copia fino a `0x70` byte della tabella in `dword_1505E620`. Il callback all'offset `+0x18` è invocato da `storm_111` a `0x15037372`.
- `storm_111` valida `player < 16` e invoca il callback solo se il flag per quel giocatore `dword_1505A6B4[4*player]` è nonzero (`0x1503735B–0x15037372`). Prima della chiamata inizializza gli output locali: tre DWORD dalle tabelle `dword_1505A6A8/A6AC/A6B0`, un `FILETIME` ottenuto da `GetSystemTime`/`SystemTimeToFileTime`, e i due DWORD dell'output aggiuntivo a `10000000, 0` (`0x150372F8–0x15037341`). La chiamata riceve quattro argomenti: id giocatore, puntatore DWORD, puntatore FILETIME e puntatore al blocco aggiuntivo (`0x15037372`).
- `standard.snp!0x1D0083A0` ha quattro argomenti e ogni percorso termina con `retn 10h` (`0x1D0083C6`, `0x1D0083D9`, `0x1D0083ED`, `0x1D008401`, default `0x1D008407`). Scrive un DWORD soltanto per id 12–15 e ritorna 1; per gli altri id ritorna 0. Le variabili scritte (`Addend`, `dword_1D0178C8/CC/D0`) sono inizializzate a zero dal setup del provider a `0x1D009CAE–0x1D009CC0`. Il callback non usa gli argomenti FILETIME/blocco aggiuntivo.
- Il codice Storm a `0x15037372` non controlla il valore di ritorno del callback: il risultato della chiamata non è assegnato né testato; Storm prosegue con `LeaveCriticalSection` e restituisce 1 (`0x1503737A–0x15037381`). Nei call-site esaminati non c'è un aggiornamento di `turns_per_second` o `turns_in_transit` dal ritorno di questo callback. L'esatta unità semantica del DWORD campione non è nominata/provata dal codice: non la deduco. Il valore `10000000` è invece il valore iniziale del campo FILETIME aggiuntivo (unità FILETIME, intervalli da 100 ns), non una misura di latenza.

### `+0x58 spiCheckDataFile`

- Nella tabella originale `standard.snp` (`0x1D016E48`), la voce `+0x58` è zero; la tabella ha un callback presente a `+0x6C`, ma non a `+0x58`. Il dato è stato letto dalla tabella in IDA, non inferito dal nome della funzione.
- Storm non chiama `dword_1505E620+0x58` nei call-site esaminati. L'unica istruzione Storm trovata nella scansione generale con `call [reg+58h]`, `0x15034725`, appartiene a `storm_359` e chiama il vtable dell'oggetto DirectDraw (`dword_1505E664`), non il provider SNet.
- Lo shim corrente ha una funzione `snp_check_data_file` che pone `*available=0` (`runtime/shim-port/elf/snp_elf.c`, circa riga 1080), ma ciò non riproduce un callback standard attivo: il provider originale lascia quello slot NULL. Pertanto il valore FALSE/available=0 non è una spiegazione suffragata per il blocco del ready nel percorso UDP originale.

### CAPS e ritmo dei turni

- `runtime/shim-port/elf/snp_elf.c` costruisce `caps.dat` in ordine esplicito: `latency` a record `+24`, `turns_per_second` a `+28`, `turns_in_transit` a `+32`; i valori correnti sono 50, 8, 2 (`struct SnpCaps`, initializer vicino alle righe 59–61 e 154; writer alle righe 1347–1349).
- Non ho trovato nel call-site del callback +0x18 un flusso da quel risultato verso tali campi. `+0x18` fornisce dati per-giocatore; non imposta né adatta quei parametri CAPS.

## Esperimenti / numeri

1. SHA-256 delle due copie private coincide con gli artefatti di input (verificato con `sha256sum`).
2. Lettura IDA della tabella Snp: 1 callback `+0x18` presente (`0x1D0083A0`), slot `+0x58` nullo, dimensione tabella `0x70`.
3. Implementazione `+0x18`: 4 argomenti (`ret 0x10`), ritorno TRUE solo per gli ID 12, 13, 14, 15; FALSE negli altri casi. Output DWORD inizializzati a zero all'avvio.
4. Implementazione corrente dello shim: 1 callback sempre FALSE e nessun campione (log `runtime/shim-port/elf/snp_elf.c`, `snp_get_performance_data`, circa riga 727).
5. Build dopo il fix: `./tools/build.sh --arch x86 --skip-entry` completata con successo; una warning già non bloccante su `host_elf_ticks` in `input_elf.c:54`.
6. Due istanze avviate in Xvfb con audio dummy e configurazione isolata `SC_DISC_PORT=16021`, `SC_LAN_PORT=16022`, bind `.21`/`.22` e peer incrociati; `ss -lunp` conferma quattro socket attesi, discovery su 16021 e gioco su 16022. I log mostrano inizializzazione/SnpBind ma nessun `announce tx`, `discovery sendto` o `spiStartAdvertisingGame`: i click non hanno portato la UI alla fase di advertising. Le istanze sono state fermate. Quindi il fix è compilato e il bind custom provato, ma la discovery/join funzionale non è stata verificata in questo tentativo.
7. Screenshot verificati solo numericamente con `tools/shot-stat.sh`: mean 0.1694 e nonblack=1 per A; mean 0.169397 e nonblack=1 per B. Questi numeri attestano frame non neri, non la schermata o la partita.
8. Il dato runtime preesistente in `reports/REPORT-lan-step3.md` (clock fermo a 0:00) resta una riproduzione storica, non una prova valida su porte discovery custom: i peer non potevano ricevere i datagrammi inviati a 6111.

## Conclusione e limiti

L'ipotesi che il callback performance FALSE, o il `spiCheckDataFile` FALSE, impediscano direttamente il quorum del turno 0 non è sostenuta dal binario originale: il callback performance non determina CAPS e Storm ignora il suo return; `spiCheckDataFile` è NULL nella tabella UDP originale. È stato comunque corretto il bug indipendente della porta discovery: destinazioni broadcast globale/loopback e peer usano ora `g_disc_port`, valorizzata da `open_discovery_socket()` con la porta configurata. Resta da completare una prova UI fino all'advertising/join per misurare la discovery; il tentativo corrente non lo ha raggiunto. Resta aperto il vero motivo del lockstep.

## Decisioni autonome

Non alterare arità o semantica di callback senza prova binaria. Correggere invece la porta discovery configurabile, mantenendo invariati i callback H2; consegnare il diff in `build/qa/h2/LAN-FIX-H2.patch`. Il test su porta custom è valido solo se la prova raggiunge l'advertising e conferma pacchetti su quella porta.
