# Report — ARMv6 Storm blitter

## Stato (2026-11-20)

L’obiettivo era rimpiazzare il JIT x86 Storm con wrapper ARMv6, generare solo Storm, linkare `build/starcraft-armv6-blt` e verificare il menu via smoke. Ho letto integralmente `work/link/REPORT-arm-sdraw.md` prima di procedere e consultato qmem (record progetto coerente, score 0.6261). Il DB usato è esclusivamente `build/lift-full/ida/storm.lift.dll.i64`, sessione IDA progetto `1adcd661`; nessuna sessione altrui toccata. La directory di lavoro non appartiene a un repository Git (`git status` fallisce: nessun repository).

## Verifiche IDA eseguite

- `storm_313` (`0x15036260`, pseudocodice completo): firma Hex-Rays `__thiscall(this, a2..a9)`, cioè ECX + 8 dword stack; ritorna 1 se `a4<=0 || a5<=0` (0x15036276–78). Fast path per ROP `0xCC0020` o `0xF00021`, `(a4|a2)&3==0`, `a4<=0x280` (0x150362AE): prende il kernel/slot via `sub_150015C0(&stru_1505EBEC)` (0x150362DE), indice `a4>>2` (0x150362F5), poi salta al kernel tramite `jmp ebx` (0x1503631F). Altrimenti, per ROP diverse chiama `sub_15035E40(this,&a8)` (0x1503635C), ottiene dispatcher `sub_150015D0(rop)` (0x15036372), prepara la struct locale: `[0]=0x40,[1]=0,[2]=a4,[3]=a5,[6]=a2,[7]=a3,[4]=a6-a4,[5]=a7-a4,[11]=a8` (0x15036375–B0), quindi chiama `storm_334` (0x150363C0). Il ritorno è `retn 0x20` a 0x150363C6.
- Dal call site `storm_358` (`0x15035390`), call a `storm_313` a 0x1503550B: `ECX=dword_1505A7C8`; stack args in ordine `a2=v14` (dst), `a3=a2` (src), `a4=v9` (larghezza in byte), `a5=dword_1505A7C8` (dst pitch/stride), `a6=v12`, `a7=v13`, `a8=0`, `a9=0xCC0020`; valori derivano da `storm_350` (0x150354B6–CF) e stato superficie. Questo identifica SRCCOPY in quel chiamante, non prova in generale che i due stride siano pitch o il loro segno.
- `storm_388` (`0x15023210`) chiama `storm_313` a 0x150232BB con `ECX=dst`, `a2=dst`, `a3=0`, `a4=(v8-v9)<<dword_1505EAC4`, `a5=v7-v11`, `a6=dword_1505A7FC`, `a7=0`, `a8=v10`, `a9=0xF00021` (PATCOPY). Le coordinate sono clampate a zero/dimensioni (0x1502325E–88); destinazione deriva da `a1 + (v9<<shift) + *(dword_1505EAB8+4*v11)` (0x15023231). `sub_15022810(a6)` produce il pattern (0x15023255), ma il suo layout/semantica non è stato ancora ricostruito.
- `storm_334` (`0x15030AB0`) pseudocodice completo: due argomenti puntatore/struct; usa `a2+8` (struct offset +8) come larghezza/parametro (0x15030AC5); flag `a1[2]&0x40000` sceglie il percorso con allineamento, modifica `a1[3]`, seleziona entry da tabelle `a1[]`, patcha displacement relativi ai buffer e seleziona `sub_15030D30` o `loc_15030D50` in base a `a1[2]&0x4000000` (0x15030AD4–0x15030CE6); poi carica i registri guest (0x15030CF6–0x15030D27) e `jmp eax` a 0x15030D2A. Non è un blitter C statico: dispatch/patch del kernel generato. Il disassemblato mostra `push ebp; mov ebp,esp; sub esp,34h; push ebx/esi/edi` (0x15030AB0–B8), il che è coerente con preservare i callee-saved nel frame tradotto; il template/kernel successivo è tail-jump, quindi per definire il ritorno si deve seguire il template effettivo. Convenzione dell’epilogo `retn 8` riportata nella precedente analisi (`0x15030D9D`).
- Decompilati anche `storm_332` (0x150311D0), `storm_331` (0x15032480), `storm_358`, `storm_388`, `sub_15036330` e `sub_150015C0`. `storm_332` è wrapper stream/RTTI e critica sezione (0x150311D9–24); `storm_331` compone/copierizza sorgenti/codice e buffer SCode (allocazioni/copie/offset 0x150325C0–0x15032832), ma il suo pseudocodice non determina il kernel effettivamente selezionato né tutte le istruzioni/ROP dei template. `sub_15036330` non è stata risolta come funzione autonoma da Hex-Rays e restituisce la funzione contenitore `storm_313`; il disassemblato mostra il gateway decrementare EDX e, per ogni riga, aggiornare EDI/ESI con `arg_10-a4` e `arg_14-a4` (0x15036330–39). `storm_313` salva EBX/ESI/EDI/EBP nel prologo (0x15036260–6B), ripristina ESI/EDI nel fast path (0x1503633B–3C), e EBX/ESI/EDI/EBP nel percorso normale (0x150363C0–C5). `sub_150015C0` è un accessor indicizzato, restituisce `a2+4*(3*a1+6)` (0x150015C7); la forma IDA è usercall EAX/ECX.
- Altri caller verificati: `storm_315` (0x150364A0) spezza rettangoli/regioni, calcola offset sorgente/dest e inoltra a `storm_313` con ROP `a10` (call 0x1503658B). `storm_314` (0x150365D0) clippa rettangoli, calcola i puntatori/stride, e inoltra con ROP `a10` (0x15036748). Quindi entrambe possono inoltrare modalità oltre SRCCOPY/PATCOPY: i loro ROP non sono costanti ma parametri ricevuti.
- `sub_150015D0` (0x150015D0) indicizza il registro dispatch ROP da `BYTE2(ROP)` (0x150015E9–FA); se non trovato invoca `storm_332`/`storm_331` per generare SCode (0x1500160F–5E). `storm_388` usa `sub_15022810(a6)` per produrre un valore pattern/color: per 8 bpp con flag `0x1000000` ripete il colore nei byte; per 16 bpp converte RGB a RGB565 e duplica la word (0x1502281E–7C). Questo chiarisce input di PATCOPY ma non sostituisce la decodifica del codice kernel generato.

