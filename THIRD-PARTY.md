# Third-party components and legal notice

## Codice di terzi incluso in `vendor/`

| Componente | Percorso | Licenza | Come viene usato |
|---|---|---|---|
| **SR / llasm** (static recompiler per eseguibili DOS/Windows) | `vendor/sr/` | **MIT** (l'autore la offre anche come GPLv2+ o LGPLv2.1+; qui viene usata nella forma MIT) | produce la rappresentazione intermedia (`.llasm`) del codice guest da cui parte la nostra pipeline di lifting |
| **udis86** (disassemblatore x86) | `vendor/sr/**/udis86-*` | licenza BSD a 2 clausole, come distribuita dal progetto SR | usato da SR per la decodifica delle istruzioni |
| **libsmacker** (decoder Smacker) | `vendor/libsmacker/` | **LGPL 2.1** (vedi `vendor/libsmacker/COPYING`) | decodifica i filmati `.smk`; viene caricata a **runtime** come modulo separato (`dlopen`, variabile `SC_SMACKER_SO`), mai linkata staticamente |

Il testo integrale delle licenze di questi componenti è nelle rispettive directory.

## Nessun dato o codice di StarCraft è incluso

Questo repository **non contiene** e non deve contenere:

- gli eseguibili originali (`StarCraft.exe`, `BroodWar.exe`, `storm.dll`, `battle.snp`,
  `standard.snp`, `local.dll`, …);
- gli archivi di dati (`.mpq`), i filmati (`.smk`), le mappe (`.scm`/`.scx`), i profili (`.spc`),
  i salvataggi (`.sng`), le immagini o i suoni del gioco;
- il codice tradotto del gioco (`StarCraft.ll`, `storm.ll`, `*.entry.o`, database IDA `.i64`):
  sono **artefatti generati localmente** dalla pipeline a partire da una copia personale del gioco
  e non vengono versionati (vedi `.gitignore`).

Per usare il progetto serve una **copia personale e legittima** di StarCraft/Brood War 1.16.1.
Gli asset restano di proprietà di Blizzard Entertainment: questo progetto non li ridistribuisce,
non li incorpora e non ne aggira la protezione.

## Marchi e affiliazione

StarCraft, Brood War, Battle.net e Blizzard Entertainment sono marchi di Blizzard Entertainment, Inc.
Questo progetto **non è affiliato, sponsorizzato né approvato da Blizzard**.

## Natura del progetto

È un lavoro di **interoperabilità e preservazione**: il codice originale non viene ridistribuito; il
repository contiene la *pipeline* che ne traduce il codice macchina in una forma ricompilabile
(lifting) e il *runtime* che ne reimplementa i servizi di sistema (shim), più gli strumenti di analisi
e verifica. Chi lo usa deve possedere il gioco.

Nessuna garanzia: il software è fornito "così com'è", senza garanzie di alcun tipo, come previsto
dalla GNU General Public License v3.0 (vedi `LICENSE`).
