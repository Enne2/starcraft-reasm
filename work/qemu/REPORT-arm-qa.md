# QA ARM nel guest QEMU — dialogo errore MPQ

## Scopo e vincoli
Determinare il testo reale del dialogo eseguito nel guest con `build/starcraft-armv6-dlg2`, senza input sintetico. Variabili d'ambiente richieste: `DISPLAY=:0 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy` (possono alterare il comportamento osservabile). Nessun `sudo` host, passt, modifica a runtime/ o tools del port, né operazioni git. Il guest usa l'overlay `build/qemu/runtime/guest-lite.qcow2`; l'immagine base `build/qemu/lite/lite.img` non va modificata.

## Esecuzione in corso

### 2026-11-20 — preflight
- Host: frigate, Fedora Linux 44; timestamp locale verificato con `date`.
- VM: `qemu-system-arm` PID 542755 attivo. Command line indica guest `raspi0` e disco overlay `build/qemu/runtime/guest-lite.qcow2`, SSH inoltrato solo a `127.0.0.1:2222`.
- `build/qemu/ssh/gssh pi@127.0.0.1` ha risposto, inizialmente: `systemctl is-active minimal-x` → `active`; `df -B1 /` → disponibili 171085824 byte.
- In `/home/pi/sc` presenti `Starcraft.mpq` (606857006 B), `StarDat.mpq` (66184568 B), `patch_rt.mpq` (1086853 B). Una prima verifica `sha256sum -c MANIFEST-assets.sha256` è stata avviata ma il client SSH è scaduto dopo 20 s e non ha restituito esito completo; da ritentare con timeout adeguato.
- Candidato selezionato su indicazione del collega: `build/starcraft-armv6-dlg2`; SHA-256 host verificato: `744800d90f506bdc2b76da0081ba6955cce0e118d7dcd156a42f22ef42b3d3b5`.
- Immagine base e overlay identificati: `build/qemu/lite/lite.img` SHA-256 `acf39427d33c997fc2bbc237cc48a30f3b00c9cd60b831d67fedc9e3205fa816`; `build/qemu/runtime/guest-lite.qcow2` SHA-256 `fe719fcb98c856983f40341aaa0367637cc5ea0722ee9f7ffc8df59730400f02` (letture, nessuna modifica intenzionale alla base).
- Verifica pre-lancio successiva (20 novembre, circa 17:34 CET): hash nel guest del binario `744800d90f506bdc2b76da0081ba6955cce0e118d7dcd156a42f22ef42b3d3b5`, identico all'host. `sha256sum -c /home/pi/sc/MANIFEST-assets.sha256` ha dato `OK` per `patch_rt.mpq`, `StarDat.mpq` e `Starcraft.mpq`; `minimal-x` ancora `active`; filesystem guest con 150233088 byte disponibili. Il binario è stato copiato con `build/qemu/ssh/gscp` in circa 92 s (17:32:44–17:34:16 host).
- Avvio richiesto eseguito da `/home/pi/sc` con `DISPLAY=:0 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy`. Timestamp di avvio registrato nel guest: `2026-11-20T16:37:23.874319477+00:00` (= 17:37:23 CET). Log catturato integralmente in `build/qemu/runtime/dlg2-stdout-stderr.log` (833 B); copia originale guest: `/home/pi/sc/dlg2.stdout-stderr.log`.
- Il processo non è rimasto attivo. Prima delle routine MPQ/dialogo si arresta su API non implementata: `GetModuleHandleA` del modulo assente `C:\home\pi\sc\local.dll`; stop controllato dal port (chiamante `0x189FB55C`). La cartella guest contiene i tre MPQ e l'eseguibile ma `local.dll` non è presente. Lo screenshot è stato catturato senza input circa 20 s dopo l'avvio.
- Screenshot QMP: `build/qemu/runtime/dlg2-screen.png` (800×480, SHA-256 `a566ea07ec1f2f18c17bced9c57aa7e241fc4ef16dcfc4613bef82ea4c4c8954`). Mostra il terminale guest, nessun dialogo. Titolo della finestra visibile: `StarCraft ARMv6` (titolo del terminale, non di un dialogo).
- Processo QEMU guest rimasto attivo dopo il tentativo; `minimal-x` ancora `active`; nessun input sintetico inviato.

## Risultato
**Test bloccato prima della verifica del messaggio MPQ.** Testo esatto di errore visibile a schermo: nessuno; dialogo non comparso, quindi non è verificabile se l'errore `Stardat.mpq / unknown error 0x00000002` sparisca con `StarDat.mpq` reale. Il log termina con:

```text
[reasm-arm] GetModuleHandleA di un modulo non presente: C:\home\pi\sc\local.dll
[reasm-elf] API NON IMPLEMENTATA: C:\home\pi\sc\local.dll  (chiamante 0x189FB55C) -> stop controllato
```

I messaggi iniziali completi sono in `build/qemu/runtime/dlg2-stdout-stderr.log`. La condizione di arresto è già documentata per il candidato successivo: risolvere prima il percorso/API di `local.dll` (collega evt) e poi ripetere sullo stesso guest, senza input. Tempo dall'avvio al primo screenshot che conferma l'uscita: circa 20 s; il dialogo non è comparso. Variabili d'ambiente usate e screenshot registrati sopra.

## Follow-up: Local.dll nel guest — 2026-11-20

- Richiesto trasferimento degli asset dal sorgente read-only `~/dev/final-starcraft/Starcraft/`. Verificati sul host: `Local.dll` 52224 B, SHA-256 `597e78f855a2853602e3dde0876a92849c3a5b6679e4003a46eba4f1d1dada43`; `EditLocal.dll` 92672 B, SHA-256 `e3e91cffef9e181356dedb03a2156f567ad092aa38a0bf64e7fa9f318e7537ef`.
- QEMU (PID 542755) è rimasto attivo; screenshot QMP preflight `work/qemu/preflight-20261120-1742.png` (800×480; SHA-256 `a566ea07ec1f2f18c17bced9c57aa7e241fc4ef16dcfc4613bef82ea4c4c8954`) mostra ancora il terminale guest. Nessun input sintetico.
- SSH su `127.0.0.1:2222` non ha risposto nei tentativi con timeout di 20 s, 150 s e 90 s (17:41–17:46 CET). `ss` mostrava la porta 2222 in `CLOSE-WAIT` gestita da QEMU; il processo QEMU era ancora presente. Perciò non è stato possibile verificare di nuovo spazio/servizi guest né trasferire `Local.dll`, `local.dll` o `EditLocal.dll`. Non è stato rilanciato il gioco e non è stato scritto nel guest.
- Stato follow-up: **bloccato dalla connettività SSH del guest**, non da un nuovo arresto del processo QEMU. Nessun nuovo dialogo o testo verbatim da riportare. Gli SHA-256 e le dimensioni delle DLL sorgenti sono pronti; riprendere solo quando SSH risponde, verificare spazio prima/dopo copia, installare entrambe le varianti di case di Local.dll e EditLocal.dll, quindi avviare `dlg2` con le tre variabili SDL indicate e salvare screenshot/log in `work/qemu/` (senza toccare `runtime/`).

## Follow-up: reboot pulito, DLL e dialogo MPQ — 2026-11-20

### Recupero guest
- Verificato `/proc/542755/cmdline`: QEMU era il guest StarCraft `-machine raspi0`, con overlay `build/qemu/runtime/guest-lite.qcow2`, SSH hostfwd loopback `127.0.0.1:2222`, VNC `127.0.0.1:1`. Il PID indicato dal vecchio `build/qemu/vnc-viewer.pid` (3099590) era inesistente; nessun processo `vncviewer` attivo, quindi nessun viewer orfano da terminare.
- Inviato `kill -TERM 542755`; processo terminato pulitamente. Riavvio usando i parametri di `CHECKPOINT.md` §8 (`tools/qemu/run.sh`, `SSH_FWD=1`, overlay `guest-lite.qcow2`, `QEMU_DISPLAY=none`, `-vnc 127.0.0.1:1 -k it`). `build/qemu/lite/lite.img` non è stato usato come disco scrivibile né modificato. Nuovo PID QEMU 1422649. Avviato `tools/qemu/vnc-viewer.py 127.0.0.1 5901 'StarCraft ARMv6 QA'` (PID 1425877; connesso); nessun input inviato.
- SSH è tornato disponibile; `minimal-x` = `active`. Spazio prima delle DLL: 150216704 B. Dopo copie: 150011904 B (delta 204800 B). Hash guest: `Local.dll` e `local.dll` entrambi `597e78f855a2853602e3dde0876a92849c3a5b6679e4003a46eba4f1d1dada43`; `EditLocal.dll` `e3e91cffef9e181356dedb03a2156f567ad092aa38a0bf64e7fa9f318e7537ef`. Verificati nuovamente con `sha256sum -c MANIFEST-assets.sha256`: `patch_rt.mpq`, `StarDat.mpq`, `Starcraft.mpq` tutti `OK`.