## Nuova evidenza runtime — JIT e storm_334

Tutte le compilazioni/link/test sono state lanciate con `TMPDIR=/dev/shm`.

### Fast-path JIT a `0x20170A9C`

Lo smoke diagnostico prima dei mapping ha catturato i byte realmente mappati in memoria (file `build/qa/run-blt-jit.log`; dump esteso `build/qa/run-blt-jit-tail.log`). Primi 64 byte, in ordine:

```text
89 0F 81 C7 04 00 00 00  89 0F 81 C7 04 00 00 00
89 0F 81 C7 04 00 00 00  89 0F 81 C7 04 00 00 00
89 0F 81 C7 04 00 00 00  89 0F 81 C7 04 00 00 00
89 0F 81 C7 04 00 00 00  89 0F 81 C7 04 00 00 00
```

Disassemblato con `llvm-mc --disassemble -triple=i386-unknown-linux-gnu`:

```asm
movl %ecx, (%edi)
addl $4, %edi
```

ripetuto 8 volte nei 64 byte. Il dump esteso mostra lo stesso blocco fino all’offset 0x500 (160 dword store per la larghezza 0x280 byte), poi `E9 93 72 E9 F4` (salto relativo patchato) a offset 0x500. Evidenza pixel certa per questo kernel: PATCOPY/fill di un DWORD con ECX a ogni dword destinazione. Non è evidenza del kernel SRCCOPY: non è stato acquisito il relativo codice JIT.

### Chiamata reale a `storm_334`

Dopo aver linkato i mapping, lo smoke in `build/qa/run-blt-postlink.log` ha raggiunto `storm_334_arm` e terminato intenzionalmente con exit 73 nella diagnostica, non con lo stop 71 del JIT. Quindi il percorso del filmato **non usa soltanto il fast path**. Argomenti reali: `a1=0x20120628`, `a2=0x20440088`; struct a2: `[0]=0x40`, `[2]=0x13F` (319), `[3]=0x134` (308), `[4]=0`, `[5]=0`, `[6]=0x4DC35708`, `[7]=0x18FCC170`, `[11]=0`. Il return host `0x1892EAFC` corrisponde a `loc_1502F42F`, quindi la chiamata osservata proviene da `loc_1502F428`, non è provato che venga da `storm_313`. Il valore letto a `EBP+0xCC` (`0x188A1CD4`) è stato esplicitamente riclassificato come dato diagnostico non-Rop: non è lecito usarlo come ROP.

