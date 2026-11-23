# LAN-2A — Contratti del provider UDP/IP originale

## Esito

Il provider UDP/IP usa **UDP 6111 per il discovery** e **UDP 6112 per il traffico di gioco**. Il nodo nativo è di **`0x13C` byte**, con indirizzo Winsock inline a `+0x0C`, due stringhe inline da 128 byte e `next` a `+0x128`. L'annuncio effettivo passa per **`NetFunctions+0x40`, 10 argomenti** (`storm:15037BF8 call dword ptr [edx+40h]`, `snp:1D009019 retn 28h`). **Non passa per `+0x50`**: la voce UDP a `1D016E98` è zero, così come `+0x58` a `1D016EA0`. I due buffer da 128 byte a `+0x50` sono output opzionali di un'altra callback, non input dell'annuncio.

## Oracolo e reperti

Analisi statica IDA Pro/Hex-Rays su copie private in `/tmp/sc-ida-lan2a/`. Gli indirizzi sono VA originali: `snp` = `standard.snp` base `0x1D000000`, `storm` = `storm.orig.dll` base `0x15000000`, `exe` = `StarCraft.orig.exe` base `0x00400000`. I numeri di argomenti qui sotto vengono dalle istruzioni x86, non dai prototipi inferiti da Hex-Rays. SHA-256 degli input, uguali sulle copie:

| Input | SHA-256 |
|---|---|
| `build/asset-sandbox/standard.snp` | `7de1bc524067e0f457e5a74112faff93eea8a97fe798a44496e99d68db150f6d` |
| `build/lift-full/storm.orig.dll` | `706ff2164ca472f27c44235ed55586644e5c86e68cd69b62d76f5a78778bff25` |
| `build/lift-full/StarCraft.orig.exe` | `ad6b58b27b8948845ccfa69bcfcc1b10d6aa7a27a371ee3e61453925288c6a46` |

Dump IDA con decompilati e istruzioni: `build/qa/lan-2a/snp-slots.txt`, `snp-udp.txt`, `snp-discovery-helpers.txt`, `snp-checksum.txt`, `storm-slots.txt`, `storm-aux.txt`, `storm-advertise-list.txt`, `storm-init-fields.txt`, `storm-client-data.txt` e `storm-callers.txt` (tutti nella stessa directory). Il dump dell'EXE è `build/qa/lan-2a/starcraft-calls.txt`. Nessuna deduzione dai sorgenti del port. Chiuse solo le tre sessioni private (`1425ab1d`, `a14651ac`, `cbfe703c`); le due sessioni preesistenti risultavano ancora aperte dopo la chiusura.

### Selezione della tabella e risoluzione Winsock

- `snp:1D00E540 mov esi, off_1D016E18` identifica `SnpQuery(4)` come **Local Area Network (UDP/IP)**, ID `0x5544504E` letto da `1D016E14`. `SnpBind(4)` a `snp:1D00E425 mov dword ptr [eax], offset unk_1D016E48` restituisce la tabella che inizia con `dwSize=0x70`. Storm `1503E492 call eax` invoca `SnpBind`; `1503E49F cmp dword ptr [eax],50h` verifica la dimensione minima, `1503E4D3 mov byte ptr [eax],0` azzera la copia e `1503E4EC call storm_491` copia al massimo `0x70` byte in `dword_1505E620` (assegnato a `1503E4C6`). I call-site sono stati trovati dagli xref a quest'ultima globale, seguendo anche i `mov reg,[reg+offset]` prima delle call indirette.
- Le API socket non hanno voci IAT dirette in `standard.snp`: `1D008C29 push offset aWsock32Dll`, `1D008C2E call ds:LoadLibraryA`, poi `GetProcAddress` per ordinale, con risultati salvati nella tabella `dword_1D017A20`. Per es. `1D008CAE call GetProcAddress` preceduto da `push 9`, risultato scritto a `1D008CB8 mov [edx+8],eax`: è `htons`. Le coppie ordinale/offset rilevanti sono `2/+0x00 bind`, `9/+0x08 htons`, `17/+0x0C recvfrom`, `20/+0x10 sendto`, `21/+0x14 setsockopt`, `23/+0x18 socket`, tutte in `sub_1D008C20`. `snp:1D009CE2 call ds:LoadLibraryA` carica opzionalmente `ws2_32.dll` e `1D009CF7 call ds:GetProcAddress` cerca `WSAIoctl`. Nomi degli ordinali e costanti WinSock riscontrati in [wsock32.spec](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/wsock32/wsock32.spec) e [WinSDK ws2def.h](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/shared/ws2def.h); comportamento e valori nel report provengono dalle istruzioni IDA.

