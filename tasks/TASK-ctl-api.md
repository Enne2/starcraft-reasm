# Missione: API di automazione DENTRO lo shim (screenshot framebuffer + input), auto-attiva

## Perche'
L'harness esterno (`~/pbst/pbst`) e' fragile: dipende da `SC_CONTROL_FILE` letto nel pump, da
`gnome-screenshot` (grab Xwayland inaffidabile) e da coordinate stimate a schermo. Serve un canale
**in-process** che parli direttamente con il runtime: screenshot dal **canvas 640x480** (non dal desktop)
e input iniettato nella coda eventi del gioco.

## Obiettivo (deliverable funzionante)
Nuovo modulo nel nostro shim, compilato sia x86 sia ARM:

`runtime/shim-port/elf/sc_ctl.c` + `sc_ctl.h`
- **Auto-attivo**: all'avvio, se `SC_CTL` non vale `0`, apre un server su **socket Unix**
  `SC_CTL_SOCK` (default `./sc-ctl.sock`, cioe' nella CWD del gioco) e, se `SC_CTL_PORT` e' impostata,
  anche su **TCP 127.0.0.1:<porta>`. Se il socket non si puo' creare: log e prosegui (mai bloccare il gioco).
- **Protocollo testuale** (una riga per comando, risposta una riga `ok ...` / `err ...`):
  - `shot <path>` → scrive **PNG 640x480 RGB** dal canvas 8bpp + palette corrente
    (niente librerie: encoder PNG minimale con zlib "stored", ~60 righe, portabile).
    La risposta e' `ok shot <path> <bytes>`.
  - `click <x> <y> [btn]` con btn `1`=sinistro (default), `2`=destro → MOVE+DOWN+UP nella coda eventi.
  - `move <x> <y>`, `down <x> <y> [btn]`, `up <x> <y> [btn]`
  - `key <nome|numero>` (`esc|enter|space|tab|backspace|left|right|up|down|f1..f12` o SDL keycode),
    `keydown <...>`, `keyup <...>`, `text <stringa>`
  - `state` → `ok state frame=<n> w=640 h=480 presented=<n> pal=<0|1>`
  - `quit` → chiude la connessione (non il gioco)
- **Thread-safety**: l'input usa la stessa via di `host_elf_test_event`; gli **screenshot NON si leggono
  dal thread del server**: si imposta una richiesta e il **thread del gioco** copia canvas+palette in un
  buffer in `host_elf_present()` (o nel punto in cui il canvas e' stabile), poi il server scrive il file.
  Niente tearing, niente lock presi dal thread di gioco oltre a un flag atomico.
- Client di comodo: `tools/scctl` (python3, nessuna dipendenza) con sottocomandi
  `shot`, `click`, `move`, `key`, `text`, `state`, `wait`, `seq` (script di piu' righe),
  che trovano il socket (argomento o `SC_CTL_SOCK`, default `./sc-ctl.sock`).

## Vincoli
- C11 portabile, CPU-agnostico (x86 e ARM), **nessuna dipendenza esterna** (no libpng, no X11, no SDL extra).
- Toccare solo: `runtime/shim-port/elf/sc_ctl.{c,h}` (nuovi), i due `tools/build-*-{x86,armv7}.sh` per
  includere il TU, un hook minimo in `runtime/shim-port/elf/host_elf.c` (init + snapshot request),
  `tools/scctl` (nuovo), test in `tests/` se serve.
- **Non** rompere nulla: il gioco deve continuare a funzionare identico senza client collegati
  (`SC_CTL=0` lo disattiva del tutto).
- Il working tree puo' contenere modifiche altrui: niente `revert`/`stash`.

## Verifiche richieste
1. `./tools/build.sh --arch x86 --skip-entry` PASS; compilazione i686 e x86-64 del TU con
   `-std=c11 -Wall -Wextra` PASS.
2. Test locale x86 in Xvfb: avvio, `scctl state`, `scctl shot /tmp/a.png` → il PNG deve essere
   **640x480** e contenere il menu (verificabile con `python3 -c "..."` sulle dimensioni e su un
   campione di pixel non neri); `click` su "Single Player" deve produrre un cambio di schermata
   (screenshot successivo diverso).
3. Build ARM (`SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`) PASS, deploy su Pinebook
   (`enne2@10.0.1.126`, vedi `reports/REPORT-pbp-armv7.md`) e prova reale: `scctl shot` dal Pinebook
   deve produrre un PNG 640x480 del **canvas del gioco** (non del desktop).
4. Riporta i tempi: latenza `shot` (ms) e dimensione PNG.

## Poi: usare l'API per arrivare in partita
Con l'API, sul Pinebook: dalla schermata di scelta campagna, **raggiungere una partita** (campagna Terran
o, se la campagna resta bloccata, `Play Custom` con una mappa) e **dimostrarlo con screenshot 640x480**
salvati in `build/qa/pbp-test/api-*.png`. Se la campagna resta bloccata, documenta con l'API la sequenza
esatta di schermate e il messaggio d'errore (serve a chiudere il bug `standard.snp`).

Deliverable: `reports/REPORT-scctl-api.md` + `reports/STATE-scctl-api.md` con comandi, misure e screenshot.
