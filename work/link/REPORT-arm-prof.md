# Profilo ARM menu3 — in corso

## Decisioni autonome
Piano operativo già approvato in sessione; eseguo nel perimetro indicato e uso `TMPDIR=/dev/shm` per build/link/test. Nessuna modifica prima di avere evidenza del percorso caldo.

## Evidenza iniziale
- Artefatto presente: `build/starcraft-armv6-menu3` (21.8 MB).
- Primo avvio errato: `qemu-arm-static` senza sysroot termina subito (`Could not open '/lib/ld-linux-armhf.so.3'`, exit 255); questo non misura il runtime. Il test corretto deve usare `-cpu arm1176 -L build/sysroot`.
- `runtime/arm/storm_sdraw_arm.c` contiene effettivamente `proc_range()` che apre/parsa `/proc/self/maps` a ogni validazione. È una causa candidata, non ancora attribuita come calda.
- `work/qemu/pbp/menu3-trace.log` è in `work/qemu/pbp/` (non nel path inizialmente ipotizzato).

## Misure / profilo
- Avviato il test corretto con `qemu-arm-static -cpu arm1176 -L ../sysroot`, `SC_SMACK=0`, SDL dummy, `SC_FPS_LOG=1`, `timeout 60s`. Al termine il PID QEMU non era più presente; exit code finale non è stato registrato dal wrapper. Il log `/tmp/menu3-host-baseline.log` mostra ripetutamente `GPU 0 fps` e non contiene evidenza di arrivo al menu né una riga di splash riconosciuta. Quindi questo test non stabilisce se il binario raggiunga il menu: dummy video/avvio molto lento possono influenzare il risultato.
- Campionamento preesistente `work/qemu/pbp/menu3-trace-proc.txt`: `starcraft-armv6` 86,4% CPU; worker `starcraft-armv6` 10,6%; syscall/stack non leggibili per `Operation not permitted` / `Permission denied`. Non contiene PC/LR né backtrace.
- Trace esistente `work/qemu/pbp/menu3-trace.log`: numerose chiamate `Wait(handle=0x5E000003, ms=250)`: `0x00000102` (timeout) e `0` (segnalato) sono entrambi visibili. Gli handle `0x5E00004E+` mostrano wait infiniti con risultato 0; la traccia conferma quindi che il worker/evento progredisce almeno in alcuni intervalli, ma non consente di identificare il produttore di `0x5E000003` né il peso CPU attribuibile ai timeout. La riga FPS tracciata oscilla intorno a 3–5.
- Tentato il campionamento GDB remoto con `/usr/bin/gdb` supportato e istanza QEMU `-g 127.0.0.1:12345`: QEMU termina con `Failed to bind socket: Permission denied`. Alternativa `-g unix:/tmp/sc-gdb.sock` termina con `Failed to bind socket ... No such file or directory`. Nessun PID QEMU resta attivo. Quindi non ci sono sample PC/LR/backtrace né una distribuzione numerica del PC; il campionatore suggerito `SIGPROF` non è stato aggiunto, perché il processo è QEMU user-mode x86_64 e il `ucontext` host non contiene il PC guest ARM da attribuire alle `loc_*`.
- IDA, database `build/lift-full/ida/storm.lift.dll.i64`, aperto in sola lettura e poi chiuso. La decompilazione di `storm_269` (entry 0x15016360; chiamata `CreateEventA` a 0x1501658F) mostra la creazione di evento manual-reset inizialmente non segnalato per la lettura MPQ buffered/asincrona, seguito da `WaitForSingleObject(INFINITE)` a 0x15016683. `sub_150167C0` (0x150167C0; `CreateEventA` 0x15016984, wait 0x15016A58) è un altro percorso di lettura buffered/asincrona. Nel trace, `CreateEvent #3` è proprio handle `0x5E000003`, auto-reset, inizialmente non segnalato; il contatore CreateEvent rende plausibile il legame col terzo evento creato dal percorso Storm, ma l’ordine temporale/callsite per quel singolo handle non è provato in modo conclusivo. Gli stessi percorsi Storm segnalano l’evento tramite il completamento di `sub_15014F70` (parametri finali includono l’evento sull’ultima richiesta); servono dati runtime per provare che il worker abbia mancato o ritardato la segnalazione.
- Il trace esistente mostra timeout `WAIT_TIMEOUT (0x102)` alternati a `WAIT_OBJECT_0 (0)` per `0x5E000003`. È comportamento di attesa bounded, non prova che il worker resti bloccato; la logica `WaitForSingleObject` nel shim usa correttamente condvar timed wait e ritorna 0x102 soltanto se non segnalato alla scadenza. Trace e `menu3-trace-proc.txt` non distinguono la quota CPU delle attese dalla quota nel guest.

