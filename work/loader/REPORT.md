# Checkpoint — SRW / ordinali e passaggio flags tra regioni

## Implementazione precedente (contesto)
- SRW locale è OUT_LLASM. Import ordinali: nome canonico `ordinal_<hex-octets-DLL-ASCII-lower>_<ordinale-decimale>`; casefold ASCII prima della codifica esadecimale, perciò `.` e `_` non collidono. Import ed export ordinali non nominati condividono lo schema; mapping legacy prioritari. Esempio Storm #508: `ordinal_73746f726d2e646c6c_508`.
- `build/loader/check_ordinal_imports.py`: PASS 135/135 import PE storm.dll presenti nell’export catalog JSON, unici e coperti. È un gate strutturale, non prova che StarCraft abbia superato il parser SRW.

## Lavoro corrente: flags tra regioni
File sorgente aggiornati: `vendor/sr/SRW/SR_full_llasm.c`, `vendor/sr/SRW/SR_full.c`, `vendor/sr/SRW/SR_defs.h`, `vendor/sr/SRW/container_helpers.h/.cpp`, `vendor/sr/SRW/judy_helpers.h/.c`. Il campo `region_data.flags_to_write` trasporta il live-in. OUT_LLASM processa le regioni in ordine inverso; se il live-in non è risolto, `SR_disassemble_region_llasm` lo propaga alla regione precedente solo quando il confine è contiguo e termina su un’istruzione. Non cancella flags: buchi/confini non contigui continuano a terminare con errore. Il ramo X64 mantiene l’ordine precedente. Inizializzazione del nuovo campo coperta sia dai container C++ sia Judy.

## Verifica IDA privata
Sessione IDA aperta sulla copia privata `build/loader/storm.private.dll`; le sessioni in `work/slice` e `build/relocs` sono state lasciate intatte. Nessun rename/commento persistente.
- Il precedente stop `0x15002BEB` (JNB) in `sub_15002B60`: il compare a `0x15002B6D` stabilisce CF e il `push edi` non modifica flags. La vecchia regola diagnostica `loc_15002BEB,0,0x01` è stata rimossa dal file `work/loader/instruction_flags.sci` (ora vuoto): l’obiettivo corrente è il trasferimento nel codice, non un override SCI.
- Delta storico dopo quella regola: stop successivo a `0x15005DAA`, mask `0x8`. IDA mostra `cmp ebx,esi` a `0x15005DA6`, `ja` a `0x15005DA8` (non modifica flags), `mov eax,esi` a `0x15005DAA`, quindi `jz` a `0x15005DAC`; ZF live attraversa la regione. È un candidato coerente per il trasferimento generalizzato.
- Con la nuova iterazione inversa, il run Storm reale si arresta prima di raggiungere `0x15005DAA`: IDA privata mostra `0x15043BD6` = `fldpi` in `sub_15043B89`; gli opcode circostanti sono test/jz e x87 load/store. Il run standard fallisce `unknown flags ... fldpi` (exit 5): la tabella `udis86_dep` non classifica quell’istruzione. Test diagnostico solo in copia sotto `build/loader` assegnando FL_NONE a FLDPI ha superato il controllo EFLAGS ma si è fermato subito dopo con `unknown instruction ... fldpi` (emitter non implementa l’opcode), exit 5. Nessun bypass o patch di tale controllo è stato mantenuto. Di conseguenza il passaggio alle regioni 0x15005DAA/0x15002BEB non è ancora misurato con questa traversata inversa.

## Build e test
- Build standard riuscita: `python3 -m SCons -C ../../build/loader/srw-src -c && python3 -m SCons -C ../../build/loader/srw-src`.
- Run reale: `../../build/loader/srw-src/SRW.exe /home/enne2/dev/sc-reasm/storm/storm.dll.orig /home/enne2/dev/starcraft-armv6/build/loader/storm-flags-final.llasm`; exit 6 globale, causa lifting `unknown flags` a `0x15043BD6`, non un errore loader/export.
- Il test-only FLDPI in `build/loader/srw-src/udis86_dep.c` è stato rimosso ripristinando la copia dal vendor e ricostruendo. Nessuna modifica a loader/export fixup; nessun binario originale alterato; nessun gioco avviato.
- StarCraft.exe resta separatamente bloccato prima del parsing import SRW perché PE relocs stripped e `relocations.csv` non disponibile nel workspace; worker relocs non toccato.

## Decisioni autonome
Non ho esteso il lavoro al decoder/emitter `fldpi`: è fuori dal focus richiesto e il test conferma che introdurre solo FL_NONE non basta, poiché manca anche l’emissione dell’opcode. Ho mantenuto attivi tutti i controlli e lasciato vuoto il file SCI, preferendo dichiarare il gate non validato invece di mascherarlo.

## Limiti e prossimo passo
La generalizzazione è compilata, ma non ancora validata ai due gate Storm a causa dell’istruzione `fldpi` non supportata, incontrata prima dall’ordine inverso. Occorre decidere/implementare supporto corretto per FLDPI nell’ambito appropriato, oppure isolare la verifica dei trasferimenti senza eludere il decoder/emitter; poi ripetere Storm e confrontare con i gate precedenti. Il log `[flags-region-propagate]` permette di osservare ciascun trasferimento effettivamente eseguito.
