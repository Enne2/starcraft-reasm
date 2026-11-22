# REPORT-arm-evt — diagnosi MPQ/eventi ARM

Stato: **BLOCCATO — gate di riproduzione non raggiunto; nessun fix candidato valido**.

## Perimetro e decisioni autonome
Ho limitato le modifiche ai due file assegnati e a questo report; script, `dlg_elf.c`, `runtime/arm/patches/`, guest, git e repo originali non sono stati modificati. Decisione conservativa: non promuovere un binario se il test non arriva a misurare eventi/contenuti e non dichiarare risolto l'errore sulla sola base di una modifica locale.

## Evidenza
- `win32_shim.c` conferma il pool fisso: `g_ev[16]`; `CreateEventA` restituisce `(void *)1` a esaurimento; `CloseHandle` marca `used=0` senza refcount. `WaitForSingleObject` su handle non riconosciuto fallisce, quindi l'effetto esatto del sentinel va misurato nel percorso effettivo.
- `mpq_diag.c` nel tree assegnato contiene già un bridge alternativo con eventi allocati dinamicamente (`calloc`), refcount, attese pthread e hook IAT per `CreateEventA/SetEvent/ResetEvent/WaitForSingleObject/CloseHandle`. Non è una prova che gli hook siano installati nel run.
- Il log preesistente `build/qa/run-last.log` (20/11 17:24) e la ripetizione sul candidato `build/starcraft-armv6-local` mostrano `Data File Error`, template `0x6A`, testo non leggibile dal logger, poi `STOP` del dispatcher su `0x00427714`; il run locale termina per timeout 124. Non mostra il nome `Effect.pcx`, il codice 0x26 né eventi/letture MPQ.
- Ho effettuato una strumentazione temporanea del pool nel sorgente e linkato `build/starcraft-armv6-evtmeas`; il run è terminato con exit 9 e SIGSEGV in storm a `0x15040E07`, prima del template 0x6A. Il contatore non ha quindi prodotto evidenza utile sul numero di eventi. Un tentativo di confronto con un build «base» non è valido: il link ha combinato gli oggetti shim del tree di build con un oggetto ricompilato da sorgente e ha cambiato la riproducibilità del crash (exit 9). Non lo uso come misura.
- La strumentazione temporanea è stata rimossa. Verificato: `win32_shim.c` ripristinato identico (MD5 `bd67c1532950edb0408930c17b2d63e3`); `build/runtime-arm/shim-port/obj/win32_shim.o` ripristinato all'hash iniziale (MD5 `a9bca5f9dfdfa03edf7b193a68856bfd`).

## Gate di accettazione
1. **Non superato/non misurabile:** nessun conteggio affidabile di handle finti; il run termina prima del percorso osservabile e manca un candidato coerente.
2. **Non verificato:** nessun confronto byte/hash per `font\\font.gid` (72 B) o PCX (900 B). `mpq_diag.c` non esegue estrazione/confronto dei contenuti.
3. **Non superato:** template 0x6A ancora presente nei log disponibili; il nuovo run misurale si è fermato prima.
4. Hash SHA-256 del candidato di riferimento, invariato: `build/starcraft-armv6-mt` = `377e32c678397a28a06814b3e556088b7e7a271c1f4f6986499be2aa11270f8d`. **Non è un candidato prodotto da questa missione.** `make check` non eseguito; nessun candidato di fix consegnabile.

## Prossimo passo necessario
Serve un run ARM riproducibile che superi il muro di dispatch corrente (`0x00427714`) con gli asset reali e gli oggetti shim coerenti con le sorgenti. Nel run, aggiungere statistiche alla soluzione realmente agganciata (`mpq_diag_get_stats`: created/live/peak; più contatore errori allocazione) e tracciare i path e byte letti; quindi verificare i contenuti contro valori x86 o estrarli con `mpq_diag`. Solo dopo la misura si può decidere se il pool di `win32_shim.c` è effettivamente raggiunto o se il bridge dinamico di `mpq_diag.c` intercetta già tutte le chiamate. Alla data del report non è stato possibile concludere il fix né attestare i gate.
