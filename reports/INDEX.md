# Indice dei report — cosa è corrente e cosa è storico

Il repo contiene molta documentazione prodotta durante le fasi di lavoro. Questa pagina dice **quale
documento è la verità corrente** per ogni area; tutto il resto è diario storico (utile per capire
*perché* una scelta è stata fatta, non per sapere *come sta adesso* il codice).

## Documenti correnti (leggere questi)

| Area | Documento corrente | Contenuto |
|---|---|---|
| **Fondamenta / port i686** | `REPORT-x86-final.md`, `STATE-x86-final.md` | architettura del port, dispatch, shim, esito |
| **Multi-architettura** | `PLAN-MULTIARCH.md`, `REPORT-x86-vs-arm.md` | piano, e la misura reale della divergenza (1,08%) |
| **Port ARMv7 / Pinebook** | `REPORT-arm-ingame.md`, `REPORT-arm-parity.md` | cosa mancava su ARM (`O_DIRECTORY`), parità |
| **Grafica / Smacker** | `REPORT-graphics-final.md`, `REPORT-intro-black.md`, `REPORT-race-select.md` | flicker intro, ritratti campagna, filmato ARM nero |
| **Multiplayer** | `ANALYSIS-snp-contracts.md` **← contratti provati in IDA**, `REPORT-lan-step1.md`, `REPORT-lan-step2b.md`, `REPORT-lan-step3.md`, `REPORT-lan-lockstep-fix.md` | discovery/GameList/join/lockstep, causa e fix |
| **Ipotesi di rete scartate** | `LAN-probe-h1.md` … `LAN-probe-h4.md` | cosa è stato falsificato e con quale evidenza |
| **Compressione (SComp)** | `REPORT-opt-scomp.md`, `REPORT-scomp552.md` | Huffman+ADPCM nativo, settori byte-exact |
| **Performance** | `PLAN-PERF-OPT.md`, `PERF-WINE-COMPARISON.md`, `PROF-CPU.md`, `PROF-RAM.md` | piano e misure |
| **Automazione / QA** | `REPORT-scctl-api.md`, `docs/POLICY-immagini.md`, `tools/qa/` | API `sc_ctl`, verifica numerica degli screenshot |

## Storico (diario di fase, non aggiornato)

Le serie seguenti raccontano come si è arrivati allo stato attuale. **Non** descrivono il codice di
oggi e alcune loro affermazioni sono state smentite da misure successive (per esempio
`PLAN-MULTIPLAYER.md` sulle tabelle di giugno: vedi il blocco "STATO" in testa a quel file).

- `STATE-x86-bringup*.md`, `REPORT-x86-bringup*.md` — bring-up i686 (6 tappe).
- `STATE-x86-play*.md`, `REPORT-x86-play*.md` — far giocare il port (6 tappe).
- `STATE-gap-*.md`, `REPORT-gap-*.md` — colmamento lacune per sottosistema.
- `STATE-diag-*.md`, `REPORT-DIAG-*.md` — diagnosi (campagna, hover, trasparenza).
- `STATE-opt-*.md`, `REPORT-opt-*.md` — ottimizzazioni successive.
- `STATE-port-*.md`, `REPORT-port-*.md` — porting per sottosistema (win32, dispatch, gfx, smk).
- `STATE-b1/b2/b3-*.md`, `STATE-solve-lut.md`, `STATE-fix-ht.md` — indagini puntuali.

## Convenzioni

- `STATE-<area>.md` = stato sintetico di un task (≤70 righe, pensato per essere riletto).
- `REPORT-<area>.md` = rapporto con esito, evidenze e limiti.
- `ANALYSIS-<area>.md` = analisi di un binario (oracolo IDA), con indirizzi citati.
- `PLAN-<area>.md` = piano; se in testa c'è un blocco **STATO**, quello prevale sul resto del file.
- `LAN-probe-*` = esiti delle esplorazioni parallele di un'indagine (ipotesi + falsificazione).

## Artefatti fuori dal versionamento

File di lavoro spostati in `build/qa/superseded/` (cartella ignorata da git) quando sono copie
superate di sorgenti vivi — per esempio `storm_sdraw-elf-2026-11-23.c`, duplicato di
`runtime/common/storm_sdraw.c`.
