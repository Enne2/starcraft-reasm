# TASK LAN-2A — Oracolo IDA su `standard.snp`: contratti SNETADDR / SnpGame / GAMELIST / beacon

Repo: `/home/enne2/dev/starcraft-reasm-generic`. **Solo analisi: NON modificare sorgenti, NON fare git.**
Deliverable unico: `reports/ANALYSIS-snp-contracts.md` (+ eventuali dump in `build/qa/lan-2a/`).

## Regola primaria (oracolo)
IDA Pro sul binario originale è l'oracolo assoluto. **Prima** di scrivere qualunque affermazione: apri il binario,
decompila la funzione, cita **indirizzo + istruzione**. Nessuna deduzione "a naso". Ciò che non riesci a provare va
marcato `[?]` con la spiegazione di cosa manca.

## Binari da analizzare
- `build/asset-sandbox/standard.snp` — provider SNP UDP nativo di Blizzard (PE32 i386). **Copia** in una dir
  privata (`/tmp/sc-ida-lan2a/`) e apri la copia, così non tocchi nulla di condiviso.
- `build/lift-full/storm.orig.dll` (image base `0x15000000`) e l'eventuale `build/lift-full/StarCraft.orig.exe`:
  servono per i **call-site** di Storm (chi chiama gli slot e con quanti argomenti).
- Non toccare altre sessioni IDA (usa `ida_list` prima, chiudi solo la tua).

## Domande a cui il report deve rispondere con prove
1. **`SNETADDR`** (16 byte secondo il piano): layout esatto campo per campo. È `sockaddr_in` POSIX-like
   (`family` u16 little-endian = 2, `port` **big-endian**, `addr[4]`, padding) o un formato Blizzard diverso?
   Cita la funzione in `standard.snp` che lo riempie/legge e i riferimenti a `htons`/`ntohs` (o al loro inlining).
2. **`SnpGame` / `GAMELIST`**: offset di `next`, `game_id`, `name`/`statstring` (puntatori o buffer inline?),
   `host_address`, `elapsed_time`, `creation_time`, dimensione del nodo, chi alloca e chi libera.
   Provalo dal lato **consumatore** (Storm che legge la lista) e dal lato **produttore** (`standard.snp` che la compila).
3. **Arità `stdcall` esatte** (quanti byte di `ret N`) e semantica di ingresso/uscita per:
   `spiLockGameList` (+0x28), `spiUnlockGameList` (+0x4c), `spiSelectGame` (+0x34), `spiGetGameInfo` (+0x14),
   `spiStartAdvertisingGame` (+0x50), `spiStopAdvertisingGame` (+0x44), `spiCheckDataFile` (+0x58).
   Per ognuna: indirizzo del call-site in Storm, numero di `push` prima della `call`, `ret N` nella funzione di
   `standard.snp`, e cosa Storm si aspetta in output (puntatore? flag? ownership del buffer?).
4. **Discovery/advertising wire format** in `standard.snp`: porta UDP, indirizzo di broadcast, magic/versione del
   pacchetto, campi e loro ordine, probe vs response, intervallo di beacon e TTL della voce in lista, comportamento
   quando due istanze stanno sullo stesso host.
5. **Trigger e dati di `spiStartAdvertisingGame`**: cosa passa Storm (il piano dice 4 argomenti con due buffer da
   128 byte e due lunghezze) e quali campi finiscono nella voce di lista.

## Come lavorare
- `ida_open` su copia privata, poi `ida_list(kind=funzioni)`, `ida_search` per stringhe/porta `6112` (0x17E6),
  xref alla IAT di `WSOCK32`/`ws2_32` dentro `standard.snp`, `ida_decompile` delle funzioni di interesse.
- Per `storm.orig.dll` lavora sui call-site: `ida_xrefs` sulle entry della tabella NetFunctions, `ida_disasm`
  attorno alle `call dword ptr [reg+offset]`.
- Archivia i disassemblati usati in `build/qa/lan-2a/*.txt` e citane il percorso nel report.
- Il report deve contenere una **tabella finale** "slot → arità → fonte (indirizzo) → output atteso" e una sezione
  "NON PROVATO" onesta.

## Vincoli
- Nessuna modifica a codice, binari, `build/starcraft-*`, `build/obj-*`. Solo report e `build/qa/lan-2a/`.
- Nessun test grafico, nessun input desktop, nessun `git`.
- Chiudi la tua sessione IDA alla fine (`ida_close`) e dichiara gli SHA-256 dei binari analizzati.
