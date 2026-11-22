# Stato — scx86-play3 (aggiornato 21/11/2026, 3ª passata)

- **Crash `SnpQuery`/Terran (`esp=0x08582010`, `pc=0x1`) RISOLTO**: erano due difetti ABI.
  1. `SnpQuery`/`SnpBind` restituite come simboli C (`win32_snp_get_proc`) → ora `DYNAMIC` in
     `runtime/common/bridge_dynamic.def` + `SHIM(SnpQuery,5,5)`/`SHIM(SnpBind,2,2)` nei manifest,
     `win32_snp_get_proc` → `arm_bridge_lookup_thunk`, `GetProcAddress` risolve il modulo snp per
     primo. Evidenza GDB `build/qa/gdb-play3.log`.
  2. Tabella `NetFunctions` con puntatori C → ora `g_slots[]` di **stub guest-ABI** per nome
     (`snp_elf.c`, fail-closed). Evidenza `build/qa/gdb-play3-fixed.log` (`_ind_target=snp_initialize`).
- Build: `build-shim-x86` PASS (356 stub, 0 collisioni, manifest i686 = file versionati),
  `build.sh --arch x86 --skip-entry` PASS, `tests/x86_runtime_check` **PASS**
  (`build/qa/build-play3.log`, `build/qa/check-play3.log`).
- **Raggiunto e verificato a schermo** (640×480): menu → Single Player (220,120) → Select Game Type →
  Original (260,226) → Registry (`enne2`; profilo ricreato: New ID 122,344 → `text enne2` → **`key 13`**)
  → voce (100,109) + OK (517,391) → **Campagna** (Terran 305,250 · Zerg 110,330 · Protoss 520,330 ·
  Load Saved 325,375 · Play Custom 325,411 · Load Replay 325,448 · Cancel 548,428) → click Terran.
- Dopo Terran: **SnpQuery 0/1/2, SnpBind index=0, spiInitialize, spiLockDeviceList,
  spiUnlockDeviceList, spiStartAdvertisingLadderGame** eseguiti, nessun crash SNet
  (`build/isolated/play3/game.log`, righe 313-322).
- **Blocco aperto (nuovo, diagnosticato)**: SIGSEGV `pc 0x08457DB1`, `si_addr 0x10`, proc tradotta
  `0x004A6030+1`, `ECX=0` — `win32_shim_timers_pump` (`win32_shim.c:451-474`) chiama il TIMERPROC con
  ABI C host su un ingresso tradotto. Catena e dettagli: `build/qa/gdb-play3-crash2.log`,
  `reports/REPORT-x86-play3.md`.
- Screenshot verificati con `read` in `build/qa/play-screenshots/play3-*.png`; neri (crash):
  `play3-terran-result.png`, `play3-s6-terran.png`.
- Istanza `play3` **chiusa** (`stop`); nessun display reale, nessun audio reale.
- `qmem_store` aggiornato (3 record: fix SnpQuery/SnpBind/NetFunctions; blocco timer; coordinate).

## Prossimo passo

1. `win32_shim_timers_pump`: sostituire `cb(hwnd,msg,id,ticks)` con
   `arm_guest_call4(proc, hwnd, 0x113, id, GetTickCount())` (come `win32_ui.c:950`), rebuild,
   `tests/x86_runtime_check/check.sh`.
2. Rilancio isolato (`start --id play4 --skip-intro 1 --smack 1`, pause ≥2 s tra i click) e ripresa:
   Campagna → Terran (305,250) → selezione missione / Boot Camp → Briefing → Start → partita/HUD;
   salvare gli screenshot ispezionati in `build/qa/play-screenshots/play4-*.png`.
3. Registrare in qmem il fix del timer e le coordinate missione/briefing/HUD.
