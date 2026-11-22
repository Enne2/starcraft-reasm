# REPORT-arm-pathevt — chiusura path/eventi MPQ

Stato: **path MPQ e pool eventi corretti; logging quiet by default. Avvio supera Data File Error ma il run termina a un crash guest successivo.**

## Modifiche (solo file assegnato)
`build/runtime-arm/shim-port/tools/elf/win32_shim.c`:
- `reasm_win_path()` converte in path relativo quelli sotto la cwd effettiva. In QEMU `-L sysroot`, i path guest assoluti host-like venivano risolti sotto il sysroot e non nella cwd host.
- `reasm_open_ci()` gestisce anche path relativi a partire da `.`; il lookup case-insensitive ora risolve `Stardat.mpq` → `StarDat.mpq`.
- Eventi dinamici con handle monotoni, nessun handle finto, e refcount con distruzione pthread differita mentre Set/Reset/Wait mantengono riferimenti.
- Tutti i log `[evt]`, `[file]`, `[attr]`, `[file-open]` sono gated da `g_trace` o `REASM_TRACE` letto lazy. `win32_shim_init()` viene ora chiamata da `game_main_arm.c` (modifica dell'altro agente).
- Nessuna modifica a `mpq_diag.c`, script, `game_main_arm.c`, `dlg_elf.c` o `runtime/arm/patches/`.

## Verifiche
- `make check`: PASS; log `build/qa/make-check-pathevt.log`.
- Run da `build/asset-sandbox`, `ulimit -c 0`, `SDL_VIDEODRIVER=dummy`, QEMU ARM1176, candidato `starcraft-armv6-pathevt-clean`.
- Con `REASM_TRACE=1`, `build/qa/run-pathevt.log` mostra cwd reale `/home/enne2/dev/starcraft-armv6/build/asset-sandbox`. `CreateFileA` e `GetFileAttributesA` riportano i path guest e tradotti; `Stardat.mpq` risolve a `StarDat.mpq` e apre fd 3. Anche `patch_rt.mpq`, `BroodWar.mpq`, `Broodat.mpq` aprono con fd validi.
- Eventi nel run: 65 creati (log limitato ai primi 17), 62 chiusi, 124 attese tutte `WAIT_OBJECT_0`, nessun `WAIT_FAILED`. L'implementazione non contiene percorso di ritorno a handle finto. Nella misura corrente il handle `0x5E000002` è creato ma non atteso prima del crash; l'anomalia riferita dal supervisore non si riproduce nel run corrente.
- Nessun template 0x6A, Data File Error, ERROR_FILE_NOT_FOUND o 0x26. Il gioco registra `SWarClass` e crea la finestra, poi SIGSEGV guest con entry `0x004D1D70`, fault `0x010027FC` (exit 9). Quindi superato il blocco MPQ, non dichiarato avvio completo.
- Verifica logging quiet: stesso binario senza `REASM_TRACE`, stderr non contiene `[evt]`, `[file]`, `[attr]` o `[file-open]`; il run arriva allo stesso crash successivo (exit 9).
- `font\\font.gid` 72 B e un PCX 900 B non sono stati estratti o verificati byte-per-byte. `mpyq` ha segnalato decifratura MPQ non supportata; non deduco da questo run una verifica dei contenuti. Questo sotto-gate resta aperto.

## Altri pool fissi esaminati
- Eventi: il limite a 16 è stato rimosso; il run conferma almeno 65 eventi reali.
- Thread: `g_th[16]` e `g_thjob[16]` sono tabelle fisse. `th_tramp` non azzera `fn`, perciò i 16 slot job non sono riciclati; inoltre `g_th_n` non ricicla il contatore e dopo 16 thread i nuovi handle non vengono registrati per il wait. Possibile limite cumulativo da correggere se misurato; in questo run `REASM_TRACE` non registra `CreateThread`, quindi non appare raggiunto.
- Timer: `SHIM_TIMER_MAX=8`, rifiuta l'allocazione quando pieno; il run non ha evidenza di saturazione.
- Critical section: `g_cs[256]`; il conteggio cresce anche con `DeleteCriticalSection` (che azzera `used`, ma non ricicla `g_cs_n`), limite cumulativo a 256 inizializzazioni: non raggiunto/misurato qui.
- File handle: file ordinari sono handle-fd, senza pool shim numerato; virtual file SNP e risorse hanno registri distinti non esaminati in questa missione.

## Artefatto finale
- Candidato: `build/starcraft-armv6-pathevt-clean` (20.852.292 byte).
- SHA-256: `81cec93c8cbc00f5888d431e6a343e7285d24a481297efe030bbaeeefd461494`.
- Log tracciato: `build/qa/run-pathevt.log`; exit 9. Log quiet di confronto: `/tmp/pathevt-quiet.log`; exit 9.

## Decisioni autonome
Ho mantenuto i pool thread/timer/critical-section invariati perché non ho misurato saturazione nel run e le modifiche sarebbero fuori dal fix provato. Ho lasciato esplicitamente aperta la verifica dei payload MPQ invece di dedurla dall'assenza del dialogo.
