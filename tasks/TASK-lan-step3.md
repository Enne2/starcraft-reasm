# TASK LAN-3 — `spiSelectGame` + partita melee end-to-end ≥60 s fra due istanze

Repo: `/home/enne2/dev/starcraft-reasm-generic`. Non fare git. Test solo sandbox Xvfb, background, audio dummy.

## Ingressi
- `reports/ANALYSIS-snp-contracts.md` (contratti IDA) e `reports/REPORT-lan-step2b.md` (GameList/advertising
  funzionanti: due istanze si vedono in lista).
- `runtime/shim-port/elf/snp_elf.c` con `spiSelectGame` **ancora fail-closed**.

## Obiettivo
1. **Arità e semantica di `spiSelectGame`** (+0x34): prova in IDA il call-site di Storm (indirizzo, numero di
   `push`, `ret N` del provider) e cosa Storm si aspetta in uscita (indirizzo selezionato? flags? player info?).
   Se il contratto resta ambiguo, **dichiara** il residuo e implementa solo la parte provata.
2. **Implementazione**: selezione della partita annunciata (join lato client) coerente col wire format provato;
   l'host deve accettare il join e avviare la lobby.
3. **Gate end-to-end (criterio di accettazione)** — due istanze `starcraft-x86` (`lan-a` host su 127.0.0.1,
   `lan-b` client su 127.0.0.2):
   - host crea partita melee 2 giocatori; client la vede, la **seleziona** ed entra;
   - si avvia la partita: entrambe le istanze sono in gioco (HUD + unità) e avanzano per **≥60 s** di tempo reale;
   - nessuno stall >2 s, nessun disconnessione/errore; se possibile misura il **turno/tic** raggiunto da
     entrambe le parti e confrontalo (deriva massima dichiarata).
   Verifica numerica/log: righe `[snp]` con `SC_SNP_TRACE=1`, contatori di turno dal gioco, statistiche immagine
   (`tools/shot-stat.sh`) o OCR mirato. Niente immagini nel contesto.

## File di tua proprietà (esclusivi)
- `runtime/shim-port/elf/snp_elf.c`, `runtime/shim-port/elf/snp_elf.h`
- `tests/x86_snp_lan_test.c`
- `build/qa/lan-3/`
- `reports/REPORT-lan-step3.md`

**NON toccare**: `host_elf.c` (file di un'altra sessione attiva), `win32_shim.c`, `com_*.c`, `bridge_shims.def`,
`build/starcraft-*`, `build/obj-x86/`.

## Vincoli
- D19: niente ABI inventate. Ordine degli slot immutabile.
- Il desync è un rischio reale: se le due istanze divergono, **documenta** il punto (turno, checksum) invece di
  mascherare con timeout.
- Pulizia display Xvfb orfani; mai `:0`; nessun input sul desktop dell'utente (solo `tools/scctl` sulle tue sandbox).
- Build x86 e ARM; errori in file non tuoi → riprova una volta e segnala.
- Report con: prove IDA, comandi, output sintetico del gate (turni, deriva, durata), limiti.