## Campionamento guest in-process (21/11/2026)
Aggiunto profiler opzionale in `runtime/arm/game_main_arm.c`, abilitato solo da `SC_PROF` (`SC_PROF=wall` usa ITIMER_REAL/SIGALRM, altrimenti ITIMER_PROF/SIGPROF). Handler SA_SIGINFO registra PC/LR in hash statico e, a 1000 campioni, stampa top 20; `SC_PROF_EXIT=1` esce dopo la stampa. Ritardato il primo tick di 3 s CPU per collocare il campionamento dopo l’inizializzazione audio. Il ritardo e il campionamento sono strumentazione diagnostica temporanea.

Build `TMPDIR=/dev/shm ./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-prof`: PASS, link 362 ms, build totale 829 ms. Il log della run mostra `CreateSoundBuffer #3` prima di `[SC_PROF] samples=1000`. Uscita profilo 0.

Top 20 (1000 campioni; `pct` dal logger è in centesimi di percentuale, es. 6180 = 61,80%). L’immagine ELF porta segmenti di codice a 0x18000000-0x18A8D518, PE a 0x00401000-0x00500000 e Storm a 0x15001000-0x15051000. PC fuori da questi segmenti sono marcati non simbolizzati, non attribuiti per supposizione.

| # | PC | LR | hit | % | simbolo / mapping |
|---:|---:|---:|---:|---:|---|
|1|0x4093A184|0x00000000|618|61,80|fuori dai segmenti ELF linkati; non risolto da llvm-nm/addr2line|
|2|0x18A5386C|0x0207C438|18|1,80|`arm_va_is_mapped+0x44`|
|3|0x408A9E98|0x408A9E98|18|1,80|fuori dai segmenti ELF linkati; non risolto|
|4|0xFFFF0FE0|0x408A96A8|14|1,40|helper/vDSO o mapping esterno, non risolto dal binario|
|5|0x40874960|0x40899870|14|1,40|fuori dai segmenti ELF linkati; non risolto|
|6|0x40899870|0x40899870|13|1,30|fuori dai segmenti ELF linkati; non risolto|
|7|0x408A9DE8|0x408A9DE8|13|1,30|fuori dai segmenti ELF linkati; non risolto|
|8|0x40899884|0x40899884|9|0,90|fuori dai segmenti ELF linkati; non risolto|
|9|0x18A53884|0x0207C444|9|0,90|`arm_va_is_mapped+0x5C`|
|10|0x408CD04C|0x408CD04C|9|0,90|fuori dai segmenti ELF linkati; non risolto|
|11|0x408A96A8|0x408A96A8|9|0,90|fuori dai segmenti ELF linkati; non risolto|
|12|0x408E83D8|0x408CB6C0|8|0,80|fuori dai segmenti ELF linkati; non risolto|
|13|0x408CA3B8|0x408A9EB4|8|0,80|fuori dai segmenti ELF linkati; non risolto|
|14|0x408AA4C4|0x408A9EB4|7|0,70|fuori dai segmenti ELF linkati; non risolto|
|15|0x18A53878|0x0207C434|6|0,60|`arm_va_is_mapped+0x50`|
|16|0x18A5382C|0x18A50D84|6|0,60|`arm_va_is_mapped+0x04`|
|17|0x18A50D70|0x18A4D364|6|0,60|`cpu_llasm_read32`|
|18|0x408BADBC|0x408BADBC|5|0,50|fuori dai segmenti ELF linkati; non risolto|
|19|0x408AA058|0x408A9EB4|5|0,50|fuori dai segmenti ELF linkati; non risolto|
|20|0x408AB35C|0x408AAF30|5|0,50|fuori dai segmenti ELF linkati; non risolto|