SCode descriptor: `a1[2]=0`, `a1[3]=0xFFFFFFFF`, `a1[5]=0x201600BC`, `a1[6]=0x1A`, `a1[17]=0x20170FAC`, `a1[18]=0x1685`. Byte effettivi catturati:

```text
a1[5] @ 0x201600BC, size 0x1A:
C7 C0 00 00 00 00 8A 26 FF C6 8A C4 66 89 07 81
C7 02 00 00 00 E9 00 00 00 00

a1[17] @ 0x20170FAC, size 0x1685, primi 64 byte:
8A C4 8A 26 FF C6 8A 04 28 66 89 07 81 C7 02 00
00 00 8A C4 8A 26 FF C6 8A 04 28 66 89 07 81 C7
02 00 00 00 8A C4 8A 26 FF C6 8A 04 28 66 89 07
81 C7 02 00 00 00 8A C4 8A 26 FF C6 8A 04 28 66
```

Disassemblato: il template a1[5] azzera EAX, carica un byte in AH, lo copia in AL, scrive AX e avanza EDI di 2; termina con un `jmp rel32` non ancora patchato nel dump. Il kernel a1[17] ripete `mov al,ah; mov ah,[esi]; inc esi; mov al,[eax+ebp]; mov [edi],ax; add edi,2`. Ciò prova una conversione/tabella indicizzata da byte sorgente e output word (EBP è caricato dal dispatcher da `a2+0x20`, nel caso osservato `0x204D0010`), non un semplice SRCCOPY/PATCOPY bytewise. La semantica esatta della tabella/packing, del salto patchato e dei restanti blocchi del kernel non è stata ricostruita.

Nota diagnostica: la stampa dei buffer ha un byte spurio `@` in coda a ciascuna riga a causa del writer diagnostico che scrive 49 anziché 48 caratteri; i byte esadecimali elencati sopra sono i token byte iniziali, verificati e disassemblati separatamente. Va corretto prima di conservare la diagnostica come interfaccia stabile.

## Implementazione, build e test

Creati `runtime/arm/storm_sdraw_arm.c` e trampolini in `runtime/arm/indirect_dispatch.S`; modificati `runtime/arm/indirect_dispatch.c`, `build/lift-full/external_procedures.sci` (backup `external_procedures.sci.bak`), `build/lift-full/extern.llinc` (dichiarazioni `proc ... external`) e `tools/build-entry-obj.sh` (runtime C + due `--map-extra`). Rigenerazione richiesta completata: `TMPDIR=/dev/shm ./tools/lift-full.sh srw storm && TMPDIR=/dev/shm ./tools/lift-full.sh ir storm` PASS. Il primo tentativo senza dichiarazioni in `extern.llinc` falliva in LLASM (`Unknown instruction parameter: tcall storm_313_arm`); aggiunte le dichiarazioni e la rigenerazione successiva è riuscita.

`TMPDIR=/dev/shm ./tools/build-entry-obj.sh --no-smoke --link-out build/starcraft-armv6-blt` ha eseguito dispatch-ir/opt/llc; link finale eseguito con `ENTRY_DIR=build/lift-full/entry RUNTIME_OBJ_DIR=build/lift-full/entry/runtime ./tools/link-arm.sh build/starcraft-armv6-blt`. `--dump-map`: 96966 voci, ordinate/univoche, 0 collisioni. `TMPDIR=/dev/shm make check`: PASS dopo il link completo e prima dell’ultimo rebuild runtime-only di solo diagnostica; non ripetuto dopo quest’ultima strumentazione. SHA256 dell’ELF attualmente presente in `build/starcraft-armv6-blt` (ultimo link runtime-only): `8c4654cf41e4c74dbe310fdc51adec7d29598e920ffdd15234d30132b140ac2f`. Il precedente ELF full-link aveva SHA `9016bb9c6af93789738fda0997213967f77592a1a605822fb186d09dd1a97cd4`.

