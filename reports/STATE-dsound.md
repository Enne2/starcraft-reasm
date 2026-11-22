# Stato scx86-dsound

- Nessun fix sorgente applicato; report: `reports/REPORT-dsound.md`.
- DS pool statico: 2048; mixer host: 32 voci con reap/furto LRU; controlli volume/pan/freq già implementati, test >16 non fatto.
- Import/stringhe originali: solo riscontri `dsound.dll`/`DirectSoundCreate`; nessuna evidenza EnumerateA/Capture.
- IDA: `doexit` 0x00404F09 chiama callback dalla tabella 0x006DD684; `__onexit_lk` 0x00406041 scrive il callback ricevuto in EDI. Writer del valore 0x1805F2D4 non identificato; D19 intatto.
- X11: `host_elf.c` richiede bypass compositor e crea direttamente `SDL_WINDOW_SHOWN`; analisi binaria della libreria non fatta in questo turno.
- Runner `dsound-exit` fermato (gioco e Xvfb terminati). Il test non ha usato SC_BIN privato e non prova Exit.
- Prossimo passo: GDB watchpoint in run isolato con SC_BIN esplicito sul file privato; identificare il writer prima di ogni fix. Test DirectSound -m32 e ispezione `nm -D`/`objdump` rimangono aperti.
- Nessun input desktop, nessuna finestra su `:0`, nessun audio reale.
