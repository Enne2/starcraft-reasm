# Stato — link x86 / smoke checkpoint

- Link `tools/link-x86.sh` riuscito (35 runtime + 12 shim); `build/starcraft-x86` ELF32 EXEC Intel 80386, `.text` 0x18000000.
- `dispatch-ir.py --dump-map`: 102376 voci ordinate/univoche, collisioni host/guest 0.
- Segmenti guest PE/data e codice separati da `.text` host; nessuna sovrapposizione VMA.
- Smoke esatto richiesto: exit 9, SIGSEGV durante DllMain Storm; prima API `InitializeCriticalSection`, non raggiunge MPQ/menu.
- Diagnosi: funzione guest IDA `DllEntryPoint` 0x1503F6A0 usa `stru_1505BDC0` (0x1505bdc0); traduzione/link fornisce 0x18333b3c. `gen_game_layout.py` non mappava i dati translated x86: oggetti hanno `.data`/`.rodata`, non `.data..L_data`.
- Fix già tentati: regex typed-pointer in `gen_game_layout.py`; runtime build passa `entry-x86/*.entry.ll`. La build successiva si ferma su sezione `.data..L_data` assente. Mapping x86 dati ancora da implementare.
- Dettagli, output e log: `reports/REPORT-x86-step3.md`; smoke `build/qa/run-x86-step3*.log`.

## Prossimo passo
Adattare il layout i386 per le sezioni ELF `.data`/`.rodata` dei due entry object, preservare ARM, quindi ricompilare/runtime/link, validare VA dati con readelf e ripetere dump-map e smoke 15s. D19: nessun workaround con dati/stub fittizi.
