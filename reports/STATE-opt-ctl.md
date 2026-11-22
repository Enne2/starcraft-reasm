# STATE opt-ctl — control-file

## Fatto e verificato
- `runtime/shim-port/elf/host_elf.c`: fd persistente; read a blocchi da 512 B; righe parziali/overflow gestiti; backoff 250 ms; rilevati truncate e sostituzione inode.
- TU C11 compilato PASS per i686 e x86-64 (`-Wall -Wextra`); warning preesistente `g_uitrace_present` non usata.
- QA privato: `build/qa/opt-ctl/starcraft-x86-ctl`, linkato con nuovo `host_elf` object e oggetti build esistenti; nessun rebuild globale.
- Gameplay isolato con click: menu → Select Game Type → Registry. Screenshot: `build/qa/opt-ctl/screenshots/final-click-gametype.png`, `final-second-click-registry.png`.
- Test split buffer 700 B, rotazione e troncamento PASS: `split-buffer.png`, `rotation-reopen-after.png`, `truncate-restart.png` in `build/qa/opt-ctl/screenshots/`.
- Trace idle 4 s: zero `openat/read/lseek` osservati; baseline utente ~2400/3400/2400 chiamate/s. Senza `SC_CONTROL_FILE`, nessun riferimento `cmd.txt` nel trace.
- Runner `ctl-test` e `ctl-no-control` spenti; PID verificati assenti. `git diff --check` PASS.

## File di proprietà (modificati per questo task)
- `runtime/shim-port/elf/host_elf.c`
- `reports/REPORT-opt-ctl.md`
- `reports/STATE-opt-ctl.md`

## Comandi che funzionano
- Compilazione x86: `clang --target=i686-unknown-linux-gnu -m32 -std=c11 -Wall -Wextra ... -c runtime/shim-port/elf/host_elf.c -o build/qa/opt-ctl/host_elf-i386-c11.o`
- Compilazione host: `clang -std=c11 -Wall -Wextra ... -c runtime/shim-port/elf/host_elf.c -o build/qa/opt-ctl/host_elf-x86_64-c11.o`
- Runner isolato: `SC_BIN="$PWD/build/qa/opt-ctl/starcraft-x86-ctl" ./tools/run-isolated.sh start --id ctl-test --skip-intro 1 --smack 1`
- Idle trace: `timeout 4s strace -f -e trace=openat,read,lseek -o build/qa/opt-ctl/strace-final-idle.log -p <pid>`

## Aperto / limiti
- La baseline è quella misurata dall’utente; non ho eseguito una build pre-patch separata. La misura post è steady-state: non include l’unico `open()` iniziale.
- `fstat` per aggiornamenti/troncamento a ogni pump; `stat(path)` ogni 250 ms per rotazioni.
- Nessun commit/push.

## Decisioni autonome
- Verifica dell’identità del pathname ogni 250 ms per mantenere persistente il descriptor senza `open()` per pump; dettagli in `REPORT-opt-ctl.md`.