### Esecuzione `dlg2`
- Eseguito da `/home/pi/sc` il binario `starcraft-armv6-dlg2` (SHA-256 `744800d90f506bdc2b76da0081ba6955cce0e118d7dcd156a42f22ef42b3d3b5`) con `DISPLAY=:0 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy`. Avvio guest `2026-11-20T17:02:55+00:00`. Il log conferma caricamento risorse di `/home/pi/sc/local.dll` e creazione finestra X11; supera dunque il precedente stop su `GetModuleHandleA`/Local.dll.
- **Non compare un dialogo `Stardat.mpq / error 2`.** Il template del dialogo 0x6A nel log è `arr\\mapdata.dat` seguito da `unknown error 0x00000018`. La finestra grafica X11 è visibile nello screenshot QMP `work/qemu/dlg2-rerun-screen2.png`, catturato alle 18:03:48 CET, circa 53 s dopo l'avvio. Titolo finestra: `Data File Error`. Testo visibile (a capo come reso):

```text
Data File Error
StarCraft was unable to open a required file. If this problem
persists, try uninstalling and reinstalling StarCraft using
unknown file
[OK]
```

- Log stdout/stderr completo: `work/qemu/dlg2-rerun.stdout-stderr.log` (SHA-256 `11b8227975262a81b0abb77ed1d14f7a2f0a99c782db0fbe6add7b0c7b9c6c8f`). Screenshot zoom di supporto: `work/qemu/dlg2-rerun-screen2-zoom.png`; SHA-256 screenshot originale `be314c42e7955e6544afb0688870979712081fd2f20da24b176f6cc9609ad1d0`.
- A controllo finale il processo guest `./starcraft-armv6-dlg2` (PID 590) era ancora attivo in modal loop; log termina con `[dlg] WM_INITDIALOG delivered; entering modal loop` e `[host] present: percorso GPU (renderer + texture)`. `minimal-x` attivo, SSH operativo, spazio libero 147890176 B. Nessun tasto/click sintetico inviato; dialogo lasciato aperto per il prossimo candidato di `evt`.

## Follow-up: prerequisiti QA, helper e candidato `win` — 2026-11-20

### Guest pulito e dipendenze
- Prima di terminare il vecchio processo ho eseguito `ps -p 590 -o comm=` nel guest: valore esatto `starcraft-armv6`; solo allora ho inviato `kill -TERM 590`. Il processo è terminato entro 1 s. `minimal-x` è `active`; il client `tools/qemu/vnc-viewer.py` è vivo sull'host (PID 1425877, connesso al VNC loopback :5901). Spazio disponibile prima dei nuovi test: 147890176 B (oltre la soglia richiesta).
- Eseguito `ldd /home/pi/sc/starcraft-armv6-dlg2` nel guest. Tutte le dipendenze risolvono; inclusa `libSDL2-2.0.so.0 => /lib/arm-linux-gnueabihf/libSDL2-2.0.so.0`; nessuna riga `not found`. Output completo: `work/qemu/dlg2-cleanup-ldd-prep.log`.

### Helper QA
- Creati `tools/qemu/qa-perf.sh` e `tools/qemu/qa-deploy.sh` (eseguibili, `bash -n` PASS). Copiato `qa-perf.sh` nel guest in `/home/pi/sc/qa-perf.sh`; nessun file sotto `runtime/` modificato.
- `qa-perf.sh sample PID SECONDS INTERVAL CSV` legge `/proc/PID/stat` (delta `utime+stime`, RSS pages) e scrive CSV; test nel guest con `sleep` PID 835, tre campioni a 1 s: CPU 0.00%, RSS 1848 KiB. `qa-perf.sh parse LOG` analizza le righe `[fps] N fps, present medio X ms, ...` e calcola p95 nearest-rank su FPS e medie di present al secondo. Fixture a 4 righe testata nel guest: 4 campioni, `fps_p95=60.000`, `present_ms_p95=33.300` (esplicitamente non p95 per-frame).
- `qa-deploy.sh FILE [REMOTE_PATH] [TARGET]` usa `gzip -c | gssh ... "gunzip -dc > temp; chmod mode; mv"`, preserva i permessi e verifica SHA-256 host/guest. Smoke test di un fixture da 64 KiB: PASS; deploy completo di `dlg2` (20.850.856 B) e poi di `win` verificato bilaterale. Un primo invio ha superato il timeout di 180 s; il retry è riuscito in 35,7 s. Un test successivo con preservazione mode ha trasferito `win` in altri 35,7 s. Il `scp` iniziale di `dlg2` era durato 92 s. I binari in guest risultano eseguibili (`0755`). Fixture temporanei rimossi.

