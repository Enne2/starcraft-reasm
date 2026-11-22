# Report — scx86-play (stato raggiunto)

## Esito

Missione **parziale**. In modalità Xvfb isolata e senza audio reale il gioco è arrivato al menu principale e alla schermata `Select Game Type`; scegliendo `Original` appare `Registry` con il modal di creazione del nome personaggio. Non è stato possibile confermare il profilo: i test terminano con fail-closed `STOP` a `0x90909090`. Campagna Terran, briefing e ingresso missione **non verificati**. Nessun codice runtime modificato.

## Esecuzione isolata

Comando usato:

```sh
./tools/run-isolated.sh start --id scx86-play --skip-intro 1 --smack 1 --script campaign --trace 1
```

Runner: `tools/run-isolated.sh`; istanza su Xvfb privato `:102`, audio SDL dummy, `SC_CONTROL_FILE` separato e cartella profili `build/isolated/scx86-play/sandbox/characters`. Seconda prova manuale `--id scx86-play-manual`, display `:103`, senza script. Screenshot con `./tools/run-isolated.sh shot --id <id> --out <png>`; input solo tramite `cmd` interno. Entrambe le istanze sono state fermate con `stop`.

## Schermate e coordinate osservate (640×480)

- Menu principale: `Single Player` circa **(220,120)**. Click manuale confermato; apre `Select Game Type`.
- `StarCraft Original`: click **(260,226)** confermato; apre Registry e la finestra `Enter new character name`.
- Finestra nuovo personaggio: campo circa **(320,227)**, bottone `OK` circa **(235,313)**. Il testo è stato visualizzato, ma la conferma non ha portato a un profilo utilizzabile.
- Screenshot ispezionati: `build/isolated/scx86-play-manual/shot-main-menu.png`, `shot-gametype.png`, `shot-after-original.png`, `shot-profile-typed.png`, `shot-after-profile-ok.png`; inoltre `build/isolated/scx86-play/shot-boot.png`, `shot-original-selected.png`, `shot-profile-created.png`, `shot-postbriefing.png`, `shot-current.png`.
- Lo script `campaign` stampa marker per connessione (150,102; OK 518,391), profilo (298,227 / OK 235,313), Terran (313,256), briefing Start (520,400). Questi marker attestano l’emissione degli eventi, **non** l’avanzamento effettivo: screenshot successivi mostravano ancora il modal del profilo. Coordinate Terran e Start non sono quindi confermate visivamente.

## Blocco e diagnosi disponibile

- Run dummy con `SC_SMACK=0`: esce con `MessageBoxA: Could not load "glue\\mainmenu\\Exit.smk"`. Per la navigazione utilizzare `--smack 1`.
- Con `SC_SMACK=1`, dopo la schermata di creazione profilo il runtime si ferma fail-closed a `0x90909090`. La catena log mostra ritorni guest `0x004118F1` e `0x0041188E`, top `0x0050CF90`; il PE disassemblato colloca `0x004118E4–0x004118F1` nel percorso di stream/file (`call 0x0040F652`). L’IDA MCP non ha aperto `build/lift-full/StarCraft.orig.exe.i64`; il disassemblato da solo non dimostra il writer né la causa. D19 resta intatto.
- Le due prove hanno duplicato il testo per sovrapposizione tra script e input manuale, creando soltanto nei sandbox isolati i file vuoti `p8testp8test.spc` e `test01test01.spc`. Questi non sono profili validi e non vanno riutilizzati.
- Nessun fix applicato; nessuna ricostruzione eseguita. I report precedenti documentano i gate x86 già passati, ma non li ho rieseguiti in questo blocco.

## Decisioni autonome

Ho fermato le istanze, mantenuto D19 e non introdotto workaround o target arbitrari: l’evidenza non individua ancora la causa del `0x90909090`. Ho registrato in qmem le coordinate verificate distinguendole dai marker dello script.

## Prossimo passo

Rilancio in una nuova istanza isolata, senza `SC_INPUT_SCRIPT=campaign`: predisporre nel sandbox per-ID una copia di un `.spc` valido da 44 byte, quindi usare screenshot e `cmd` un comando alla volta (evitando duplicazione del testo). Se il crash si ripete, fermare l’istanza e acquisire GDB al passaggio `0x004118E4–0x004118F1` e al target `0x90909090`; verificare l’API/file handle e il flusso esatto prima di modificare runtime. Solo dopo profilo validato proseguire verso menu Terran, briefing e missione.
