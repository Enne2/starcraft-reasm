# Report — scx86-play6 (21/11/2026)

## Esito

**Tutorial / Prima Missione raggiunta e giocabile.** Con il fix di `RE_TABLE_BASE` sugli alias
`@_text`, il click **Start** del briefing Boot Camp non produce più lo STOP `0x0435A008`: il gioco
entra nella mappa, mostra l'HUD completo, seleziona unità e esegue ordini di movimento.

## Fix applicato — `tools/dispatch-ir.py` (alias `@_text`)

1. **Oracolo / causa** (play5, riconfermata qui): llasm copia i frammenti **dati** del `.text` in un
   blob **host** `%__text* @_text` (`private constant`, finisce in `.rodata` a `0x1803DC20` del
   nostro ELF) con layout **compattato**: `@loc_409268 = private alias i8, ... @_text, i32 344`,
   mentre nel PE la distanza dal base `.text` è `0x00409268 - 0x00401000 = 0x8268`. Quindi
   `ptrtoint (i8* @loc_<VA> to i32)` emetteva l'indirizzo **host** del blob e un'indicizzazione
   negativa (`loc_409194`: `jmp *0x409268(,%ecx,4)` con `ecx = -6`) leggeva `0x0435A008` invece dei
   byte del PE a `0x00409250`.
2. **Contraltare verificato**: gli alias verso `%__data* @_data` **non** hanno il difetto — la
   sezione `.guest_exe_data` è a `0x0050C000` con size `0x1D1694` (l'intero `.data`) e
   `@_data + 0x16A9B8 = 0x6769B8` = VA esatto. Perciò **solo** gli alias `@_text` vanno riscritti.
3. **Riscrittura**: nuova `RE_TEXT_ALIAS` (raccoglie i VA il cui `@loc_<VA>` è alias dentro `@_text`)
   + `RE_PTRTOINT_TEXT`: ogni `ptrtoint (i8* @loc_<VA> to i32)` con VA in quell'insieme diventa la
   costante `i32 <VA decimale>`, **solo dentro i corpi di funzione** (i 7 siti nell'inizializzatore
   del blob `@_text`, che è `constant` e read-only, restano intatti). Copre tutte le forme (`add`,
   `sub`, `icmp`, `and`, `store`, `inttoptr`).
4. **D19 intatto**: il valore caricato dalle tabelle passa comunque per `reasm_indirect` →
   `reasm_indirect_resolve` (nessun target arbitrario, nessuno stub silenzioso).

Verifica locale del sito di play5 (`/tmp/p6-exe.ll`, `loc_409194`):

```
%5 = shl i32 %3, 2
%6 = add i32 4231784, %5          ; 4231784 = 0x00409268 (prima: ptrtoint @loc_409268)
%7 = inttoptr i32 %6 to i32*
%8 = load i32, i32* %7, align 4   ; legge .guest_exe_text_0 ai VA del PE
%"ind.t2" = getelementptr ... i32 17
store i32 %8, i32* %"ind.t2"      ; -> reasm_indirect (fail-closed)
musttail call fastcc void @reasm_indirect(%_cpu* %cpu) nounwind
```

## Gate

| Gate | Esito |
|---|---|
| `./tools/build.sh --arch x86` | PASS (`build/qa/build-play6.log`); dispatch-ir: **102376 VA** (exe 80644, storm 21727), **12766** call-site indiretti, **12** basi tabella `.text`, **619** ptrtoint alias `@_text` (exe **552**, storm **67**) |
| `./tests/x86_runtime_check/check.sh` | PASS |
| `readelf -h build/starcraft-x86` | ELF32 / EXEC / Intel 80386 |
| `python3 tools/dispatch-ir.py --dump-map` | 102376 voci, ordinate e univoche, **0 collisioni** |
| Isolamento | solo `./tools/run-isolated.sh` (Xvfb `:126` privato, `SDL_AUDIODRIVER=dummy`), zero finestre/suono; `stop --id play6` eseguito, nessun `starcraft-x86` attivo |

## Navigazione e partita (screenshot ispezionati con `read`)

`build/qa/play-screenshots/play6-00..14-*.png`

| # | Azione | Risultato |
|---|---|---|
| 00 | start `--id play6 --skip-intro 1 --smack 1` | main menu 640x480 |
| 01 | `click 220 120` | Select Game Type (Original / Expansion) |
| 02 | `click 260 226` | **Registry** con profilo `enne2ù` **listato** |
| 03–04 | `click 100 109` + `click 517 391` | schermata campagne (Terran/Zerg/Protoss) |
| 05 | `click 320 250` | testo campagna "REBEL YELL" |
| 06 | `click 320 440` | cartello "BOOT CAMP" — Mar Sara, 2:25 SCT |
| 07 | `click 320 440` | briefing missione (Objectives: 3 Supply Depots, 1 Refinery, 100 Vespene) |
| 08 | `click 538 390` (**Start**) | **in gioco** + dialog "Starcraft Tips" |
| 09 | `click 197 262` (OK) | **campo di battaglia**: Command Center ~(285,30), Barracks ~(410,160), minerali, 8-9 unità Terran, HUD (minerali 150, gas 0, supply 17/48, minimappa, ritratto, MENU, command card) |
| 10 | `click 247 180` (sx) | unità selezionata (cerchio verde + ritratto) |
| 12 | `down 220 130 1` + `move 330 300` + `up 330 300 1` | **box-select**: 8 unità selezionate (griglia ritratti) |
| 13 | `click 400 320 3` (dx) | ordine di movimento: dopo 5 s le unità sono in colonna verso il punto → pathing attivo |

Il gioco resta vivo e reattivo per tutta la sessione (130–395 fps, nessuno stop, nessun SIGSEGV).

## Decisioni autonome

- Riscrittura limitata ai corpi di funzione: l'inizializzatore del blob `@_text` è `constant`
  (mai scritto) e non partecipa alla semantica osservata, quindi non è stato toccato.
- Ritratti del briefing neri (animazione/filmato non pronto): non è un blocco, la schermata avanza;
  nessuna indagine oltre il necessario in questa passata.

## Prossimo passo

1. Verificare che la build ARM resti coerente: `./tools/build.sh --arch arm --skip-entry` +
   `tools/build-entry-obj.sh --check` (la riscrittura è arch-indipendente, ma non è ancora stata
   misurata su ARMv6).
2. Continuare la missione Boot Camp: costruire i 3 Supply Depot e la Refinery con gli SCV
   (selezione SCV → command card → placement) e verificare che le direttive del tutorial avanzino.
3. Censire le prossime API non implementate che la partita tocca (log `[snp] spiReceive non
   implementata` è atteso su singolo giocatore; controllare eventuali nuovi kernel SCode).
