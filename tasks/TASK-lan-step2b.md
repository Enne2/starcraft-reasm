# TASK LAN-2B — GameList + advertising LAN reali (gate: due istanze si vedono in lista)

Repo: `/home/enne2/dev/starcraft-reasm-generic`. Non fare git. Test solo in sandbox Xvfb, background, audio dummy.

## Ingressi obbligatori (leggili prima di scrivere codice)
- `reports/ANALYSIS-snp-contracts.md` — contratti provati in IDA su `standard.snp`/`storm`: layout
  `SNETADDR`, `SnpGame`/`GAMELIST`, arità `stdcall` e wire format di beacon/probe. **È la tua specifica.**
  Ciò che lì è marcato `[?]` resta **non implementato** (fail-closed con log), non indovinato.
- `runtime/shim-port/elf/snp_elf.c` — provider attuale (UDP, 27 slot, advertising solo locale,
  `snp_peers_*` e event wakeup arrivano dal task LAN-1, già integrato dal supervisore).
- `PLAN-MULTIPLAYER.md` §2 per il contesto e §3 per i rischi di timing.

## Obiettivo
1. **`SNETADDR`**: se il formato reale differisce dai 16 byte `sockaddr_in` usati oggi, introduci una
   conversione esplicita (`snetaddr -> sockaddr_in` e ritorno) usata da `spiSend`, `spiReceive`, advertising
   e GameList. Documenta nel codice **l'indirizzo IDA** che giustifica ogni campo.
2. **Advertising**: `spiStartAdvertisingGame` pubblica la voce (beacon periodico e/o risposta a probe, come
   provato dal wire format) con porta/indirizzo del socket provider; `spiStopAdvertisingGame` la ritira.
   Rispetta TTL e intervalli misurati.
3. **GameList**: `spiLockGameList` compila la lista dei giochi visti (locale + annunci remoti), con TTL;
   `spiUnlockGameList` libera le allocazioni. Nessuna struct inventata: usa gli offset provati.
4. **Gate end-to-end (il criterio di accettazione)**: due istanze `starcraft-x86` in sandbox separate
   (`lan-a`, `lan-b`), **127.0.0.1** e **127.0.0.2**, porta UDP provata (6112 se il documento lo conferma):
   - `lan-a` crea una partita (Multiplayer → Original → Create Game) con nome noto;
   - `lan-b` apre la lista partite e **deve vedere** quella partita;
   - prova anche il ritiro (`Stop advertising`/uscita dalla lobby) e verifica che sparisca dopo il TTL.
   Verifica **numerica/log**: righe del provider (`SC_SNP_TRACE=1`) + OCR dell'area lista + statistiche
   immagine (`tools/shot-stat.sh`); nessuna immagine da leggere nel contesto.

## File di tua proprietà (esclusivi)
- `runtime/shim-port/elf/snp_elf.c`, `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c` (estendi: discovery in-process fra due provider)
- `build/qa/lan-2b/`
- `reports/REPORT-lan-step2b.md`

**NON toccare**: `host_elf.c` (file di un'altra sessione attiva), `win32_shim.c`, `com_*.c`,
`bridge_shims.def`, `build/starcraft-*`, `build/obj-x86/`.

## Vincoli
- D19 fail-closed; ordine degli slot immutabile; `spiSelectGame` resta com'è (è del task LAN-3).
- Se due istanze sullo stesso host non possono usare la stessa porta, usa la variante documentata
  (porte distinte o IP distinti) e **dichiara** perché.
- Pulizia display Xvfb orfani come da nota d'ambiente del repo; mai `:0`.
- Build x86 e ARM (comando in `tasks/TASK-lan-step1.md`); se la build fallisce per errori in file
  non tuoi, riprova una volta e segnala, senza modificare codice altrui.
- Report con: cosa è stato implementato, offset IDA citati, comandi, output sintetico del gate,
  cosa resta aperto.

---

## AGGIORNAMENTO OBBLIGATORIO (post-analisi IDA, leggi prima di iniziare)

`reports/ANALYSIS-snp-contracts.md` è stato completato e **cambia alcune assunzioni del piano**.
Verifica nel codice attuale e allinea la tabella degli slot **prima** di implementare:

| slot | vero contratto (provato) | nostro codice oggi | azione richiesta |
|---|---|---|---|
| `+0x40` | **advertising vero**, **10 argomenti** (`ret 0x28`): `name, optstr, statstring, flags, elapsed, d1,d2,d3, blob, blob_len` | `snp_start_advertising_ladder`, 10 argomenti (aritá OK), non fa nulla | implementare la semantica: comporre il datagramma tipo 0 e inviarlo |
| `+0x44` | stop advertising, **0 argomenti**; invia datagramma **tipo 1** e libera l'annuncio | `snp_stop_advertising(void)` | implementare invio tipo 1 |
| `+0x50` | callback **diversa** dall'advertising: 4 argomenti `(buf1, len1, buf2, len2)` con **buffer di output** da 128 | `snp_start_advertising` legge `name`/`password` come stringhe (usi un buffer di output come input) | **non** usarla per l'advertising; fail-closed + log, senza leggere i buffer come stringhe |
| `+0x28` | `(arg1, arg2, &head)`, lock CS, `head` anche NULL | ok aritá, lista vuota | implementare la lista |
| `+0x4C` | `(head, u32 *out)`; **scrive 500** se `head != NULL` | ok aritá | scrivere il poll hint 500 |
| `+0x34` | **6 argomenti** (`ret 0x18`) | `snp_unknown_select_game(void)` → **mismatch: 24 byte di stack non ripuliti** | correggere l'aritá a 6 subito (resta fail-closed, la semantica è del task LAN-3) |
| `+0x14` | `(game_id, name, arg3, out316)` | 4 argomenti, negata | lasciare negata |
| `+0x58` | `(str, u32, u32, u32 *out)`, ritorna void con `*out=0` | ok | lasciare |

Wire format provato (implementa esattamente questo, citando gli indirizzi nel codice):
- **due socket**: discovery su **UDP 6111**, gioco su **UDP 6112** (bind specifico su `SC_LAN_BIND`, non wildcard, così due istanze su 127.0.0.1/127.0.0.2 convivono);
- datagramma: `+0x00` checksum 2 byte (somma mod 255 tipo Fletcher, scorrendo all'indietro, resto zero valido),
  `+0x02` u16 LE lunghezza totale, `+0x04` u16 LE tipo (`0` annuncio/risposta, `1` ritiro, `2` probe 20 byte),
  `+0x06` u16 zero, `+0x08` u32 product, `+0x0C` u32 versione, `+0x10` u32 flags,
  `+0x14` `name\0 statstring\0 blob` (solo tipi 0/1);
- probe inviato a `255.255.255.255:6111`; il ricevitore risponde **unicast** con il tipo 0 conservato;
- TTL voce: **10000 ms** dal tick di ricezione; purge a Lock; chiave = `sockaddr_in` con porta normalizzata a 6112;
- nodo `SnpGame` = **0x13C byte** con gli offset provati (`+0x0C` host_address[16] inline, `+0x20` tick,
  `+0x28` name[128], `+0xA8` statstring[128], `+0x128` next, `+0x12C` blob, `+0x130` blob len, `+0x134` product,
  `+0x138` version); ownership: alloc/free tutti del provider, Lock restituisce un prestito della testa.