## 1. `SNETADDR` nel provider UDP

È un **`sockaddr_in` WinSock trasferito come 16 byte**, non un IP/porta Blizzard in ordine host. Non è una definizione universale di `SNETADDR` per gli altri provider.

| Off. | Larghezza | Contenuto e prova `snp` |
|---:|---:|---|
| `0x00` | 2 | `sin_family` u16 LE = 2 (`AF_INET`): `1D009DBF mov word ptr dword_1D0179DC,si`, `1D009DF9 mov word ptr dword_1D0179F0,si`, con `ESI=2` nell'inizializzazione. |
| `0x02` | 2 | `sin_port` in network order: `1D009DE8 push 17E0h` (6112), `1D009E00 call dword ptr [eax+8]` (`htons`), `1D009E17 mov word ptr dword_1D0179F0+2,ax`. Per il record proveniente da discovery, `1D0089AC push 17E0h`, `1D0089B1 call eax`, `1D0089B6 mov [esi+2],ax` sostituiscono la porta sorgente con 6112. |
| `0x04` | 4 | IPv4 in network order: `1D009DB5 mov dword_1D0179E0,0FFFFFFFFh` per il broadcast, `1D009DD1 mov dword_1D0179F4,edx` con `EDX=0` per bind wildcard; negli indirizzi remoti i byte di `recvfrom` non vengono convertiti. |
| `0x08` | 8 | `sin_zero`: azzerato per gli indirizzi costruiti (`1D009DA6`, `1D009DB0`, `1D009DD7`, `1D009DED`, store dei dword zero); i 16 byte ricevuti sono copiati/confrontati integralmente, non viene applicata una seconda normalizzazione del padding. |

Il socket di gioco espone a Storm il puntatore all'indirizzo in testa al record RX (`1D00832F mov [ebx],eax`), con payload separato a `+0x10` (`1D008337 add ecx,10h`; `1D00833A mov [edi],ecx`). In TX `1D00827A push 10h` e `1D00827C push edx` consegnano lo stesso formato a `1D008289 call dword ptr [eax+10h]` (`sendto`). Sul join, Storm ricava `&node[0x0C]` a `1503CCAD lea eax,[ebp+var_138]` (`node` in `var_144`) e lo passa in EAX a `1503CCB9 call sub_15039590`; quella funzione confronta quattro DWORD di indirizzo a `150395A0 mov ecx,4` / `150395AF repe cmpsd`. Nessun `ntohs` necessario nei percorsi tracciati: porta copiata come byte e non interpretata numericamente; l'ordinale 15 non è risolto dalla tabella UDP (`snp:1D008C7B`–`1D008D6E`).

## 2. `SnpGame` / `GAMELIST`

**Dimensione nativa 316 byte = `0x13C`.** Produttore `snp:1D008B7F push 13Ch`, `1D008B86 call storm_401`, `1D008B8F mov ecx,4Fh` / `1D008B9C rep movsd`. Consumatore `storm:15038B06 mov ecx,4Fh` / `15038B0F rep movsd`, poi `15038B17 add eax,13Ch`. Non usare come `sizeof` la riserva Storm per snapshot (`15038ACD imul eax,23Ch`–`15038AE0 call storm_401`, `572 = 316+256` byte per voce ammissibile).

