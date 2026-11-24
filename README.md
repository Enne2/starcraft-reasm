# StarCraft + Storm — port nativo multi-architettura (x86 32-bit e ARM)

Reimplementazione nativa di **StarCraft.exe + storm.dll** senza Wine e senza DLL Windows: il codice
guest viene tradotto in LLVM IR e ricompilato per l'architettura host, con un runtime C che fornisce
i servizi Win32/DDraw/DSound/GDI/Smack/SNet.

Stato sintetico (2026-11-24): **giocabile su i686 e su ARMv7 (Pinebook Pro)** — menu, campagne,
salvataggi, multiplayer LAN fra due istanze sullo stesso host. Battle.net non è supportato.

## 1. Prerequisiti

| Cosa | x86 (i686) | ARM (armv7hf) |
|---|---|---|
| Compilatore | clang con supporto `-m32` | clang + sysroot armhf in `build/sysroot` |
| Linker | `ld.lld` (rustup va bene) | `ld.lld` |
| Altro | `llc`, `opt`, `llvm-nm` | idem |
| Libreria Smacker | `build/i386-libs/libsmacker-1.2.0.so` | `libsmacker-1.2.0.so` armhf nella dir di gioco |

Gli asset (`.mpq`, `.snp`, `characters/`, `Save/`, `maps/`) **non sono nel repo**: servono copie di
un'installazione StarCraft 1.16.1. Il lifting dell'IR (`build/lift-full/StarCraft.ll`, `storm.ll`)
è un artefatto generato e non versionato.

## 2. Build

```sh
# i686 (host a 64 bit va bene, serve il multilib)
./tools/build.sh --arch x86 --skip-entry

# ARMv7 (hard-float). ATTENZIONE: usare l'entry ARMv7 rigenerato, non build/lift-full/entry
ENTRY_DIR=$PWD/build/lift-full/entry-armv7 SYSROOT=$PWD/build/sysroot ./tools/build-runtime-armv7.sh
```

Prodotti: `build/starcraft-x86` e `build/starcraft-armv7`.
Se `build/lift-full/entry-armv7` non esiste, rigenerarlo con `./tools/build-entry-arm.sh` (richiede
`build/lift-full/StarCraft.ll` e `storm.ll`). **Non** usare `build/lift-full/entry/`: è il vecchio
entry ARMv6 e il binario che ne esce va in `SIGSEGV` all'avvio.

## 3. Esecuzione

Il gioco cerca gli asset nella **directory corrente**: si lancia dalla cartella che li contiene.

```sh
cd /path/con/gli/asset && /path/al/repo/build/starcraft-x86
```

Variabili d'ambiente utili:

| Variabile | Effetto |
|---|---|
| `SDL_VIDEODRIVER` / `SDL_AUDIODRIVER` | `x11` / `pulseaudio` (o `dummy` nei test) |
| `SC_PRESENT` | forzare il percorso di presentazione (`cpu`, `gl`, …) |
| `SC_GL_VERIFY=1` | confronta il frame GL col canvas CPU e stampa PASS/mismatch |
| `SC_FPS_CAP`, `SC_FPS_SLEEP`, `SC_FPS_LOG` | pacing e log FPS |
| `SC_SKIP_INTRO=1` | salta solo il filmato introduttivo (`SC_SMACK=0` **non** è utilizzabile: il gioco esce) |
| `SC_SMACKER_SO` | percorso della libsmacker da caricare |
| `SC_LAN_BIND`, `SC_LAN_PORT`, `SC_DISC_PORT`, `SC_LAN_PEERS` | networking LAN (vedi §6) |
| `SC_CTL=1`, `SC_CTL_SOCK`, `SC_NET_POLL_MS` | API di automazione e polling di rete |
| `SC_PRESENT_DBG`, `SC_SNP_TRACE`, `REASM_TRACE` | tracce diagnostiche (frame, rete, SCode) |

Il binario stampa all'avvio `[build] git=… time=… inputs=…` e dichiara se è **allineato al codice**
o obsoleto (in quel caso esce). Per ricostruire: `./tools/build.sh --arch x86` (senza `--skip-entry`).