### Candidato successivo e gate di esecuzione
- `work/link/REPORT-arm-dlg.md` documenta `build/starcraft-armv6-win` e un run host che supera `CreateWindowExA`/WndProc e `ShowWindow`, poi termina con SIGSEGV durante caricamento DirectDraw, prima del menu. Il report registra SHA-256 `308519cb61dc8f1b7f8d91ab1fa50a936819152a7e48546c6880956cb72c3dc0`.
- L'artefatto corrente host `build/starcraft-armv6-win` è stato verificato e staged nel guest come `/home/pi/sc/starcraft-armv6-win`; hash bilaterale attuale `17db74cadd0c9b848cea9316f65961b90e4305c6a8faf3a7207a412a8e5aad78`, size 20856344 B, mode 0755. **Il suo hash non coincide con quello registrato nel report** (il file host è stato scritto dopo il report); perciò non ho eseguito questo artefatto nel guest né attribuito ad esso il successo documentato di `CreateWindowExA`.
- **Richiesta al supervisore nel report:** confermare se `17db74…` è il successore intended e la sua evidenza di build/test/hash, oppure fornire il binario che corrisponde a `308519…`. In attesa della conferma, il candidato resta solo staged e la VM non esegue il gioco. Stato guest attuale: `minimal-x` attivo, oltre 100 MB liberi dopo il deploy (~127025152 B), `qa-perf.sh` presente; nessun input sintetico.

## Follow-up: candidato COM nel guest — 2026-11-20

### Cleanup e deploy
- Verificato che non ci fosse un altro gioco in esecuzione. Rimossi solo `/home/pi/sc/starcraft-armv6-dlg2` e `/home/pi/sc/starcraft-armv6-win`; `minimal-x` è rimasto `active`. Spazio: 127025152 B prima della rimozione, 168734720 B dopo; il vecchio binario `win` (mode 0755) non è stato eseguito.
- Hash host verificato prima del deploy: `build/starcraft-armv6-com` SHA-256 `4b5cc9f9ec3690ef8947d1bb8af16abdb7be8b66b3c4ae62a8b93decfa0ab722`. Usato `tools/qemu/qa-deploy.sh build/starcraft-armv6-com /home/pi/sc/starcraft-armv6-com`; SHA-256 guest identico, trasferimento compresso completato in 33,3 s. Dopo il deploy: 147836928 B liberi.

### Avvio e osservazioni
- Avvio da `/home/pi/sc` con tutte le variabili richieste: `DISPLAY=:0 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy SC_FPS_LOG=1 SC_SMACK=0`. `SC_SMACK=0` disabilita filmati/animazioni perché `libsmacker` ARM non è presente; questa modifica del comportamento osservabile è esplicita.
- Avvio guest registrato `2026-11-20T17:33:24+00:00`. Il primo comando SSH di lancio è andato in timeout dopo 60 s per la shell remota in background, ma il processo è rimasto attivo; stato verificato poi come PID 1073, `comm=starcraft-armv6`. Nessun file `com.exit` al controllo finale: **non è uscito con codice 0 né è uscito rapidamente**.
- Log verbatim completo: `work/qemu/com-stdout-stderr.log` (2224 B). Il run carica `local.dll`, crea una finestra X11 e poi registra il template dialogo 0x6A: `arr\\mapdata.dat` + `unknown error 0x00000018`, quindi `WM_INITDIALOG delivered; entering modal loop` e `[host] present: percorso GPU (renderer + texture)`. Il log `[fps]` contiene 12 righe `GPU 0 fps, viewport 640x480, finestra 640x480`; nessun frame presentato. Parser aggiornato per questa variante di formato e risultato salvato in `work/qemu/com-fps-parse.txt`: `samples=12`, `fps_median=0`, `fps_p95=0`, `present_ms_samples=0` (il formato GPU non contiene `present medio`).
- Screenshot QMP catturati senza input a +75 s (`work/qemu/com-elapsed-75s.png`), +136 s (`...-136s.png`), +146 s (`...-146s.png`) e +161 s (`...-161s.png`). Tutti identici (SHA-256 `9f6ae5554361cf4a85b84e37aade6fdc9ebb0bedc837419fe26e6fb1da829923`): desktop/frame X11 nero con terminale, nessun menu/splash/frame visibile. Le catture sono partite più tardi del piano +10/+25/+45 perché l'invocazione SSH del launch ha impiegato/atteso il timeout; tempi reali riportati, non retrodatati.
- **Esito guest:** nessun menu e nessun frame; schermo nero durante i campioni, log segnala 0 FPS; il processo resta vivo nel modal loop con template `arr\\mapdata.dat / unknown error 0x00000018`. Non è stato avviato `qa-perf.sh sample` da 60 s perché il prerequisito indicato (“disegna il menu”) non si è verificato; il parser FPS è stato comunque eseguito sul log reale. Nessun tasto/click simulato. VM, `minimal-x`, SSH e viewer VNC restano attivi; spazio libero finale 147836928 B. Il binario COM verificato resta in `/home/pi/sc/starcraft-armv6-com` per il prossimo debug.

