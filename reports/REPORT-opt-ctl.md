# Report — ottimizzazione `SC_CONTROL_FILE`

## Esito
Aggiornato il reader live in `runtime/shim-port/elf/host_elf.c`: descriptor persistente, letture da 512 byte con buffer e parsing incrementale delle righe, retry con backoff di 250 ms se il file non è disponibile, rilevamento di troncamento e sostituzione per identità device/inode. `ctrl_pending` e i ritardi di rilascio mouse/tasto restano invariati.

Per rilevare byte nuovi senza chiamare `read()` a ogni pump, il codice usa `fstat()` sulla dimensione del descriptor; verifica il pathname con `stat()` ogni 250 ms per rilevare rename/rotazione. Questa verifica pathname è necessaria perché `fstat()` sul descriptor persistente da solo non rileva la sostituzione dell’inode. In assenza di `SC_CONTROL_FILE`, il ramo non effettua queste syscall.

## Misure syscall
Dati precedenti forniti dall’utente durante gameplay: circa **2.400 `openat`/s, 3.400 `read`/s e 2.400 `lseek`/s**, quasi tutti sul `cmd.txt` (baseline misurata dall’utente; non ricampionata su una build sorgente precedente).

Dopo, con binario QA privato contenente il nuovo TU, in una finestra idle di 4 s su PID 3153775:

| Syscall filtrate | Baseline fornita | Dopo, steady-state |
|---|---:|---:|
| `openat` | ~2.400/s | 0 osservate |
| `read` | ~3.400/s | 0 osservate |
| `lseek` | ~2.400/s | 0 osservate |

Comando e log: `timeout 4s strace -f -e trace=openat,read,lseek -o build/qa/opt-ctl/strace-final-idle.log -p 3153775`; `grep -Ec '(openat|read|lseek)\(' ...` restituisce **0**. Il descriptor resta aperto fra i pump; in idle il controllo si limita a `fstat()` per dimensione e a `stat(path)` ogni 250 ms per l’identità. Il `read()` da 512 byte avviene solo se la dimensione è cresciuta oltre l’offset prefetched. Il valore 0 è per la finestra steady-state, non include l’unico `open()` iniziale.

## Verifiche
- **Compilazione TU C11:** PASS per i686 e x86-64 con `-Wall -Wextra`; oggetti ELF32/ELF64 in `build/qa/opt-ctl/`. Rimane un warning preesistente per `g_uitrace_present` non usata.
- **Nessun rebuild globale:** creato `build/qa/opt-ctl/starcraft-x86-ctl`, link QA privato dagli oggetti già presenti sostituendo il solo oggetto `host_elf`; il binario globale `build/starcraft-x86` non è stato ricostruito.
- **Input reale:** avvio con `./tools/run-isolated.sh start --id ctl-test --skip-intro 1 --smack 1`, poi `focus`, `move 220 120`, `click 220 120`; screenshot `build/qa/opt-ctl/screenshots/final-click-gametype.png` mostra **Select Game Type**. Il successivo `move 260 226` + `click 260 226` apre Registry (`final-second-click-registry.png`).
- **Buffer/riga spezzata:** commento di 700 byte seguito da comandi validi; il click Cancel viene elaborato e ritorna al menu (`split-buffer.png`).
- **Rotazione:** rinominato `cmd.txt`, creato un nuovo inode al pathname e inviati move/click; il reader riapre e il click raggiunge Registry (`rotation-reopen-after.png`).
- **Troncamento:** portato l’offset oltre 700 byte, troncato `cmd.txt` e inviato un click; il reader riparte dall’inizio e avanza alla selezione episodio (`truncate-restart.png`).
- **Senza variabile:** avvio via wrapper QA che rimuove `SC_CONTROL_FILE`; `/proc/<pid>/environ` conferma l’assenza e il trace `open,openat,read,lseek,newfstatat,fstat` non contiene riferimenti a `cmd.txt`.
- I runner `ctl-test` e `ctl-no-control` sono stati fermati e verificati inattivi. `git diff --check -- runtime/shim-port/elf/host_elf.c` passa.

## Decisioni autonome
Per riconoscere una sostituzione senza riaprire il pathname a ogni pump, uso `stat(path)` ogni 250 ms confrontando `st_dev/st_ino` con l’identità acquisita da `fstat(fd)`. Per troncamento controllo `st_size` contro offset completo e offset prefetched; al reset scarto buffer e riga parziale. Le righe oltre 255 byte continuano a conservare i primi 255 byte, come il parser precedente.

Ho preservato le modifiche non correlate già presenti nel working tree. Non ho modificato `snp_elf.c`, `win32_shim.c`, tool o il binario globale.