**Interpretazione cauta:** il campionatore registra PC guest forniti da `ucontext` nel processo ARM; tuttavia 61,8% dei campioni è a `0x4093A184`, fuori da tutti i LOAD segment linkati e senza corrispondenza `llvm-nm`/`addr2line`. Non è dimostrato se sia codice condiviso caricato dinamicamente, una regione generata o un PC non interpretabile in questo contesto. Tra i PC risolti, `arm_va_is_mapped` totalizza 39 campioni (3,9%); non emerge `proc_range`, `storm_313_arm`, `storm_334_arm`, `host_elf_present`, né `pthread_cond_timedwait` nei Top 20. Questo campione quindi non autorizza a dichiarare `proc_range` hotspot. Il setitimer è process-wide e il sample non separa main thread e worker.

## Verifiche successive
- `TMPDIR=/dev/shm make check`: PASS (freestanding ARM/QEMU e slice tests).
- Run richiesta con `SC_PROF=0`, `SC_FPS_LOG=1`, SDL dummy e timeout 45 s su candidato: exit 137 a timeout; otto righe `[fps] GPU 0 fps`, nessuna schermata di menu rilevabile nel log. Il driver dummy non consente inferire il frame rate reale o il menu. La vecchia misurazione Pinebook Pro resta 3,66 FPS medio e splash statico, non è un A/B omogeneo.
- Eventi/IDA: l’analisi precedente resta nel report sopra; non si è provato che i timeout derivino da un worker difettoso.

**Fix funzionale non applicato:** il dato dominante non è risolto a un simbolo del candidato e l’unico helper identificato pesa 3,9%; cambiare `proc_range`/eventi senza mappare quel 61,8% potrebbe peggiorare il runtime. Codice modificato solo per aggiungere profiler opt-in; nessun fix di performance ancora dimostrato.

## Risoluzione del PC dominante con `/proc/self/maps`
Il reporter fuori dal signal handler ora stampa mapping e stack/register snapshot. In una nuova run il top PC è rimasto `0x4093A184` (636/1000 = 63,60%, run precedente 618/1000 = 61,80%). `/proc/self/maps` lo colloca nella mappatura RX della libc ARM:

```
40856000-409c2000 r-xp 00000000 .../build/sysroot/usr/lib/arm-linux-gnueabihf/libc.so.6
```

Base mapping 0x40856000, file offset 0; offset nel file 0x13A184. `llvm-objdump` su libc a 0x13A184 mostra `clnttcp_create+0x9A4`, istruzione `cmp r3,#4` (0x13A184), branch a 0x13A2D4; il controllo del flusso immediatamente circostante è un retry intorno a `recvfrom` (0x13A1A0). Quindi l’identificazione precisa del PC è **glibc `clnttcp_create`**, non `proc_range`, SDL o Storm. `0xFFFF0FE0` è distinto e non è il PC dominante.

**Chiamante non ancora attribuito.** `arm_lr` nel sample dominante è 0; il probe di saved LR a SP+100 non ha prodotto un indirizzo di codice stabile/valido (es. valori diversi fuori dai segmenti), dunque non lo presento come callsite. La routine `clnttcp_create` in una partita StarCraft è inattesa, ma senza un backtrace affidabile non è corretto dedurre da quale chiamata sia stata raggiunta o cambiare il runtime. Inoltre `ITIMER_PROF` è process-wide; va esclusa l’ipotesi che il sample ricada in un contesto secondario prima di attribuire alla logica del main loop.