| Off. | Campo / larghezza | Produttore UDP / consumatore Storm |
|---:|---|---|
| `0x00` | `game_id`, u32 locale | Conserva l'ID del nodo sostituito (`1D0089F8 mov ecx,[ebx]`), altrimenti incrementa la sequenza ed evita zero (`1D008A73`–`1D008A8D`); `1D008AC4 mov [ebp+var_140],ecx`. Storm enum `1503920E mov ecx,[esi]`; GetGameInfo `1D008474 cmp ebx,[esi]`. Non è un ID trasmesso sul filo. |
| `0x04` | flags, u32 | Datagramma `+0x10` → `1D008AAD mov edx,[ebx+10h]`, `1D008ACD mov [ebp+var_13C],edx`. Storm filtra col mask `0x0B` a `15038AB0 test byte ptr [eax+4],0Bh`. |
| `0x08` | u32, zero nel produttore `[?]` | Azzeramento dell'intero record a `1D008ABD rep stosd`; nessuna scrittura specifica in questo produttore. |
| `0x0C` | `host_address[16]` inline | `1D008ABF`–`1D008AEB` copia i quattro dword dall'indirizzo RX dopo aver normalizzato la porta; `storm:1503CCAD`–`1503CCB9` lo usa per il join. |
| `0x1C` | u32, valore 50 `[?]` | `1D008AF1 mov [ebp+var_124],32h`. Unità e significato non provati. |
| `0x20` | tick ricezione/creazione del nodo, u32 ms | `1D008AFB call ds:GetTickCount`, `1D008B11 mov [ebp+var_120],eax`; il purge legge `1D008719 mov edx,[esi+20h]`. È rinnovato alla sostituzione del nodo: non è un timestamp assoluto né la creazione della lobby remota. |
| `0x24` | dword esposto all'enumerazione (candidato `elapsed_time`), zero in UDP `[?]` | Resta al valore di `1D008ABD rep stosd`; Storm lo inoltra come quinto dword della descrizione di enum (`15039222 mov ecx,[esi+24h]`, `15039225 mov [ebp+var_10],ecx`). L'elapsed in secondi calcolato per advertising non viene serializzato da UDP. |
| `0x28` | `name[128]` inline | `1D008B01`–`1D008B17` copia dal pacchetto con limite `0x80`. Storm `15039216 lea edx,[esi+28h]`, non dereferenzia un puntatore. La ricerca UDP fa `_stricmp` di questo campo (`1D00847D`–`1D008482`). |
| `0xA8` | `statstring[128]` inline | `1D008B21 lea esi,[esi+eax+1]` avanza oltre il NUL del nome; `1D008B1C`–`1D008B2D` copia col limite `0x80`. Storm `15039210 lea eax,[esi+0A8h]`; il callback dell'EXE passa la seconda stringa al parser `sub_472570` (`004D367E mov ecx,[esi+0Ch]`, `004D3684 call sub_472570`). |
| `0x128` | `next`, puntatore PE32 | Producer inserisce in testa a `1D008B9E mov ecx,dword_1D0179EC` / `1D008BA4 mov [eax+128h],ecx`; consumer avanza a `15038AB8 mov eax,[eax+128h]`. |
| `0x12C` | puntatore dati binari aggiuntivi | Allocazione separata `1D008B4E call storm_401`, store `1D008B5F mov [ebp+var_14],eax`; Storm lo legge e, nell'enumerazione, fa deep copy e riscrive il puntatore (`15038B11`–`15038B3C`). Non è `statstring`. |
| `0x130` | lunghezza blob, u32 | `1D008B36`–`1D008B4B`: fine datagramma meno inizio blob; `15038B23 mov ecx,[edx+130h]` e `15038B42 add eax,[edx+130h]`. |
| `0x134` | product ID, u32 | `1D008B53 mov ecx,[ebx+8]`, `1D008B59 mov [ebp+var_C],ecx`; nel join `1503CC8A mov eax,[ebp+var_10]`, `1503CC8D cmp eax,dword_1505BD98`. |
| `0x138` | versione, u32 | `1D008B56 mov edx,[ebx+0Ch]`, `1D008B69 mov [ebp+var_8],edx`; nel join `1503CC99 mov ecx,[ebp+var_C]`, `1503CC9C cmp ecx,dword_1505BBC4`. Mismatch in uno dei due campi: `1503CF1B push 85100079h`. |