`StarCraft.ll` non è stato modificato (SHA256 `8e8917511e0fe34bd24996159b8e4715db8dc3eccd76d86bd0966b373d9a71e6`). Attenzione: la build full ha ricompilato `StarCraft.entry.o`, non lasciandolo in cache; SHA256 corrente annotato al momento della verifica `9f7937e360d30f1554b17b88cef6f237e8e838d766001fce1b7976b1b7929f33`.

Smoke con mapping in `build/qa/run-blt-postlink.log` e successiva raccolta SCode `build/qa/run-blt-code.log` / `build/qa/run-blt-frame.log` / `build/qa/run-blt-code.log`: il gioco carica `blizzard.smk` (320×308, 171 frame), esegue `SmackToBuffer #1` e `DoFrame`, poi arriva al dispatcher generico `storm_334_arm`, che al momento termina exit 73. Nessun menu raggiunto. La cattura JIT del fast path è stata effettuata in una build diagnostica precedente ai mapping, exit 71. I log riportano uscita 73 per il wrapper generico.

## Stato dei wrapper e cosa manca

`storm_313_arm_body` è presente: legge gli otto stack argomenti, implementa copia dword SRCCOPY / fill dword PATCOPY solo per il fast path allineato (`width<=0x280`) e restituisce 1 con pulizia stdcall via rilocazione della return address per `reasm_extern`; ROP non gestiti e percorsi non-fast terminano con exit 73. **Non è ancora verificato end-to-end**: il run corrente intercetta `storm_334` prima di una dimostrazione del rendering completo, e il JIT SRCCOPY non è stato catturato.

`storm_334_arm_body` raccoglie argomenti, struct e SCode, ma la semantica generica non è ancora implementata: ROP non viene passato come argomento; il caller osservato non è provato essere `storm_313`, perciò leggere `EBP+0xCC` come ROP sarebbe scorretto. La funzione mantiene un fail-fast controllato (exit 73) se il ROP non è provato/gestito. Mancano: identificare con IDA il caller `0x1502F428` e il suo contratto/ROP; ricostruire i campi SCode e la semantica della tabella EBP; decodificare il corpo completo del kernel, inclusi jump patchato, epilogo e loop/stride; poi validare pixel e ABI con smoke fino a menu o al prossimo stop. Nessun frame SMACK decodificato salvato né menu raggiunto.

## Decisioni autonome

Proseguo con i wrapper limitati e il fail-fast; non attribuisco un ROP al dato `EBP+0xCC`, non considero completata la semantica di `storm_334` e non dichiaro il blitter corretto né il menu raggiunto. Il risultato dimostrato è la cattura/disassemblaggio di PATCOPY e del kernel SCode generico, più la prova che il filmato entra nel percorso `storm_334`.

## Follow-up: chiamante, campi e interprete SCode (2026-11-20)

### Oracolo IDA (solo `build/lift-full/ida/storm.lift.dll.i64`, sessione `0e066d78`, lettura)

