# H1 — messaggi esterni SNet: evidenze, probe e risultato

Data: 24 novembre 2026. Copia di lavoro: `/tmp/lan-h1`; binario originale copiato in `/tmp/storm-h1.orig.dll` (SHA-256 `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25`, base IDA `0x15000000`). Nessun accesso in scrittura a `/home/enne2/dev/starcraft-reasm-generic`; nessun comando Git.

## Esito aggiornato dopo il fix discovery

**La precedente conclusione sperimentale sulla LAN era invalida:** `disc_broadcast()` usava `SNP_DISC_PORT_DEFAULT` (6111) anche quando i socket ascoltavano su `SC_DISC_PORT=16011`. Quindi i peer dei test precedenti non potevano scambiarsi annunci/probe sulla porta di discovery configurata. Il test locale di ritorno `TRUE` con output nulli dimostra soltanto che TRUE senza payload non produce un messaggio; non falsifica l'ipotesi relativa al blocco di una partita LAN reale.

Applicato il fix: A invia ora probe/annuncio e peer su porta `16011`; B riceve il datagramma di annuncio `0x58`, valida il sigillo e inserisce `p16test` nella game list. **Il fix ripristina la discovery**, ma il client non ha ancora eseguito il join: non risultano `spiGetGameInfo`/`spiSelectGame`, pacchetti SNet gameplay, mappa o clock da misurare. H1 è quindi **aperta, non falsificata**. Non ho fabbricato payload/readiness perché ownership e formato del messaggio external restano non provati.

## Evidenze IDA e ABI

Le chiamate sono indirette attraverso il puntatore `dword_1505E620`, usato dalla tabella NetFunctions di `standard.snp`.