**Ownership.** Il provider possiede testa, nodi e blob (allocazioni `snp:1D008B4E/1D008B86`). Libera blob e nodo nel replace o ritiro (`1D008A12/1D008A52`), nel purge (`1D008746/1D008758`) e nel teardown (`1D0092B6/1D0092FA`). Lock restituisce un prestito della testa, anche `NULL`, non un trasferimento. Storm `sub_15038A70` conta e copia sotto lock, costruisce il proprio snapshot, poi passa **la testa originale** a Unlock (`15038B52 mov edx,[ebp+var_4]`, `15038B61 push edx`). `storm_133` usa lo snapshot per i callback e lo libera (`15039242 call [ebp+arg_8]`, `15039267 call storm_403`). GetGameInfo è diverso: copia **solo il record** nel buffer del chiamante (`snp:1D00849D mov ecx,4Fh`, `1D0084A2 rep movsd`), lasciando i puntatori interni shallow/borrowed anche se il lock interno viene rilasciato subito (`1D0084A9 call ds:LeaveCriticalSection`); Storm nel join legge indirizzo e campi inline, non acquisisce quei puntatori.

## 3. Wire format e cadenza

### Socket e indirizzo di destinazione

`sub_1D009C70` crea due socket `socket(2,2,17)` (`snp:1D009D39`–`1D009D54`), con bind su `0.0.0.0:6112` (`1D009E0F`–`1D009E1D`) e `0.0.0.0:6111` (`1D009E49`–`1D009E6D`). L'indirizzo globale per discovery è `255.255.255.255:6111` (`1D009DAB push 17DFh`, `1D009DB5 mov dword_1D0179E0,0FFFFFFFFh`, `1D009DC6 call dword ptr [ecx+8]`, `1D009DDD mov word ptr dword_1D0179DC+2,ax`). I `setsockopt` sono `SOL_SOCKET/SO_BROADCAST=1` (`1D009E8A/E8C/E99` e `1D009EAD/EAF/EB5`); il `push 4` è **optlen**, non `SO_REUSEADDR`. Game send usa il socket `1D017A04` (6112) a `1D00827D mov edx,dword_1D017A04` / `1D008289 call dword ptr [eax+10h]`.

### Discovery: datagramma, non record in memoria

| Offset | Formato | Evidenza `snp` |
|---:|---|---|
| `0x00` | 2 byte checksum, non un magic fisso | `1D008F9D call sub_1D008BD0` / `1D008FA2 mov [esi],ax`; il ricevitore a `1D009A7B call sub_1D00F060` / `1D009A80 test ax,ax` esige resto zero sull'intero datagramma. `1D00F074`–`1D00F095` mostra somme tipo Fletcher a modulo 255, scorrendo all'indietro. |
| `0x02` | u16 LE, lunghezza totale | Probe fissa 20 (`1D0093E0 mov word ptr [ebp+var_14+2],14h`); annuncio `22+strlen(name)+strlen(statstring)+blob_len` (`1D008F14 lea eax,[esi+ebx+16h]`, `1D008F18 mov [edx+2],ax`). `1D009A5E cmp esi,14h` e `1D009A69`–`1D009A72` richiedono almeno 20 byte e lunghezza dichiarata uguale a quella ricevuta. |
| `0x04` | u16 LE tipo: `0` annuncio/risposta, `1` ritiro, `2` probe | `1D008F1E mov [edx+4],ax` con AX=0, `1D008DAC mov word ptr [esi+4],1`, `1D0093E6 mov word ptr [ebp+var_10],2`; dispatch `1D009A85`–`1D009ABC`. |
| `0x06` | u16 LE, zero nei tre writer `[?]` | `1D008F22 mov [edx+6],ax`, zero iniziale del probe a `1D0093C7`, ritiro riusa l'annuncio. Non è letta dal dispatch. |
| `0x08` | u32 LE product ID | `1D008F2B mov [edx+8],ecx`; `1D008A96 cmp eax,dword_1D017A0C` è il filtro per inserire una voce. |
| `0x0C` | u32 LE versione | `1D008F34 mov [edx+0Ch],eax`; salvata nel nodo senza test nel discovery, confrontata da Storm al join. |
| `0x10` | u32 LE flags (zero nel probe) | `1D008F37 mov [edx+10h],ecx` (argomento 4 in advertising); `1D0093D0 mov [ebp+var_4],eax` (zero). |
| `0x14…` | Solo annuncio/ritiro: `name\0`, `statstring\0`, poi blob fino alla lunghezza totale | `1D008F31 lea esi,[edx+14h]`, `1D008F44`/`1D008F67 call storm_501`, `1D008F59 lea edi,[esi+eax+1]`, `1D008F84 lea edi,[eax+edi+1]`, `1D008F88 rep movsd`, `1D008F8F rep movsb`. Nessun puntatore sul filo. |