- `0x1502F428` è il call site interno a `sub_1502EFA0`, non una funzione autonoma: `push edi; push eax; call storm_334` e ritorno a `0x1502F42F`. Il corpo completo di `sub_1502EFA0` mostra che il rettangolo ha coordinate `(v19,v20)..(v17,v20+v22)`: `a2[2]=v17-v19` (319 per 320 pixel, differenza destra-sinistra, quindi nel caso catturato width−1), `a2[3]=v22` (differenza bottom-top, 308 nel caso osservato). Nel blit preliminare a 0x1502F288 viene passato `a2[3]=v22−1`; la chiamata di 0x1502F42A passa il valore principale `v22`.
- Nella chiamata principale, IDA assegna: `a2[4]=v39−2*v21`, `a2[5]=v38−v21`, `a2[6]=v41+v54*v39+2*v19`, `a2[7]=v19+v38*v20+v48`; i nomi dst/src non sono ricavabili solo dai nomi variabili Hex-Rays, ma i due delta sono proprio i valori caricati dal dispatcher nel template. `a2[8]` è l’input caricato in EBP dal dispatcher (`[eax+20h]` a 0x15030D1B), non un ROP: al runtime osservato vale `0x204D0010`, base LUT. `a2[9..11]` vengono aggiornati in uscita con ECX/EDX/EBX (0x15030D86–8F).
- All’ingresso di `jmp eax` (0x15030D2A), i registri sono esattamente: ECX=`a2[3]`; ESI=`a2[7]`; EDI=`a2[6]`; EBX=`a1[4]`; stack locale `[0]=EBX`, `[4]=ECX`, `[8]=a2`, `[0xC]=a2[5]`, `[0x10]=a2[4]`; poi EDI=`a2[6]`, ESI=`a2[7]`, EBP=`a2[8]`, ECX/EDX/EBX=`a2[9..11]`. `sub_15030D30` decrementa il contatore `[esp+4]`, termina a zero, altrimenti aggiunge i delta a ESI/EDI e risalta `[esp]`. `loc_15030D50` sceglie il passo dispari/even e, nel caso dispari specifico, usa `a2+0x34` e `a2+0x30`.
- `storm_334` (0x15030AB0) in modalità senza flag `0x40000`: se `a1[3] != a2[2]`, assegna `a1[3]=a2[2]`, `a1[4]=a1[5]`; patcha il displacement finale di `a1[5]` al target `a1[a2[2]+19]` (0x15030C85–A9) e quello di `a1[17]` al loop selezionato (0x15030CB0–E6; `0x4000000` sceglie `0x15030D50`, altrimenti `0x15030D30`). Quindi `a1[5]` è l’entry/segmento iniziale che può saltare a un offset nel kernel, mentre `a1[17]` è il kernel regolare terminante nel loop; non sono alternative intercambiabili. In modalità flag `0x40000` la routine seleziona entry/prefix diversi e modifica ulteriormente `a1[3]`; il supporto runtime qui sotto rifiuta ancora questa variante, invece di simulare patch parziali.

### Implementazione runtime e verifiche

`runtime/arm/storm_sdraw_arm.c` ora applica la patch non-allineata descritta sopra e interpreta il subset x86 SCode effettivamente osservato (`C7 C0`, `8A C4`, `8A 26`, `FF C6`, `8A 04 28`, `66 89 07`, `81 C7`, `89 0F`, `E9`). Il salto `E9` segue il target nella code buffer oppure riconosce i due loop IDA e li emula in C; il range del codice e dei buffer viene validato prima del loop, e un opcode/target fuori subset termina con 73 con PC/byte diagnostici. La LUT per `[eax+ebp]` è validata sull’intero span 64 KiB. `a1[4]` viene letto dopo aver applicato le mutazioni richieste da IDA.

Non ho ancora aggiunto fast-path SCode: non è stata dimostrata una parità byte-per-byte/interprete-versus-fast-path. Resta esplicitamente fail-73 la modalità allineata `a1[2]&0x40000`; non si assume alcun ROP per `storm_334`. `storm_313_arm_body` continua a limitarsi a SRCCOPY (`0xCC0020`) e PATCOPY (`0xF00021`) nel percorso fast verificato (allineato, width ≤ 0x280); altri ROP/percorso terminano 73.

- Build finale: `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-blt` PASS, link completato; warning solo per helper diagnostico `dump_bytes` non più usato.
- `TMPDIR=/dev/shm make check`: PASS (test freestanding ARMv6 e test ordinal PE/Wine x86 eseguito tramite ARM1176).
- Smoke richiesto con asset sandbox, SDL dummy, core dump disabilitato e timeout 25 s: `build/qa/run-blt-final.log`, exit 137 (timeout + kill di sicurezza, interprete troppo lento per il clip lungo). È stato superato il primo `blizzard.smk` di 171 frame e aperto il successivo SMK da 0xD64 frame; nel run precedente la diagnostica osservava frame 47 del secondo filmato con pixel non-zero. Non è stato raggiunto il termine del logo né il menu, e non è apparso un nuovo opcode non supportato. La prova dimostra il progresso oltre il vecchio stop in `storm_334`, ma non ancora rendering corretto end-to-end o performance accettabile.

## Decisioni autonome