| Slot | Evidenza dal binario originale | Contratto ricavabile |
|---|---|---|
| `+0x30 spiReceiveExternalMessage` | `sub_15039370`, `0x15039393`: `call dword ptr [eax+30h]` dopo tre `lea`/`push` per gli indirizzi di uscita. `0x15039396 test eax,eax`; `0x15039398 jz 0x1503940C`; il ciclo richiama lo slot a `0x15039405`. | 3 argomenti, stdcall, `ret 0x0C` dedotto dai tre argomenti sullo stack e dall'assenza di `add esp` dopo la call. `EAX != 0` significa che Storm tenta di consumare un risultato. Prima della call azzera tutti e tre gli output a `0x1503938A–0x15039390`. Se address, data o size sono nulli, interrompe il drain a `0x150393A5`, `0x150393AC` o `0x150393B3`; non li dereferenzia ulteriormente né chiama il free in quel caso.
| `+0x10 spiFreeExternalMessage` | Nello stesso consumer, `0x150393DF–0x150393E8`: push di size, data, address e `call dword ptr [edx+10h]`; gli output vengono azzerati a `0x150393FC–0x15039402`. Un altro consumer, `sub_15037D00`, chiama invece `+0x0C` (slot diverso) a `0x15037DB6`. | Per `+0x10`: 3 argomenti stdcall, `ret 0x0C` dedotto dalla call-site. Storm chiama il free solo dopo aver ricevuto tre output non nulli. **Quale allocatore/buffer liberare non è deducibile dai call-site esaminati**, perciò il provider non libera puntatori sconosciuti.
| `+0x3C spiSendExternalMessage` | `storm_134` (#134), `0x1503999A–0x150399B0`: push di cinque argomenti e `call dword ptr [edx+3Ch]`; il risultato è salvato da `0x150399B8` e restituito da `0x150399C0`. Il primo argomento è il buffer locale `"\\\\.\\game\\%s"` costruito a `0x1503997A–0x1503998B`; il secondo è `v2+8`; gli argomenti 3 e 4 puntano a `unk_150512A2`; il quinto è l'argomento del wrapper. | 5 argomenti stdcall, `ret 0x14` dedotto dai cinque push senza cleanup del chiamante. `EAX` è propagato come risultato dell'operazione. Il call-site usa un nome `\\.\game\...`: non è una trasmissione UDP del normale lockstep; formato e ownership dei buffer restano non provati.

### Collegamento effettivo all'handshake

`storm_118` (#118), `0x1503CB20`, crea/inizializza il contesto SNet e chiama `sub_1503B900` a `0x1503CD55` dopo aver impostato il valore `2` a `0x1503CD4E`. La stessa barriera è richiamata a `0x1503CE0B` con valori `9` e `10` preparati a `0x1503CDFD–0x1503CE04`. In `sub_1503B900`, `0x1503B934` chiama `sub_15039370` ad ogni iterazione; la funzione riceve external messages e passa i dati ricevuti a `storm_372` come messaggio `SNET` (`0x150393BE–0x150393D1`). L'indice byte a `data+9` è segnato in `dword_1505BBD0` a `0x15037D51–0x15037D5C`; la barriera lo controlla a `0x1503B950–0x1503B975`.

Questo prova che **il canale receive external è coinvolto nella barriera di setup/handshake**, ma non prova che lo stallo del turno 0 sia causato dal suo ritorno `FALSE`: senza un messaggio reale non c'è un indice da marcare e `TRUE` da solo non può soddisfare la barriera.

## Esperimenti isolati

Build finale: `./tools/build.sh --arch x86 --skip-entry` — PASS. Binario `build/starcraft-x86`, SHA-256 `70d7077091d60e2f5c5a616c49da24b0bdf0d51894807eb337b3457dc12ae197`. Una warning preesistente in `input_elf.c:54` (address-of-function sempre true); nessun errore.

Tutti i test sono stati eseguiti con `tools/run-isolated.sh`, Xvfb privati e `SDL_AUDIODRIVER=dummy`; A `127.0.0.11`, B `127.0.0.12`, discovery `16011`, gioco `16012`, peer incrociati su `16011`. `ss -lunp` ha confermato i quattro bind. Nessuna porta `6111/6112` è stata usata come endpoint di bind.

1. **Rivalutazione dei run precedenti:** A ascoltava su 16011 ma annunci/probe erano inviati su 6111, perciò l'assenza di discovery/join in quei run non era prova contro la connettività peer. I conteggi dei callback receive e il probe `TRUE`/output nulli restano osservazioni locali su Storm, non un test LAN valido.
2. **Run col fix**, ritorno external normale `FALSE`: entro 138 s di processo A ha inviato 8 probe (20 byte) e 1 annuncio da `0x58` byte; trace di A mostra le destinazioni `0xFFFFFFFF:0x3E8B`, `0xFFFFFF7F:0x3E8B` e peer `0x0C00007F:0x3E8B` (16011). B ha ricevuto 8 probe e l'annuncio `type=0x0000`, `len=0x58`, seal/calcolato `0xD52A`, poi `[snp-trace] game list +: p16test ... Bottleneck`. A ha registrato 387 chiamate external receive con output `0,0,0`, ritorno `0`; B zero. Nessuna chiamata `spiSendExternalMessage`/`spiFreeExternalMessage`, nessun datagramma gameplay (`spiSend` log = 0).
3. **Limite del run corretto:** l'inserimento della voce nella lista è verificato, ma non è stato raggiunto un join. Non compaiono `spiGetGameInfo`, `spiSelectGame`, messaggio di join o caricamento mappa; nessun clock SNet/turno misurabile. L'overlay e il requisito ≥60 s non sono stati verificati.

Log: `build/isolated/h1-fix-{a,b}/game.log`. Screenshot usati solo attraverso `tools/shot-stat.sh`: A mean `0.118584`, B mean `0.169442`, max `1`, nonblack `1`; nessuna immagine inserita nel contesto.

## Patch e limiti

`build/qa/h1/LAN-FIX-H1.patch` ora include sia la diagnostica external (esperimento opt-in `SC_H1_EXTERNAL_TRUE=1`, default invariato) sia il fix discovery: variabile `g_disc_port`, tutte le tre destinazioni di `disc_broadcast()` basate su di essa e assegnazione dal socket aperto. Nessuna arità è cambiata; i file `.def`/rename non sono stati toccati.

**Cosa resta aperto:** completare la selezione/join dal client, arrivare alla mappa e osservare il turno 0; confrontare ritorni false/true soltanto in una partita peer effettivamente avviata; contratto/ownership del messaggio external e sequenza pacchetti per ≥60 s. La diagnosi «FALSE da solo blocca il lockstep» non è stata verificata in condizioni valide di peer; il precedente test non può falsificarla. Il fix ha provato la discovery, non il gameplay.

## Decisioni autonome

Ho mantenuto fail-closed l'assenza di messaggi: non ho costruito buffer né marcato readiness con payload inventati, perché i call-site non provano l'allocatore né il formato completo. Ho corretto il test di rete usando solo le porte/IP autorizzate. Nessun file o processo fuori dalla copia/sandbox è stato modificato o avviato.