Product/versione derivano dal client, non da un header UDP fisso: `snp:1D009C7C mov ecx,[eax+0Ch]` / `1D009C82 mov dword_1D017A0C,ecx` e `1D009C7F mov edx,[eax+10h]` / `1D009C91 mov dword_1D017A1C,edx`. L'EXE analizzato imposta `0x53544152` (StarCraft), `0x53455850` (Brood War) e versione `0xD3` (`004727A2`, `00472792`, `004727BF`, tutti `mov dword ptr [esi+offset],imm`); passa il record a `004D3D42 call storm_117`, da Storm al provider a `1503E505 call dword ptr [edx+1Ch]`.

- **Probe/risposta.** `snp:1D009451 call eax` invia il probe di 20 byte in broadcast. Il ricevitore, sul tipo 2, passa il `sockaddr_in` sorgente a `1D009AA0 call sub_1D008780` e, se c'è un annuncio attivo, risponde **unicast al mittente** (`1D0087A5 test esi,esi`, `1D0087B2 push edx`, `1D0087C1 call ecx`). Usa il datagramma tipo 0 conservato; non c'è un tipo distinto per la risposta. Il ramo del probe non confronta product/versione. Il tipo 1 rimuove la voce senza reinserirla (`1D009AA9 cmp cx,1`, `1D009AAD setz dl`, `1D008AA5 test eax,eax`, `1D008AA7 jnz …`).
- **Cadenza.** Un probe all'avvio del thread discovery (`1D0099E3 call sub_1D0093B0`); nel dialogo il timer è **500 ms** (`1D0095D9 push 1F4h`, `1D0095E1 call ds:SetTimer`) e `WM_TIMER` o `0x400` invocano il probe (`1D00996F`–`1D00997D`). Nell'enumerazione via Storm, Unlock restituisce **500 ms** al chiamante quando fornisce un indirizzo, ed emette un probe solo con `GetTickCount()-lastProbe > 400 ms` (`1D009BAE mov dword ptr [eax],1F4h`, `1D009BC4 cmp ecx,190h`, `1D009BCA jbe …`, `1D009BD1 call sub_1D0093B0`). Non è un timer periodico dell'host: il buffer tipo 0 viene trasmesso alla callback `+0x40` (`1D008FED call ecx`) e come risposta ai probe. Gli xref al buffer `1D0179D4` sono nei percorsi `1D008780`, `1D008D80`, `1D008E70`, non in un beacon autonomo.
- **TTL e chiave.** Lock (`1D00904A push 2710h`) e dialogo (`1D009982 push 2710h`) invocano il purge: rimozione quando `now-tick > 10000 ms`, confronto unsigned (`1D008719`–`1D008725`, `jbe` conserva la voce). Non è uno scadenzario eseguito esattamente allo scoccare dei 10 s. Su ogni annuncio la chiave è l'intero `sockaddr_in` con porta normalizzata a 6112 (`1D0089B6`, `1D0089E2 mov ecx,4`, `1D0089E7 lea esi,[ebx+0Ch]`, `1D0089EC repe cmpsd`); ID riutilizzato sulla sostituzione, tick rinnovato. A indirizzo normalizzato uguale non corrispondono due nodi distinti.
- **Due istanze sullo stesso host.** Stesso wildcard e stesse due porte; nessun fallback per seconda istanza. I bind precedono la configurazione del broadcast (`1D009E1D` e `1D009E6D`, test e salto al fallimento `1D009F7D call sub_1D009070`, seguito da `SetLastError(0x4C6)`). Nessuna richiesta di riuso da questo provider prima dei bind. Non affidarsi a una seconda istanza sul medesimo stack con le porte già occupate; se l'OS rifiuta il bind, l'inizializzazione fallisce. `[?]` Nessuna verifica dinamica della consegna dei broadcast a sé o del comportamento di particolari stack, namespace o policy di bind.
- **Stop e shutdown non sono lo stesso datagramma.** `+0x44` cambia tipo a 1, ricalcola il checksum, invia il pacchetto della stessa lunghezza e libera l'annuncio (`1D008DAC`, `1D008DB2`, `1D008E11`, `1D008E42`). Il teardown usa anche un datagramma separato da 8 byte `08 EF 08 00 00 00 00 00` (`1D0090B9`–`1D0090D5`), broadcast a 6111 (`1D009115`) e due volte a 6112 per iterazione (`1D009140 mov esi,2`, `1D009164 call dword ptr [eax+10h]`, `1D009168 jnz …`), in un ciclo limitato; il normale ricevitore discovery non lo tratta come annuncio/ritiro (`1D009A5E cmp esi,14h`).