Ho scelto l’interprete fail-closed come base conservativa, senza introdurre una scorciatoia non confrontata con l’esecuzione SCode. Ho mantenuto in fail-73 le modalità ROP non verificate e il ramo allineato `0x40000`. Il prossimo passo tecnico è verificare il subset rimanente/il caso di allineamento e confrontare un fast-path con l’interprete prima di abilitarlo; il menu non è dichiarato raggiunto.

## Follow-up: fast-path C 2x LUT e test intro (2026-11-20)

### Modifiche runtime

- Il log di entry per chiamata è ora emesso soltanto con `REASM_TRACE=1` (lettura esatta della variabile). A trace disattivato non c’è output per frame. Rimangono un messaggio one-shot di attivazione fast-path e un one-shot se si cade nell’interprete.
- Aggiunto fast-path C solo per fingerprint stretto e verificabile: `a1[6]==0x1A`, `a1[18]==0x1685`, width-param `319`, prefisso `a1[5]` byte-per-byte (displacement E9 ignorato), tutte le 320 sequenze da 18 byte in `a1[17]`, entry `a1[4]==a1[5]`, continuazione patchata `a1[5] -> a1[17]+18` e coda `a1[17] -> 0x15030D30`. Qualunque differenza torna all’interprete fail-closed.
- La traduzione C rispecchia ogni istruzione del fingerprint: il prefisso legge il primo byte, scrive il byte duplicato nella word e avanza; ognuna delle 319 iterazioni restanti legge il successivo, usa la word `(current<<8)|previous` come indice nella LUT EBP (byte risultato in AL), scrive AX e avanza EDI di due. Il controllo dello span sorgente (320 byte/row) e destinazione (640 byte/row), con delta di riga, avviene prima del loop. La LUT è controllata per 64 KiB. Il marker runtime conferma che il fingerprint è stato selezionato; non sono stati aggiunti altri kernel/ROP.

### `SmackWait`, skip video e risultato smoke

- `SmackWait` in `build/runtime-arm/shim-port/tools/elf/smack_elf.c` è polling, non una sleep: `return ((now-t0)*1000 < usf)`. Nel log il frame interval è `usf=0x10464` (66.660 ms), ma i primi delta osservati sono `0x5A7`, `0x5C7`, `0x5DA` ms, quindi Wait ritorna 0; non è l’attesa di 66 ms a spiegare il limite.
- `SC_SMACK=0` fa sì che `active()` sia falso e `SmackOpen` ritorni NULL. L’override #454/#457/#458 in `storm_ext.c` sostituisce un’apertura fallita con `VID_SKIP_HANDLE`, ma l’oggetto `storm_ext.c` è esplicitamente escluso dal link ARM (`tools/build-runtime-arm.sh`, fase 8), dove gli ordinali sono VA dirette. Pertanto su questo ELF non è un percorso di skip affidabile. `SC_SMACK_SKIP` e fast-forward non risultano implementati in `smack_elf.c`. Non ho modificato `storm_ext.c`, `smack_elf.c` o altri shim.
- Smoke finale del binario con `SC_SMACK=1`, SDL dummy e `timeout -k 2s 180s`: `build/qa/run-blt-fastpath-180-final.log`, durata riportata 182 s (timeout 180 + kill grace), exit 137. `blizzard.smk` (171 frame) termina senza crash; viene aperto `starXintr.smk` (0xD64/3428 frame), con audio inizializzato e buffer video aperto. Il log si ferma subito dopo `SmackToBuffer` del secondo filmato: nessun suo `DoFrame`/pixel non-zero registrato prima del timeout; nessun nuovo opcode SCode, STOP o API non implementata. Menu principale **non raggiunto**.
- `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-blt`: PASS (circa 0,9 s). `TMPDIR=/dev/shm make check`: PASS. SHA256 finale ELF: `32b141c9b94913f942b4f5668615f64a8c67d94701fb4fd0bc5aa9eb817a48c0`.

## Decisioni autonome (follow-up)

Ho mantenuto l’ottimizzazione limitata al fingerprint integralmente verificato e non ho usato `SC_SMACK=0` come scorciatoia: in questo build ARM l’override che rende sicuro lo skip non è linkato. Non ho introdotto una nuova semantica di skip/fast-forward in un altro shim senza richiesta specifica; quindi il test autorizzato da 180 s termina al setup del primo frame di `starXintr.smk`, non al menu.