Il profiler è stato ulteriormente aggiornato in `runtime/arm/game_main_arm.c` per dump fuori handler di `/proc/self/maps` e campi `arm_sp`/`arm_fp`; il tentativo `fseek(/proc/self/mem, sp+100)` è solo diagnostico e non fornisce un LR utilizzabile. Ultimo runtime-only build riuscito (circa 0,95 s). `TMPDIR=/dev/shm make check` ripetuto dopo l’aggiustamento diagnostico: PASS (test freestanding ARM/QEMU e slice tests). Nessun fix prestazionale è stato applicato.

## Correzione dell’offset e A/B `proc_range` (21/11/2026)
**Correzione:** il PC `0x4093A184` meno la base libc `0x40856000` fa `0x0E4184` (non `0x13A184`). In libc l’offset è `open+0xE4`; `0x408A9DE8 - 0x40856000 = 0x53DE8`, nel percorso `vfscanf`/`sscanf`. Il precedente mapping a `clnttcp_create` era errato: avevo confuso il base-offset durante la risoluzione. I frame stack che contenevano `local`, `00:0`, `0 rw` sono coerenti con parsing della riga `/proc/self/maps`. La causa calda è quindi `proc_range()` in `runtime/arm/storm_sdraw_arm.c`, che apriva e parsava `/proc/self/maps` a ogni validazione.

Fix applicato: snapshot/cache delle mappe, controllo in memoria su hit; solo miss/epoch change ricarica con `open`+`read` e parser esadecimale, senza `fopen`/`sscanf`. `VirtualFree` non fa `munmap`: `MEM_RELEASE` e `MEM_DECOMMIT` eseguono `madvise` + `mprotect(PROT_NONE)`; `VirtualProtect` può cambiare i permessi. Perciò `g_vm_generation` viene incrementata dopo VirtualAlloc/Free/Protect riuscite e invalida la cache. La prima capacità 512 non bastava (snapshot arrivava a 511 entry e falliva chiuso); portata a 4096.

**A/B osservato:** prima della cache, profilo da 1000 campioni: `open+0xE4` ~61,8–63,6%; `vfscanf` ~4% (dato di risoluzione PC riportato sopra). Dopo la cache, la run con `REASM_TRACE=1` non ha più errori `[storm-map]` né `[storm-blt] surface non leggibile`; procede oltre il controllo che prima terminava con exit 73 e produce molte più operazioni VirtualAlloc/VirtualFree. La run senza trace/profilo raggiunge poi un SIGSEGV prima di completare il campionamento, quindi non ho una distribuzione Top-20 post-fix né FPS/menu post-fix.

Dettaglio crash `build/starcraft-armv6-fast` (`SC_PROF=1`, dummy): dopo `CreateSoundBuffer #3`, prima del dump del profiler:
- segnale: SIGSEGV; fault address `0x00056F24`; fault PC `0x00056F24`; LR `0x18A8303C`;
- registri guest riportati: EIP `0x0000003F`, ESP `0x021F9F3C`, EAX `0x021F9F58`, EDX `0x006CEF3C`;
- il logger non ha stampato un nome di procedura tradotta per questo EIP anomalo. Il fault è fuori dal codice di `proc_range` (PC `0x56F24`, mentre il codice runtime è nella regione `0x18...`): è un nuovo fault nel guest/percorso successivo alle chiamate audio, non un crash della cache. Il log non prova che sia già stato disegnato lo splash: non dichiaro superato Loading né raggiunto il menu.

La cache è dunque effettivamente oltre il vecchio failure point, ma l’obiettivo non è ancora validato: occorre diagnosticare il nuovo guest fault prima di poter quantificare il frame rate/risultato visivo. `make check` PASS eseguito sul candidato; build e test dell’ultimo oggetto profiler non equivalgono a un profilo completato.