## 4. Trigger e dati dell'annuncio: `+0x40`, non `+0x50`

`exe:004D3ABB call sub_472300` genera la statstring in un buffer da 128, poi `004D3B0B call storm_138` avvia la creazione; `004D3AF6 push 20h` è la lunghezza del dato applicativo passato a Storm (non la lunghezza del blob di discovery). `storm_138` copia nome, descrizione/statstring e seconda stringa opzionale (ramo che imposta il flag bit 0 quando non vuota) a `1503AF96`, `1503AFA6`, `1503AFB6` (`call storm_501`). Inizializza il tick host a `1503AFBB call ds:GetTickCount`, poi `1503B0E1 call sub_15037B90` e usa l'esito (test a `1503B0E6`). Gli altri xref a `sub_15037B90` sono su aggiornamento flags/stato (`1503A643`, `1503BC18`, `1503C07D`), non un heartbeat temporizzato.

| # | Argomento del vero advertising (`storm:15037BF8`), nell'ordine C | Effetto in `snp:1D008E70` |
|---:|---|---|
| 1 | `&byte_1505BC98`, nome (`15037BF3 push offset …`) | Copia NUL-terminata a wire `+0x14`, poi nodo `+0x28`. |
| 2 | `&byte_1505BD18`, stringa opzionale (`15037BEE push offset …`) | Non letta da questa callback UDP; Storm usa la sua non-vacuità per il flag 1 (`1503AFD9`–`1503AFDE`). |
| 3 | `&byte_1505BC10`, statstring (`15037BE9 push offset …`) | Seconda stringa sul filo, poi nodo `+0xA8`. |
| 4 | flags globale (`15037BE8 push ecx`) | wire `+0x10`, nodo `+0x04`. |
| 5 | `(GetTickCount()-hostStart)/1000` (`15037BA9`–`15037BE1`, `mul` e `shr edx,6`) | Non letto da UDP; non confondere con tick di ricezione del nodo. |
| 6–8 | tre DWORD Storm (`15037BDD`, `15037BD6`, `15037BD0`) | Non letti da UDP. |
| 9–10 | `blob`, `blob_len` (`15037BC9 push ecx`, `15037BC1 push eax`) | Copia dei byte, non del puntatore (`1D008F78 mov esi,[ebp+arg_20]`, `1D008F07 mov ebx,[ebp+arg_24]`); nel nodo ricevente diventano `+0x12C/+0x130`. |