## 4. Test

```sh
for t in tests/*/check.sh; do echo "== $t"; bash "$t"; done   # 9 verifiche, tutte x86
./build/qa/lan-1/x86_snp_lan_test      # trasporto UDP: due peer loopback, fan-out, wakeup
tests/x86_scomp_test/check.sh          # decompressore SComp: 4292/4292 settori byte-exact
tests/x86_runtime_check/check.sh       # ABI degli shim (stdlib/stdcall)
```

Sandbox isolata (Xvfb + audio dummy, non tocca il desktop):

```sh
SC_PRESENT=cpu ./tools/run-isolated.sh start --id prova --skip-intro 1 --smack 1
python3 tools/scctl --sock build/isolated/prova/sandbox/sc-ctl.sock shot /tmp/f.png
./tools/run-isolated.sh stop --id prova
```

`tools/scctl` (client dell'API `sc_ctl`) permette `shot`, `rawshot`, `click`, `move`, `down`, `up`,
`key`, `text`, `state`. I clic vanno inviati come `move` → `down` → pausa ~250 ms → `up`: con
`click` a latenza zero la UI del gioco non li registra.

## 5. Verifica grafica senza leggere immagini

`tools/shot-stat.sh file.png` stampa luminanza media, massimo e deviazione standard: è il modo
previsto per verificare "il frame è nero / c'è contenuto" senza caricare immagini nel contesto
(vedi `docs/POLICY-immagini.md`).

## 6. Multiplayer LAN

Due istanze sullo stesso host (IP di loopback distinti, porte dedicate):

```sh
SC_LAN_BIND=127.0.0.51 SC_DISC_PORT=16051 SC_LAN_PORT=16052 SC_LAN_PEERS=127.0.0.52:16051 \
  SC_PRESENT=cpu ./tools/run-isolated.sh start --id lan-a --skip-intro 1 --smack 1
SC_LAN_BIND=127.0.0.52 SC_DISC_PORT=16051 SC_LAN_PORT=16052 SC_LAN_PEERS=127.0.0.51:16051 \
  SC_PRESENT=cpu ./tools/run-isolated.sh start --id lan-b --skip-intro 1 --smack 1
```

Poi: *Multiplayer → Original → Local Area Network (UDP) → Ok → profilo*; l'host crea la partita, il
client la seleziona dalla lista e l'host avvia. Dettagli, coordinate UI e diagnosi:
`reports/REPORT-lan-lockstep-fix.md`, `reports/REPORT-lan-step2b.md`, `reports/ANALYSIS-snp-contracts.md`.

## 7. Struttura del repository

| Percorso | Contenuto |
|---|---|
| `runtime/common/` | runtime condiviso fra le architetture (compilato per i686 e armv7) |
| `runtime/arch/{arm,x86}/` | seam in assembly (dispatch indiretto, bridge COM, TLS) |
| `runtime/shim-port/elf/` | shim Win32/DirectDraw/DSound/GDI/Smack/SNet + host SDL2/GL |
| `tools/` | pipeline di build, lifting, QA (`build-entry-*.sh`, `build-runtime-*.sh`, `scctl`) |
| `tests/` | verifiche eseguibili |
| `reports/` | analisi, report di fase, stato per area (`reports/INDEX.md`) |
| `docs/` | policy e note trasversali |

Indice per area e documenti correnti: **`reports/INDEX.md`**.

## 8. Limiti noti

- **Battle.net / PvPGN**: non supportato (`battle.snp` non è liftato, mancano gli shim `WSOCK32` e la
  lettura dei gateway dal registro). Vedi `PLAN-MULTIPLAYER.md` §4.
- **Multiplayer**: verificato fra due istanze sullo stesso host; non su due macchine fisiche, e il
  determinismo dei turni non è misurato con un checksum di stato.
- **Audio**: funzionante, ma la qualità non è verificata in modo automatico.
- **64-bit**: non supportato; il design è in `PLAN-MULTIARCH.md` §7.2.
- Gli asset di gioco non sono ridistribuibili: servono copie personali.
