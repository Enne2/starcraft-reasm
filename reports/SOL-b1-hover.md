# SOL — Bug 1 hover nero

## Stato
Il difetto è riprodotto nel menu: a 150 ms di hover resta visibile solo la `S` di “Single Player” mentre l'animazione copre la zona; a 400 ms la scritta riappare. **Root cause/fix ancora non dimostrati.** Non ho modificato il sorgente di progetto né creato un diff runtime.

## Trace privata
- Binario ELF32 privato `build/qa/b1-hover/starcraft-x86-b1` (~217 MiB), linkato da 35 oggetti runtime e 13 `shim-redef`; nessuna scrittura su `runtime/`, `build/obj-x86/` o `build/starcraft-x86`.
- Screenshot letti: `build/qa/b1-hover/verified-before.png` e `verified-hover-{0ms,150ms,400ms,1000ms}.png`. Runner `b1-hover-fix` fermato.
- Trace `storm_334` per il kernel: return `0x080AFF90`, descriptor flags `0x40000`, width `0x140`, rows `0x60`, dst delta `0x140`, src delta `0`, `dst=0x20590010`, `src=0x20607AA4`, LUT/base `0x20AE0010`. Kernel SCode `size1=0x4A`, `size2=0x2BC5`.
- Campionamento della LUT 64 KiB: un solo byte zero; `LUT[(0<<8)|0x2A] = 0x2A`, quindi il pixel sorgente zero conserva il destination campionato. Verifica QA indipendente `lut[(src<<8)|old_dst]` contro ogni output riga: **0 mismatch** nei campioni acquisiti (trace ogni 8 chiamate dopo le prime 512). Questo non esclude una differenza di ordine/tempo o di altri rettangoli.
- IDA, copia QA di Storm: `storm_457` @ `0x1502F500` chiama `sub_1502EFA0` @ `0x1502EFA0` dal ramo `0x1502F8B5–0x1502F8B8`; `sub_1502EFA0` richiama `storm_334` a `0x1502F288` e `0x1502F42A`. `storm_334` @ `0x15030AB0` seleziona entry/prefisso e patcha le code; stub `0x15030D30` decrementa rows e aggiorna ESI/EDI; terminazione `0x15030D80` salva ECX/EDX/EBX nel contesto. Questi passaggi sono coerenti con il modello runtime per il caso osservato, non provano la causa del frame nero.
- Trace DirectDraw: `s_Blt` 0 chiamate nel log; `s_Lock`/`s_Unlock` osservati sulla primaria 640×480, pitch 640, `px=0x19F76A70`; `sync_primary` riallinea lo stesso puntatore del canvas; `present` riceve la stessa primaria. Contatori limitati ai primi eventi, quindi non escludono tutte le race.

## Prossimo passo
Rilanciare in Xvfb con il binario QA e **solo** una traccia mirata, non il dump byte-per-byte: `cd /home/enne2/dev/starcraft-reasm-generic && SC_BIN="$PWD/build/qa/b1-hover/starcraft-x86-b1" SC_SMACK_TRACE=1 ./tools/run-isolated.sh start --id b1-hover-fix --skip-intro 1 --smack 1`. Attendere il menu, marcare `move 220 120` e correlare lo stesso frame 0/150/400/1000 ms con: callsite di `storm_334`/`sub_1502EFA0`, entry e `prefix/r` effettivi, rectangle source/dest, byte destination prima/dopo, `SmackDoFrame`, Lock/Unlock/Present. Mappare `0x080AFF90` al callsite StarCraft originale e confrontare i pixel della stessa area con l'oracolo originale; fare una patch privata solo dopo aver localizzato la prima divergenza.

## Decisioni autonome
Nessun fix ipotetico. Istanza arrestata su richiesta; nessun sotto-agente, nessun input su `:0`, audio dummy.
