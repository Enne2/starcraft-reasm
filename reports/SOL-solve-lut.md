# SOL-solve-lut — esito GDB/IDA (causa non provata, nessun fix)

## Sintesi
La premessa «`unk_5994E0` e `unk_6D05B0` restano sempre tutti zero» non regge per l’intero flusso: GDB mostra `unk_5994E0` nonzero quando `sub_4178B0` viene raggiunta; `unk_6D05B0` è zero all’ingresso della funzione, prima della copia. Non ho acquisito una prova post-copia, quindi non posso attribuire la LUT nera né applicare un fix causale. `fix.patch` resta vuoto; nessun binario privato corretto è stato costruito.

## Evidenze IDA e GDB
- Database IDA: `build/qa/fix-ht/oracle/StarCraft.orig.exe.i64`, sessione `8b16411d` (solo questo database è stato consultato; altre sessioni lasciate intatte).
- `sub_4DD910` @ `0x4DD910` chiama `sub_417C20(nullptr,nullptr)` @ `0x4DD925`; quest’ultima azzera intenzionalmente `unk_6D05B0` 1024 B @ `0x417C31`, quindi il valore zero dopo reset non dimostra un errore.
- `sub_4DD910` carica `\\tEffect.pcx` con `storm_323` e salva palette/risultati nel blocco `dword_51C60C`.
- `sub_4DD9E0` @ `0x4DD9E0`: nel ramo iniziale costruisce `\\BackGnd.pcx` e chiama il loader `sub_4D27A0(&unk_5994E0,...)` @ `0x4DDAF8`; quindi aggiorna palette e invoca `sub_4178B0(&unk_5994E0,a4)` @ `0x4DDB3A`.
- Xref IDA a `0x5994E0`: anche `sub_4C99C0` @ `0x4C9A51` lo passa a `sub_417C20`; `0x51C008` è il buffer alternato della copia in `sub_4DD9E0`. Xref a `0x6D05B0`: scritture in `sub_4178B0` e reset in `sub_417C20`.
- `sub_4178B0` @ `0x4178B0` confronta i 1024 B di input con `unk_6D05B0`; se diversi copia input nel globale e genera le LUT con `sub_4174A0` @ `0x4174A0`. `sub_4174A0` usa la palette come byte RGB triplet in record da 4 B e riempie una LUT 256×256.
- Per GDB, gli indirizzi guest tradotti sono stati risolti con `llvm-nm`: `loc_4178B0=0x080B3110`, `loc_417C20=0x080B50B0`, `loc_4DD9E0=0x085CC420`, `loc_4C99C0=0x08553280` (non i VA PE originali).
- Log GDB: `build/isolated/solve-lut-gdb2/game.log`. Entrando in `sub_4178B0`, GDB ha letto in `0x5994E0` parole nonzero (es. `0x0010216b`, `0x0031297b`) mentre `0x6D05B0` era zero **all’ingresso**. Una successiva palette osservata era anch’essa nonzero. Il log non campiona il globale dopo il blocco copia né il lookup subito dopo.
- Secondo report precedente `reports/SOL-fix-ht.md`: lookup osservato nel Registry produceva 0 per `(src=0xFE,dst=0x43)` e il renderer lo scriveva correttamente; ciò non stabilisce se la LUT o il colore siano semanticamente errati.

## Verifica visiva (run isolato, dummy audio)
- Run base: `./tools/run-isolated.sh start --id solve-lut-verify --skip-intro 1 --smack 1`; nessun file runtime/build vietato modificato.
- Letti: `build/qa/solve-lut/hover-{0ms,150ms,400ms,1000ms}.png`, `gametype-verify.png`, `registry-verify.png`, `menu-ingame.png`.
- Hover: animazione sovrapposta oscura quasi tutto il testo a 150 ms; il testo riappare a 400/1000 ms. Non prova scritte trasformate stabilmente in nero.
- Select Game Type e Registry mostrano ancora pannelli neri/opachi. È un esito non risolto, non una verifica di fix.
- MENU in partita raggiunto con percorso sandbox profilo → Terran → Skip Tutorial → missione; screenshot `menu-ingame.png` letto. Boot Camp/MENU specifico non verificato. Con `--skip-intro 0`, dopo 5 s screenshot `skip-intro-0.png` mostrava ancora il logo Blizzard: confronto non conclusivo.

## Artefatti, limiti e decisioni autonome
- Copia di lavoro: `build/qa/solve-lut/src/storm_sdraw_arm.c` (identica all’originale); `build/qa/solve-lut/fix.patch` è 0 byte. Non c’è `starcraft-x86-lut`: non ho compilato un binary senza una modifica causale da testare.
- `./tools/run-isolated.sh stop` eseguito per tutti gli ID creati (`solve-lut`, `solve-lut-gdb*`, `solve-lut-watch`, `solve-lut-verify`, `solve-lut-intro`); tutti risultano non vivi.
- Nessuna modifica a `runtime/`, `tools/`, `build/obj-x86/` o `build/starcraft-x86/`; il progetto non contiene metadati Git.
- **Decisioni autonome:** non introdurre un fallback palette né alterare la LUT sulla sola base del valore zero prima della copia. Per chiudere serve una misura GDB post-copia di tutti i 1024 B e una corrispondente LUT/lookup confrontata con l’oracolo originale; ripetere con `--skip-intro 0` fino alla schermata, poi patchare solo la prima divergenza dimostrata.
