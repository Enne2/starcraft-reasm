# STATE-solve-lut

- Esito: causa della LUT nera non dimostrata; nessun fix o binary privato corretto.
- IDA DB `build/qa/fix-ht/oracle/StarCraft.orig.exe.i64` / sessione `8b16411d`.
- `sub_4DD910` @ 0x4DD910 chiama reset `sub_417C20` @ 0x417C20; reset azzera 0x6D05B0. `sub_4DD9E0` carica BackGnd.pcx in 0x5994E0 e chiama sub_4178B0 @ 0x4DDb3a.
- GDB: loc_4178B0=0x080B3110, loc_417C20=0x080B50B0, loc_4DD9E0=0x085CC420. Log `build/isolated/solve-lut-gdb2/game.log` mostra 0x5994E0 nonzero all’ingresso di sub_4178B0; 0x6D05B0 zero soltanto al campione pre-copia. Post-copia non misurato.
- Screenshot letti: hover 0/150/400/1000 ms, Select Game Type, Registry, MENU in partita. Pannelli ancora neri; hover è occlusione temporanea d’animazione. Nessuna schermata Boot Camp.
- Skip intro 0: dopo 5 s era ancora al logo; run non conclusivo.
- `build/qa/solve-lut/src/storm_sdraw_arm.c` è copia identica; `build/qa/solve-lut/fix.patch` vuoto; `starcraft-x86-lut` non creato.
- Tutti i runner solve-lut avviati sono stati fermati. Nessuna modifica ad aree vietate. Nessun Git metadata nel progetto.
- Decisione conservativa: niente patch speculativa. Prossimo test: watchpoint/campionamento dei 1024 B subito dopo il memcpy in sub_4178B0, confronto con LUT effettiva e binario originale.