## Follow-up: micro-test BrooDat e deploy guest condizionale — 2026-11-20

### Prova isolata host
- Prima di copiare altri asset ho verificato le fonti e creato test dirs in `/tmp/sc-3mpq-test/` con soli symlink (read-only) ai tre MPQ originali e `Local.dll`/`local.dll`; varianti separate con `BrooDat.mpq` + alias `Broodat.mpq`, con `SEXP_IX86_1.18.0.1345.mpq`, e con `Smackw32.dll`.
- `BrooDat.mpq`: 23847431 B, SHA-256 `8c2c8ad5b32d4caf05d9e9d6727f46872ce6fc54f2e69ad6d6364cd1ea58c3c4`. `SEXP_IX86_1.18.0.1345.mpq`: 2303880 B; `Smackw32.dll`: 95232 B.
- Tutte le prove usano `ulimit -c 0`, `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SC_SMACK=0 REASM_TRACE=1`, `timeout -k 2s 8s qemu-arm-static -cpu arm1176 -L build/sysroot build/starcraft-armv6-com`.
- **Solo i tre MPQ originali:** exit 137 (timeout seguito dal kill dopo la grace di 2 s); il log mostra `BroodWar.mpq` e `Broodat.mpq` mancanti (`errno=0x00000002`) e il dialogo esatto `[dlg] template=0x6A text="arr\\mapdata.dat\nunknown error 0x00000018" target="arr\\mapdata.dat"`.
- **Aggiungendo soltanto BrooDat.mpq + symlink case alias Broodat.mpq:** exit 0 prima degli 8 s; `Broodat.mpq` si apre (fd 0x6) e il dialogo `arr\\mapdata.dat / 0x18` non compare. Non c'erano né SEXP né Smackw32.dll in questa variante. Dunque BrooDat è sufficiente a far sparire l'errore nel micro-test; SEXP e Smackw32 non sono necessari per questo risultato. Le varianti con SEXP e con Smackw32 (entrambe con BrooDat) hanno anch'esse exit 0.
- Log conservati: `work/qemu/only-3mpq.log`, `with-broodat.log`, `with-broodat-and-sexpi.log`, `with-smack.log`; codici in file `.exit` omonimi. Nessun MPQ originale o directory `build/asset-sandbox` è stato modificato.

### Guest: blocco SSH, deploy non confermato
- QEMU PID 1422649 è rimasto attivo. Ultimo controllo SSH che ha restituito dati (circa 18:51 CET): `ps -p 1073` mostrava `starcraft-armv6` PID 1073 in stato `Sl`; `systemctl is-active minimal-x` aveva timeout, `df` restituiva 147836928 B liberi.
- Inviati tentativi di `ps -p 1073 -o comm=` seguito da `kill -TERM 1073` con verifica della comm (`starcraft-armv6`, nome troncato da Linux); non ho ricevuto risposta entro 120/180 s. I trace `sshd` in `build/qemu/runtime/serial.log` mostrano autenticazione/sessione molto ritardata. Il tentativo host `qa-deploy.sh` per BrooDat ha superato 300 s; il client SSH figlio è stato identificato dalla command line che puntava unicamente a `/home/pi/sc/BrooDat.mpq` e terminato con TERM. Un upload parziale/temp remoto potrebbe essere stato creato: non è stato possibile verificare né rimuoverlo via SSH.
- Console seriale rilevava `raspberrypi login:`; il tentativo di login `serial-cli.py login pi raspberry` ha dato `prompt login: ok`, `login: da verificare`, senza restituire una shell verificata. Non ho eseguito altri comandi seriali, né inviato input alla finestra del gioco.
- **Stato incompleto e da riprendere:** nessuna verifica hash guest di `BrooDat.mpq`, nessun symlink guest confermato e nessuna conferma che PID 1073 sia terminato. In una sessione con SSH operativo: verificare `ps -p 1073 -o comm=`; terminare solo se corrisponde al gioco; controllare `df -B1 /` e `minimal-x`; ispezionare/rimuovere eventuale `BrooDat.mpq.qa-tmp-*`; poi ripetere `qa-deploy.sh` per il file da 23847431 B, verificare SHA-256, creare `Broodat.mpq -> BrooDat.mpq` e ricontrollare spazio. **Non copiare BroodWar.mpq né SEXP**: la prova host dimostra che BrooDat da solo basta a sopprimere il dialogo. Non è stato riavviato QEMU né modificato `runtime/`.

