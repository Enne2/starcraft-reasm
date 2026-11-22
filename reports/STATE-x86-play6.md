# Stato — scx86-play6 (21/11/2026)

- **Fix di `RE_TABLE_BASE` completato** in `tools/dispatch-ir.py`: ogni
  `ptrtoint (i8* @loc_<VA> to i32)` il cui `@loc_<VA>` è `private alias` dentro il blob host
  `%__text* @_text` viene ora sostituito con la costante del VA guest (il `.text` del PE è mappato
  ai VA originali da `.guest_exe_text_0` / `.guest_storm_text_4`). Solo nei corpi di funzione.
  - Siti riscritti: **552** (StarCraft) + **67** (storm) = **619**.
  - Gli alias verso `@_data` restano intatti: quella sezione è stride-preserving e già ai VA guest
    (`@_data + 0x16A9B8 = 0x6769B8`).
  - D19 intatto: il target caricato passa comunque da `reasm_indirect`.
- **Gate verdi**: `./tools/build.sh --arch x86` PASS, `tests/x86_runtime_check/check.sh` PASS,
  `readelf -h` = ELF32/EXEC/Intel 80386, `--dump-map` 102376 voci e **0 collisioni**.
- **Prima missione raggiunta**: briefing Boot Camp → **Start (538,390)** → partita giocabile
  (HUD, unità, selezione, box-select, ordine di movimento eseguito). Lo STOP `0x0435A008` del
  percorso `loc_409194` non si verifica più.
- **Isolamento**: solo `./tools/run-isolated.sh` (Xvfb `:126`, audio dummy); `stop --id play6`
  eseguito, nessun processo `starcraft-x86` attivo.
- **Memoria**: `qmem_store` aggiornato — fix `c47c8467-f506-4200-9500-97726d9ac06d` (decision),
  esito di gioco `4041f05c-fee1-40c7-b75d-a41db0cb9743` (episode).

## Screenshot chiave (`build/qa/play-screenshots/`)

`play6-07-bootcamp.png` (briefing con Start), `play6-08-after-start.png` (in gioco + Tips),
`play6-09-ingame.png` (campo di battaglia + HUD), `play6-10-unit-selected.png`,
`play6-12-boxselect.png` (8 unità), `play6-13-move2.png` (movimento eseguito).

## Prossimo passo

1. `./tools/build.sh --arch arm --skip-entry` + `tools/build-entry-obj.sh --check` per confermare
   che la riscrittura non rompa ARMv6 (stessa IR, ma mai misurata lì).
2. Continuare il Boot Camp: SCV → command card → costruzione Supply Depot/Refinery, verificando
   l'avanzamento delle direttive del tutorial e gli eventuali nuovi kernel SCode/API mancanti.