Nel percorso osservato l'argomento 9 non è il blocco applicativo da 32 byte passato dall'EXE a `storm_138`: `sub_150378B0` costruisce un blob Storm con **tre DWORD** — size `12`, contatore giocatori corrente, massimo (`150378B9 mov dword ptr [edi],0Ch`, `15037918 mov dword ptr [esi],1`, `15037933 inc dword ptr [esi]`, `15037953 mov [edi+8],eax`, `15037956 mov [edx],ecx`). `storm_133` espone gli ultimi due (`15039232 mov edx,[eax+4]`, `15039238 mov eax,[eax+8]`). In UDP, l'annuncio modifica il buffer locale `dword_1D0179D4` e invia; è il *ricevitore* a popolare il nodo (`snp:1D008B86 call storm_401`, `1D008BAA mov dword_1D0179EC,eax`). Non c'è push di un nodo locale da `1D008E70`.

## 5. Tabella finale: slot → arità → fonte → output

Le etichette della domanda sono conservate dove possibile; per le due entry nulle non definiscono il comportamento del provider UDP.

| Slot | Ar.Tot. e cleanup SNP | Call-site Storm e numero di `push` di argomento | Output atteso / semantica |
|---|---|---|---|
| `+0x28` `spiLockGameList` | 3; `1D009030` → `1D00906D retn 0Ch` | `15038A88 call dword ptr [eax+28h]`: **3** a `15038A7A/A80/A81` | `(arg1,arg2,&head)`; primi due non letti da UDP, `head` scritto a `1D009064 mov [esi],eax` con CS trattenuta. BOOL in EAX testato a `15038A8B`; successo anche se lista vuota. |
| `+0x4C` `spiUnlockGameList` | 2; `1D009B80` → `1D009BDC retn 8` | `15038B62 call dword ptr [ecx+4Ch]`: **2** a `15038B60/B61` | `(headOriginale,pollMsOut?)`; compara la testa (`1D009B86 cmp eax,dword_1D0179EC`), rilascia CS se coincide, poi scrive 500 se puntatore non nullo (`1D009BAE`). Storm ignora EAX (`15038B65 mov eax,1`). |
| `+0x34` `spiSelectGame` | 6; `1D009BE0` → `1D009C5D/C65 retn 18h` | `15039044 mov edi,[ecx+34h]`, `1503906A call edi`: **6** a `15039052/56/5D/64/68/69` | Selezione via dialogo, non lookup per ID. In: primo dword inoltrato, quattro strutture contestuali temporanee preparate da Storm, ultimo `out` azzerato a `15038FCC`. SNP impacchetta i sei parametri a `1D009BE6`–`1D009C1D` e apre il dialogo (`1D009C47 call storm_208`); il quarto fornisce HWND a `1D009C22 mov eax,[eax+8]`. Conferma UDP chiama `storm_118` con l'ultimo puntatore a `1D00983E`, per l'ID giocatore scritto da Storm. Storm `1503906C pop edi` preserva il BOOL ritornato (dialog result diverso da 0 e -1; `1D009C4C`–`1D009C55`). Nessuna ownership di nodo trasferita. |
| `+0x14` `spiGetGameInfo` | 4; `1D008420` → `retn 10h` a `1D0084C0`, `1D0084D3`, `1D0084E3` | `1503CC83 call dword ptr [edx+14h]`: **4** a `1503CC6D/7A/81/82` | `(game_id,name,arg3,out316)`; `name` e `out316` non nulli, almeno un filtro attivo, se entrambi attivi devono coincidere; confronto nome case-insensitive. Arg3 non letto dall'UDP. Azzeramento/copia shallow in buffer Storm, BOOL in EAX (`1503CC86 test eax,eax`); niente allocazione per il chiamante. |
| `+0x50` (nel piano `spiStartAdvertisingGame`) | **Nessun target UDP**; DWORD zero a `1D016E98`. Nessun `ret N` SNP. | `1503AF1E mov eax,[eax+50h]` → `1503AF3A call eax` (**4** `push` a `1503AF25/2D/2E/39`). Anche `1503CBD2 mov eax,[eax+50h]` → `1503CBF1 call eax` (**4** `push` a `1503CBD9/CBE4/CBE5/CBF0`). | Solo in assenza del primo identificativo stringa locale e se la callback esiste. `buf1,128,buf2,128`: **buffer di output sullo stack Storm**; si inizializza il primo byte di entrambi, poi si usano le stringhe risultanti e si esige la prima non vuota (`1503AF48 cmp byte ptr [ebx],0`; analogo join). EAX non consumato; nessuna ownership trasferita. Non è l'entry advertising. |
| `+0x44` `spiStopAdvertisingGame` | 0; `1D008D80` → `1D008E64 retn` | `1503AAE8 call dword ptr [eax+44h]`: **0** `push` di argomento | Storm lo invoca se il ruolo host è attivo (`1503AAD9 mov eax,[edi+200h]`, `1503AADF test eax,eax`). Nessun buffer, ignora EAX (`1503AAEB mov eax,dword_1505AD7C`). SNP invia ritiro se attivo, libera il buffer, ritorna 1. |
| `+0x58` `spiCheckDataFile` | **Nessun target UDP**; DWORD zero a `1D016EA0`. Nessun `ret N` SNP. | `150377E9 mov eax,[eax+58h]` → `150377F7 call eax`: **4** `push` a `150377F3/F4/F5/F6` (se non NULL) | `(stringaNonVuota,arg2NonZero,arg3,&out)`; Storm preazzera il DWORD a `1503776D mov dword ptr [esi],0`, ignora EAX della callback e restituisce `*out != 0` (`15037804 mov ecx,[esi]`, `1503780C setnz al`). Nessuna evidenza di trasferimento di ownership. In UDP ritorna falso senza chiamarla. |
| `+0x40` advertising effettivo | 10; `1D008E70` → `1D009019 retn 28h` | `15037BF8 call dword ptr [edx+40h]`: **10** `push` a `15037BC1/C9/D0/D6/DD/BE1/BE8/BE9/BEE/BF3` | In: vedi §4. BOOL usato da Storm (`1503B0E6`). UDP copia le stringhe e il blob nel proprio buffer di annuncio da `0x214` byte (`1D008EAB push 214h`, `1D008EB0 call storm_401`); successo non equivale a conferma di ricezione. |