## Follow-up: recovery guest e preparazione asset — 2026-11-20 19:33 CET

La sezione precedente descriveva correttamente il blocco SSH/deploy alle 19:19. La procedura richiesta è poi stata completata:

- Dopo un nuovo timeout SSH da 120 s, verificata `/proc/1422649/cmdline`: QEMU usava `-drive file=.../build/qemu/runtime/guest-lite.qcow2,format=qcow2,if=sd`; inviato `SIGTERM` al solo PID 1422649 e verificata la scomparsa in 2 s. Il log `build/qemu/launch.log` registra la terminazione con signal 15. Il viewer orfano verificato come PID 1425877 (`tools/qemu/vnc-viewer.py`) è stato chiuso con TERM in 2 s.
- Riavvio eseguito con la procedura di `CHECKPOINT.md` §8: `tools/qemu/run.sh`, `SSH_FWD=1`, `QEMU_DISPLAY=none`, VNC `127.0.0.1:1`, disco di lavoro `build/qemu/runtime/guest-lite.qcow2`. Nuovo QEMU PID **2120405**; SSH host-forward loopback `127.0.0.1:2222` operativo. Viewer GTK/GtkVnc locale `tools/qemu/vnc-viewer.py` PID **2123834**, connesso a `127.0.0.1:5901`. Screenshot finale senza input: `work/qemu/guest-ready-2026-11-20.png`.
- SSH recuperato: il vecchio PID 1073 non esiste più dopo il riavvio; nessun `starcraft-armv6-com`, `-dlg2` o `-win` in esecuzione. Nessun binario del gioco è stato avviato. La ricerca dei temporanei `BrooDat.mpq.qa-tmp-*` prima del deploy non ha trovato residui.
- `/home/pi/sc/BrooDat.mpq` trasferito con `tools/qemu/qa-deploy.sh`, 23847431 B, SHA-256 host/guest identico `8c2c8ad5b32d4caf05d9e9d6727f46872ce6fc54f2e69ad6d6364cd1ea58c3c4`. Creato e verificato symlink `Broodat.mpq -> BrooDat.mpq`.
- `/home/pi/sc/libsmacker-1.2.0.so` trasferita e hash host/guest verificato: `41523b9f24d395f092cd3b7a13d1cef180647eafaf2fcd18daa0f9d74bde3705`. La sorgente è **39224 B** (non ~130 KB); `file`/`readelf -A` confermano ELF32 ARM EABI5, `Tag_CPU_arch: v6`, Thumb-1.
- Verifica finale guest: `/home/pi/sc` usa 686 MB; `df -B1 /` segnala **123936768 B liberi**. `minimal-x` è `active`; alias e dimensioni target verificati. `BroodWar.mpq`, SEXP e temporanei `.qa-tmp-*` assenti.
- La command line QEMU conferma che il disco attivo è l'overlay qcow2, con `lite.img` come backing file. Nessun comando di questa procedura ha scritto intenzionalmente su `lite.img`; `qemu-img info --force-share` la identifica come backing e il relativo file descriptor QEMU è aperto read-only. **Nota da non occultare:** `stat` ha mostrato `lite.img` mtime/ctime `19:28:01` (una precedente lettura pre-riavvio alle 19:25 riportava mtime `17:55:40`); causa del cambio non determinata in questa sessione e non è disponibile un hash baseline per attestare l'integrità dei contenuti. Il file non è stato aperto da strumenti di scrittura né modificato deliberatamente dall'agente.
- VM pronta per il successivo candidato: SSH, `minimal-x` e viewer attivi; nessun vecchio gioco in esecuzione; BrooDat e libsmacker presenti. Nessuna interazione tastiera/mouse inviata al guest.
