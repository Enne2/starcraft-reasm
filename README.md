# starcraft-reasm — StarCraft + Storm, port nativo via lifting e shim

**Un port nativo di StarCraft e Brood War che non usa Wine, non usa DirectX e non emula una CPU.**

Il codice del gioco originale non viene eseguito con un emulatore né con un livello di compatibilità:
viene **tradotto staticamente** in una rappresentazione intermedia (LLVM IR) e poi **ricompilato per
l'architettura dell'host**. I servizi che il gioco si aspetta dal sistema operativo (Win32, DirectDraw,
DirectSound, GDI, Smacker, reti SNet/SNP) sono reimplementati da un runtime C che parla direttamente
con le API native della piattaforma (SDL2, OpenGL/GLES, socket POSIX).

Risultato: **StarCraft gira come un programma nativo** su i686 e su ARMv7 (Pinebook Pro), con menu,
campagne, salvataggi e multiplayer LAN fra due istanze. Nessun asset del gioco è incluso: serve una
copia personale di StarCraft 1.16.1.

---

## Indice

1. [Perché esiste questo progetto](#1-perché-esiste-questo-progetto)
2. [Come funziona](#2-come-funziona)
3. [Stato attuale](#3-stato-attuale)
4. [Requisiti](#4-requisiti)
5. [Guida: compilare per una piattaforma software/hardware](#5-guida-compilare-per-una-piattaforma-softwarehardware)
6. [Guida: usare il port](#6-guida-usare-il-port)
7. [Test e verifica](#7-test-e-verifica)
8. [Sviluppi futuri](#8-sviluppi-futuri)
9. [Struttura del repository](#9-struttura-del-repository)
10. [Licenza, dati di gioco e marchi](#10-licenza-dati-di-gioco-e-marchi)

---

## 1. Perché esiste questo progetto

Tre motivazioni, in ordine di importanza.

**1. Portare il gioco oltre il suo ecosistema software.** StarCraft 1.16.1 è un binario PE a 32 bit per
Windows 9x/XP che dipende da DirectDraw, DirectSound, GDI e da Storm.dll: su Linux moderno esiste solo
attraverso Wine. Wine funziona bene, ma resta un *emulatore di Windows*: paghi il costo di un intero
sistema operativo simulato (trap, traduzione, sincronizzazione) per eseguire un singolo programma.

**2. Portarlo su altre architetture hardware, non solo altri sistemi operativi.** Un emulatore x86 su
una scheda ARM o RISC-V paga una traduzione dinamica a ogni esecuzione; un port nativo no. Con il
lifting il codice del gioco diventa codice dell'architettura di destinazione: su un Pinebook Pro
(ARMv7) il gioco gira a frame rate pieno con un carico CPU modesto, cosa che un emulatore x86 non
sarebbe in grado di fare.

**3. Preservazione.** Un binario del 1998 dipende da un'API grafica (DirectDraw) che il sistema
operativo ha smesso di supportare. Il lifting + shim è un modo per conservare il *comportamento*
del programma descrivendone le dipendenze in termini che sopravvivono al sistema operativo: il gioco
resta lo stesso codice, ma le sue dipendenze vivono nel nostro runtime, dove possono essere
reimplementate, testate e portate.

Da qui la forma del progetto: **una pipeline di lifting** (che produce codice compilabile dal binario
originale) più **un runtime/shim** (che ne reimplementa i servizi). Nessuna delle due metà, da sola,
basta: insieme rendono il gioco nativo su qualunque piattaforma per cui esista un compilatore LLVM.

---

## 2. Come funziona

```
StarCraft.exe / storm.dll            (binari originali: copia personale dell'utente)
        │
        │  1. LIFTING  (vendor/sr: SR/llasm, static recompiler)
        ▼
StarCraft.llasm / storm.llasm        (rappresentazione intermedia del codice tradotto)
        │
        │  2. TRADUZIONE  (tools/, LLVM)
        ▼
StarCraft.ll / storm.ll              (LLVM IR, con dispatch table e ABI guest)
        │
        │  3. COMPILAZIONE  (llc -mtriple=<architettura>)
        ▼
StarCraft.entry.o / storm.entry.o    (codice nativo per l'host)
        │
        │  4. LINK  (con il nostro runtime)
        ▼
build/starcraft-x86  |  build/starcraft-armv7 ...
```

Il passaggio chiave è il **3**: lo stesso IR viene compilato per triple diverse. Cambiare architettura
non significa riscrivere il port, significa cambiare `-mtriple` e fornire lo strato minimo dipendente
dall'ISA.

Il **runtime** (`runtime/`) fornisce ciò che il codice tradotto si aspetta:

| Livello | Cosa fa |
|---|---|
| `runtime/common/` | CPU guest (registri, flag, x87), mappatura della memoria virtuale guest, dispatch delle chiamate indirette, bridge verso il codice C, SCode (i blitter di Storm), SComp (decompressore) |
| `runtime/arch/<arch>/` | solo l'assembly inevitabilmente specifico: trampolino del dispatch indiretto, bridge COM, azzeramento guest, TLS |
| `runtime/shim-port/elf/` | Win32 (file, processi, sincronizzazione, registro, messaggi), DirectDraw, DirectSound, GDI, Smacker, SNet/SNP (rete), SDL2 + OpenGL per la presentazione |
| `tools/` | pipeline di build, lifting, generazione dei bridge, QA (`sc_ctl`, sandbox isolata) |

Due regole di progetto che spiegano molte scelte nel codice:

- **l'oracolo è il binario**: ogni costante, layout di struttura, arità `stdcall` o indirizzo nel
  runtime cita l'indirizzo del decompilato che la giustifica (vedi `reports/ANALYSIS-*.md`);
- **fail-closed**: una funzione di cui non è provato il contratto restituisce un errore esplicito e
  logga, invece di restituire valori inventati che sembrerebbero funzionare. Un gioco che si blocca
  in modo visibile è meglio di un gioco che corrompe stato silenziosamente.

---

## 3. Stato attuale

| Area | Stato |
|---|---|
| **i686 (Linux, anche host a 64 bit)** | giocabile: menu, campagne (Terran/Zerg/Protoss), salvataggi e caricamento, filmati, audio |
| **ARMv7 hard-float (Pinebook Pro)** | giocabile: parità con i686 su menu, campagne, save/load, filmato introduttivo |
| **Multiplayer LAN** | funzionante fra due istanze: discovery UDP, lista partite, join, partita con lockstep attivo |
| **Battle.net / PvPGN** | **non supportato** (vedi §8.2) |
| **Architetture 64-bit** | **non supportate**: l'IR attuale ha puntatori a 32 bit (vedi §8.1) |

Numeri utili: la divergenza di codice fra le due architetture è **1,08%** (224 righe su 20.697,
elencate in `reports/REPORT-x86-vs-arm.md`); la mappa di dispatch ha **102.392 voci, 0 collisioni**.

---

## 4. Requisiti

| Cosa | Note |
|---|---|
| Copia personale di **StarCraft / Brood War 1.16.1** | il repository non contiene asset né binari |
| `clang`, `ld.lld`, `llc`, `opt`, `llvm-nm` | LLVM 18+ consigliato |
| Python 3 | per gli strumenti di lifting e generazione |
| SDL2 | runtime (x11/wayland) |
| `x86`: multilib a 32 bit (`gcc-multilib` o equivalente) | per compilare gli shim `-m32` |
| `ARMv7`: sysroot armhf | in `build/sysroot`, oppure passare `SYSROOT=...` |
| `libsmacker` per la tua architettura | sorgenti in `vendor/libsmacker`, caricata a runtime |

Per gli asset: copia (o collega) nella directory di gioco gli archivi `.mpq`, i file `.snp`,
le directory `characters/` e `Save/`, la cartella `maps/`.

---

## 5. Guida: compilare per una piattaforma software/hardware

### 5.1 Prima cosa: preparare il lifting

Il lifting si esegue **una volta sola**, sulla macchina dove hai i binari originali, e produce gli
artefatti (`build/lift-full/StarCraft.ll`, `storm.ll`) che poi si compilano per qualsiasi target.

```sh
# 0) una volta sola: compilare gli strumenti di lifting (SR/llasm + llasm)
tools/build-tools.sh

# 1) lift dei due binari: da PE a .llasm, e da .llasm a LLVM IR (LLASM_TO_IR=1, default)
tools/lift.sh /percorso/StarCraft.exe build/lift-full/StarCraft.llasm
tools/lift.sh /percorso/storm.dll    build/lift-full/storm.llasm

# risultati: build/lift-full/StarCraft.ll e build/lift-full/storm.ll
# (cartella ignorata da git: sono dati derivati dal binario originale)
```

I due nomi `StarCraft.ll` e `storm.ll` in `build/lift-full/` sono quelli che la pipeline di entry si
aspetta (`tools/build-entry-x86.sh` li verifica e si ferma con un messaggio esplicito se mancano).

L'IR non è versionato: **deriva dal binario originale** e resta sul tuo disco.

### 5.2 Compilare per x86 32-bit (Linux)

```sh
./tools/build.sh --arch x86 --skip-entry     # riusa l'entry gia' compilato
./tools/build.sh --arch x86                  # ricompila anche l'entry
# prodotto: build/starcraft-x86
```

### 5.3 Compilare per ARMv7 hard-float (esempio: Pinebook Pro)

```sh
# entry per il target (genera build/lift-full/entry-armv7)
./tools/build-entry-arm.sh

# runtime + link per armv7a hard-float, con sysroot
ENTRY_DIR=$PWD/build/lift-full/entry-armv7 \
SYSROOT=$PWD/build/sysroot \
  ./tools/build-runtime-armv7.sh
# prodotto: build/starcraft-armv7
```

> **Attenzione**: usare `ENTRY_DIR=…/entry-armv7`. La cartella `build/lift-full/entry/` contiene un
> entry ARMv6: il binario che ne esce va in `SIGSEGV` all'avvio. È l'errore più comune quando si
> ricostruisce da zero.

### 5.4 Aggiungere una nuova architettura o piattaforma

Il percorso è sempre lo stesso, in cinque passi:

1. **Triple e flag**: scegli il `--target` di clang (`armv6-…-gnueabihf`,
   `riscv32-unknown-linux-gnu`, `aarch64-…`, …) e i flag CPU.
2. **Entry**: `tools/build-entry-arm.sh` è il modello — copia in `tools/build-entry-<arch>.sh` e
   cambia `TRIPLE`, `CPU_FLAGS`, `--sysroot`. Se l'IR ha puntatori a 32 bit e il target è 64 bit,
   vedi §8.1: serve la modalità 64-bit di `llasm` (`PTROFS_64BIT`).
3. **Seam ISA**: in `runtime/arch/<arch>/` servono solo quattro cose — dispatch indiretto,
   bridge COM, azzeramento del guest, TLS. Sono poche centinaia di righe di assembly e sono l'unico
   codice che *deve* essere specifico.
4. **Runtime condiviso**: `runtime/common/` e `runtime/shim-port/elf/` si compilano così come sono.
   Gli unici punti da toccare sono i blocchi `#if defined(__arch__)`: su questo progetto sono
   **224 righe su 20.697**, elencate una per una in `reports/REPORT-x86-vs-arm.md`.
5. **Presentazione**: lo shim parla SDL2 + OpenGL/GLES; su una piattaforma senza questi (framebuffer
   diretto, KMS/DRM) si aggiunge un percorso in `host_elf.c` come per gli altri (`present_gpu`,
   `present_gl`, framebuffer CPU).

Verifica del passo: `./tools/build.sh --arch <arch> --skip-entry` deve produrre il binario, e
`tools/run-isolated.sh` (o il launcher equivalente) deve arrivare al menu con `FPS > 0` e
`mean > 0.02` su uno screenshot.

---

## 6. Guida: usare il port

Il gioco cerca gli asset nella **directory corrente**: si lancia dalla cartella che li contiene.

```sh
cd /percorso/della/copia/di/starcraft
/path/al/repo/build/starcraft-x86
```

### Variabili d'ambiente principali

| Variabile | Effetto |
|---|---|
| `SDL_VIDEODRIVER` / `SDL_AUDIODRIVER` | `x11`/`wayland`, `pulseaudio` (o `dummy` nei test) |
| `SC_SMACKER_SO` | percorso della `libsmacker` da caricare |
| `SC_SKIP_INTRO=1` | salta solo il filmato introduttivo (`SC_SMACK=0` **non** è usabile: il gioco esce) |
| `SC_PRESENT` | forza il percorso di presentazione (`cpu`, `gl`, …) |
| `SC_GL_VERIFY=1` | confronta il frame GL col canvas CPU e stampa PASS/mismatch |
| `SC_FPS_CAP`, `SC_FPS_SLEEP`, `SC_FPS_LOG` | pacing e log degli FPS |
| `SC_LAN_BIND`, `SC_LAN_PORT`, `SC_DISC_PORT`, `SC_LAN_PEERS` | networking LAN |
| `SC_CTL=1`, `SC_CTL_SOCK` | API di automazione in-process (`tools/scctl`) |
| `SC_PRESENT_DBG`, `SC_SNP_TRACE`, `REASM_TRACE` | tracce diagnostiche |

### Multiplayer LAN fra due istanze sullo stesso host

```sh
SC_LAN_BIND=127.0.0.51 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
SC_LAN_PEERS=127.0.0.52:16051 SC_PRESENT=cpu \
  ./tools/run-isolated.sh start --id lan-a --skip-intro 1 --smack 1

SC_LAN_BIND=127.0.0.52 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
SC_LAN_PEERS=127.0.0.51:16051 SC_PRESENT=cpu \
  ./tools/run-isolated.sh start --id lan-b --skip-intro 1 --smack 1
```

Poi, nelle due finestre: *Multiplayer → Original → Local Area Network (UDP) → Ok → profilo*; l'host
crea la partita, il client la seleziona dalla lista, l'host avvia. Dettagli e diagnosi:
`reports/REPORT-lan-lockstep-fix.md`, `reports/REPORT-lan-step2b.md`.

### Automazione e verifica senza guardare lo schermo

```sh
SC_PRESENT=cpu ./tools/run-isolated.sh start --id prova --skip-intro 1 --smack 1
python3 tools/scctl --sock build/isolated/prova/sandbox/sc-ctl.sock shot /tmp/f.png
tools/shot-stat.sh /tmp/f.png        # media/max/deviazione: "è nero o c'è contenuto?"
./tools/run-isolated.sh stop --id prova
```

`sc_ctl` espone `shot`, `rawshot`, `click`, `move`, `down`, `up`, `key`, `keydown`, `keyup`, `text`,
`state`. I clic vanno inviati come `move` → `down` → pausa ~250 ms → `up`: con un click a latenza zero
la UI del gioco non lo registra.

---

## 7. Test e verifica

```sh
for t in tests/*/check.sh; do echo "== $t"; bash "$t"; done    # 9 verifiche, x86
```

Coprono: ABI degli shim (stdcall/`ret N`), decompressore SComp (4292/4292 settori byte-exact),
kernel SCode (parità interprete/LUT su larghezze 1..364), palette GL (307.200 pixel esatti),
mappatura della memoria guest, pool di memoria virtuale, affinità di thread, IO readahead, API `sc_ctl`.

Oltre ai test, il progetto usa due forme di verifica che valgono più di una suite:

- **confronto con l'oracolo** (`reports/ANALYSIS-*.md`): ogni contratto è provato sul decompilato,
  con indirizzo citato;
- **verifica numerica degli screenshot** (`tools/shot-stat.sh`): luminanza media, massimo, deviazione
  standard al posto dell'ispezione visiva — così una regressione grafica è un numero, non
  un'impressione (vedi `docs/POLICY-immagini.md`).

---

## 8. Sviluppi futuri

### 8.1 Altre architetture e piattaforme

- **ARMv7 / ARMv6**: fatti. Il target ARMv6 (Raspberry Pi 1/Zero) ha una pipeline separata
  (`tools/build-runtime-arm.sh`) e un report di prova in QEMU (`reports/REPORT-armv6-native-qemu.md`).
- **64-bit (x86_64, aarch64, riscv64)**: il blocco non è il compilatore, è l'IR: i file `.ll`
  attuali usano puntatori a 32 bit (`p:32:32`, `inttoptr i32`), mentre il guest deve continuare a
  vedere indirizzi a 32 bit. Il percorso previsto, già individuato in `PLAN-MULTIARCH.md` §7.2, è:
  1. usare la modalità 64-bit di `llasm` (`PTROFS_64BIT`, `REG2PTR`/`PTR2REG`), così il guest mantiene
     indirizzi `uint32_t` mentre il codice generato usa puntatori a 64 bit;
  2. estendere `tools/dispatch-ir.py` alle forme `inttoptr i64`/`ptrtoint … to i64`;
  3. marshalling `uint32_t` ↔ `uintptr_t` negli shim dove i puntatori guest vengono tradotti
     in puntatori host;
  4. verificare le arità `stdcall` e `va_list` per le ABI LP64 (System V AMD64 / AAPCS64).
  Fatto questo, il resto del runtime è già arch-neutro: il port 64-bit è lavoro di *pipeline*, non
  di reimplementazione.
- **RISC-V 32/64, LoongArch, PowerPC**: stesso schema a cinque passi di §5.4; per i 32 bit
  little-endian l'IR funziona senza rigenerarlo.
- **Piattaforme senza X11/Wayland** (framebuffer, KMS/DRM, console portatili): serve un percorso di
  presentazione dedicato in `host_elf.c` (il codice è già organizzato in percorsi separati: CPU
  framebuffer, GPU texture, GL/GLES).
- **Piattaforme non-Linux** (BSD, macOS): gli shim usano chiamate POSIX + SDL2; il lavoro è
  soprattutto nella mappatura dei percorsi (`/proc`, `mmap`, thread affinity) e nel loader.

### 8.2 Un Battle.net privato, personalizzabile

Oggi il multiplayer passa dal provider SNP UDP nativo del gioco (`standard.snp`), che abbiamo
reimplementato: discovery, lista partite, join e lockstep funzionano. **Battle.net non è supportato**,
e questa è la parte con più margine di sviluppo interessante, perché un server privato permette cose
che il servizio originale non permetteva più.

Tre strade, in ordine di invasività crescente:

1. **Server compatibile (PvPGN o simile) + client che parla BNCS.** Il server esiste già; manca il
   client: nel nostro port bisogna far parlare il gioco con un server TCP. Le due opzioni tecniche
   sono (a) *liftare* anche `battle.snp` — il PE che implementa il protocollo Battle.net — e fornirgli
   gli shim Winsock che oggi non esistono (18 ordinali `WSOCK32`), più la lettura dei gateway dal
   registro (`Software\Battle.net\Configuration`); oppure (b) scrivere un **provider nativo** che
   implementa direttamente il protocollo verso il server, saltando `battle.snp`.
2. **Battle.net "locale" con funzionalità nuove.** Con un server nostro si può aggiungere ciò che
   l'originale non aveva o ha perso: stanza chat persistente con storico, lobby con ricerca partite,
   spettatori, replay condivisi lato server, statistiche e ladder, autenticazione moderna (senza
   CD-key), server che gira su una Raspberry o in un container.
3. **Interoperabilità.** Un server nostro può parlare sia BNCS (per client originali o via Wine) sia
   un protocollo nuovo (JSON/WebSocket) per un client moderno o un bot, e fungere da ponte fra i due.

Attenzione ai vincoli: `battle.snp` è codice di Blizzard (non ridistribuibile: il lifting resta
locale), l'autenticazione originale si basa su CD-key (un server privato deve decidere come
sostituirla), e il protocollo Battle.net classico è documentato solo da reverse engineering
comunitario (il nostro approccio è in `PLAN-MULTIPLAYER.md` §4 con l'analisi in
`reports/ANALYSIS-snp-contracts.md`).

### 8.3 Altro lavoro aperto

- **Determinismo del multiplayer**: misurare il lockstep con un checksum di stato per escludere
  desync (in particolare cross-arch, dove x87 a 80 bit e `double` a 64 bit potrebbero divergere).
- **Robustezza di rete**: host che sparisce, TTL delle voci in lista, ripresa dopo perdita di pacchetti.
- **Performance**: `PLAN-PERF-OPT.md` ha ancora fasi aperte (cache della mappatura di memoria
  invalidata dalle `VirtualAlloc`, accelerazione del codice caldo di Storm via `reasm_extern`,
  tuning per ARM).
- **Suite di test per ARM**: oggi le nove verifiche automatiche sono solo x86; su ARM la verifica è
  manuale o via `sc_ctl`.

---

## 9. Struttura del repository

| Percorso | Contenuto |
|---|---|
| `runtime/common/` | runtime condiviso fra le architetture (CPU guest, memoria, dispatch, SCode, SComp) |
| `runtime/arch/{arm,x86}/` | seam in assembly specifici dell'ISA |
| `runtime/shim-port/elf/` | shim Win32/DirectDraw/DirectSound/GDI/Smacker/SNet + presentazione SDL2/GL |
| `tools/` | pipeline di build, lifting, generazione bridge, `scctl`, sandbox `run-isolated.sh` |
| `tests/` | verifiche eseguibili, tutte senza interfaccia grafica |
| `reports/` | analisi (oracolo IDA), report di fase, misure — **indice in `reports/INDEX.md`** |
| `docs/` | policy e note trasversali |
| `vendor/` | componenti di terzi (SR/llasm, udis86, libsmacker) con le loro licenze |

---

## 10. Licenza, dati di gioco e marchi

- **Codice di questo progetto**: GNU General Public License v3.0 — vedi [`LICENSE`](LICENSE).
- **Componenti di terzi**: licenze proprie, elencate in [`THIRD-PARTY.md`](THIRD-PARTY.md)
  (SR/llasm MIT, udis86 BSD, libsmacker LGPL 2.1 caricata a runtime).
- **Nessun asset o binario del gioco** è incluso o ridistribuito: servono una copia personale e
  legittima di StarCraft/Brood War 1.16.1, e gli artefatti derivati dal lifting (IR, oggetti, database
  IDA) restano sulla macchina dell'utente, esclusi dal versionamento.
- StarCraft, Brood War, Battle.net e Blizzard Entertainment sono marchi di Blizzard Entertainment,
  Inc. Questo progetto **non è affiliato né approvato** da Blizzard.