## NON PROVATO

- `[?]` Nomi SDK esatti/semantica generale dei dword `node+0x08`, `+0x1C`, `+0x24`. Provati i loro valori UDP e l'esposizione del `+0x24` (`1D008ABD`, `1D008AF1`, `15039222`); manca un utilizzatore che dimostri che `+0x24` sia denominato `elapsed_time`. `+0x20` è provato come tick locale usato per il TTL, non come `creation_time` remoto. Non inferire nomi canonici da un valore costante.
- `[?]` Semantica completa dei parametri opzionali di `+0x34`, del terzo argomento di `+0x14`, dei primi due di `+0x28`, e dei quattro di `+0x58` oltre ai controlli/funzioni documentati. Servirebbe un consumer/produttore che li interpreti; il provider UDP non li interpreta tutti o ha slot nullo. Per `+0x50/+0x58` quattro DWORD sul lato Storm **non** sono una misura di `ret 0x10` sul lato SNP; anche le altre quattro tabelle di `SnpBind` in **questo** binario hanno zero per entrambi (dump `snp-slots.txt`). Nessun altro `.snp` caricato.
- `[?]` Significato del campo wire `+0x06` oltre al valore zero osservato e della gestione lato gioco del datagramma di teardown da 8 byte; non ricostruito il protocollo gameplay. La cadenza riportata è quella dei trigger statici; jitter, ricezione broadcast, coesistenza reale e interoperabilità tra due processi non misurati.

## Decisioni autonome

- Separati ABI UDP, snapshot di Storm e rappresentazione wire; nessun cambio alle sorgenti o alle build. Nessun uso di Git, desktop, test grafici o avvio del gioco. Nessun intervento sui file o sulle sessioni degli altri lavori.
- Trattati `+0x50` e `+0x58` come assenti quando si riproduce questo provider; non dedotte implementazioni fittizie da nomi del piano o da prototipi decompilati.
