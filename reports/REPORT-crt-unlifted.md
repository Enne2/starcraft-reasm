# Audit `crt_unlifted_1504xxxx` in Storm

## Esito

Nessuno dei 20 nomi individua una funzione CRT autonoma da implementare come body C. I nomi sono marcatori/alias generati per indirizzi dentro il codice o per una jump-table nella regione di parser CRT di Storm; la presenza di una dichiarazione `crt_unlifted_*` nell’IR non dimostra che il VA sia una funzione. Non sono stati aggiunti stub né implementazioni speculative. I file `runtime/common/storm_crt_unlifted.c/.h` sono intenzionalmente vuoti a livello di API.

## Metodo ed evidenze

- `llvm-readobj --file-headers --sections build/lift-full/storm.orig.dll`: COFF-i386/PE32, ImageBase `0x15000000`, `.text` RVA `0x1000`.
- `llvm-objdump -d --start-address=0x15040400 --stop-address=0x15043500 build/lift-full/storm.orig.dll` (finestra completa salvata temporaneamente in `/tmp/crt-window.asm`); `build/lift-full/storm.llasm` è presente ed è testo disassemblato, con ogni VA e istruzioni esadecimali.
- `build/lift-full/entry-x86/storm.entry.ll`: contiene alias privati `@loc_<VA>` e dichiarazioni `crt_unlifted_<VA>` per tutti i 20; ciò è infrastruttura di lifting, non prova di funzione CRT.
- Call effettivi osservati nell’IR: `loc_15042FFE` invoca `crt_unlifted_15043271` per il ramo `eax > 11`; nello stesso blocco calcola un indirizzo dalla base `crt_unlifted_1504339F` indicizzata da `eax` e ne carica il target. L’objdump conferma a `0x15042FFE` il jump indiretto via tabella a `0x1504339F`, e a `0x1504339F` i byte sono dati della tabella, non istruzioni di una funzione. Gli altri alias sono riferimenti d’indirizzo usati nei blocchi esternalizzati/dispatch del lift, non call-site autonomi dimostrati.
- IDA era collegata al database `build/lift-full/storm.orig.dll` (sessione `09fa6795`), ma l’audit qui si fonda sul disassemblato originale e sull’IR indicati sopra.

## Tabella completa

| VA / simbolo | Contenuto originale | Riferimento / ruolo IR | Esito |
|---|---|---|---|
| `1504048B` | `ret` (`C3`), coda del gruppo di helper x87 immediatamente precedente | alias `loc_1504048B`, dichiarazione external; nessun call come procedura indipendente provato | epilogo/ret, non implementare |
| `15040DF3` | `xor eax,eax` dentro il flusso a `15040Dxx`; seguito da `inc eax; ret` | alias/dichiarazione external | istruzione interna (risultato 1 nel ramo), non entry autonoma |
| `15040DF7` | `mov esp,[ebp-18h]`, ripristino stack | alias/dichiarazione external | epilogo interno |
| `15041A41` | store locale e operazioni x87 nel flusso matematico | alias/dichiarazione external | istruzione interna, non funzione CRT |
| `15041B81` | `or cl,cl` + branch in routine x87 | alias/dichiarazione external | istruzione interna |
| `15041D4D` | `fstp st(0)` seguito da `fld1; ret` | alias/dichiarazione external | ramo/epilogo x87, non funzione autonoma |
| `15041D52` | `fstpt [ebp-9Eh]` / store temporaneo x87, seguito da load | alias/dichiarazione external | istruzione interna |
| `15043005` | `cmp bl,31h`, parser numerico | alias/dichiarazione external | case interno del parser |
| `15043058` | `xor eax,eax`, parser numerico | alias/dichiarazione external | case interno del parser |
| `150430B9` | `cmp bl,31h`, parser numerico | alias/dichiarazione external | case interno del parser |
| `150430E4` | inizializza locale e salta nel parser | alias/dichiarazione external | case interno del parser |
| `1504311F` | `xor eax,eax` e setup locali del parser | alias/dichiarazione external | case interno del parser |
| `15043177` | estende `bl` e chiama helper di caratteri dentro parser | alias/dichiarazione external | case interno, non CRT entry |
| `15043197` | confronti e branch parser | alias/dichiarazione external | case interno del parser |
| `150431CF` | imposta flag locale e prosegue nel parser | alias/dichiarazione external | case interno del parser |
| `15043224` | confronti e branch parser | alias/dichiarazione external | case interno del parser |
| `15043241` | legge argomento locale `[ebp+20h]`, parser | alias/dichiarazione external | case interno, non funzione autonoma |
| `15043271` | confronta EAX con 10 e dirama nel parser | `loc_15042FFE` usa l’entry esterna come destinazione del ramo condizionale | blocco interno raggiungibile, non funzione CRT autonoma |
| `15043289` | istruzioni parser, salva locali e avvia conversione | alias/dichiarazione external | case interno del parser |
| `1504339F` | bytes che codificano gli elementi della jump-table (inizializzata da `15042FFE`) | indirizzo base caricato/indicizzato da `loc_15042FFE` | dato, non codice né funzione |

## Implementazione e test

Creati `runtime/common/storm_crt_unlifted.c`, `runtime/common/storm_crt_unlifted.h` e `tests/x86_crt_unlifted_test.c`. Il test è una guardia di compilazione del caso “nessun body standalone”; non pretende di testare la semantica del parser o dell’aritmetica x87. Nessun file vietato è stato modificato. Nessun file di bridge è stato aggiornato: i mapping/alias esistenti restano responsabilità della pipeline di lifting.
