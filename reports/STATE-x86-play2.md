# Stato — scx86-play2 (aggiornato 21/11/2026, 2ª passata)

- **`0x90909090` RISOLTO**: base di tabella nel `.text` guest emessa come indirizzo host da llasm;
  fix in `tools/dispatch-ir.py` (`RE_TABLE_BASE`, 12 siti) → lettura dal VA guest. D19 intatto.
- Evidenza GDB: `build/qa/gdb-play2-table-load-writer.log` (watch su `_ind_target`, `eip 0x802CE4B`,
  `mov 0x802d530(,%edx,4),%eax`); stop completo `build/qa/gdb-play2-indirect-stop.log`.
- **SCode speculari risolti** in `runtime/common/storm_sdraw_arm.c`: `8A 0E`, `8A 17`, `8B 0E`, `8B 17`
  (stop precedenti a `0x2016011A` e `0x20176A5A`).
- Build: entry-x86 PASS (102376 VA, 0 collisioni), `build.sh --arch x86 --skip-entry` PASS,
  `tests/x86_runtime_check` **PASS**.
- **Raggiunto e verificato a schermo**: Main Menu → Single Player (220,120) → Select Game Type →
  Original (260,226) → **Registry con profilo `enne2ù` listato** → lista (100,109) + OK (517,391) →
  **Campagna Terran Episode I** (320,250; Zerg 125,330; Protoss 540,330).
- Screenshot: `build/qa/play-screenshots/play2-campaign-select.png`, `play2-registry-fixed.png`,
  `play2-registry-profile.png`, `play2-select-game-type.png`, `play2-main-menu.png`,
  `play2-after-terran-crash.png`.
- **Blocco aperto**: click su Terran → `battle.snp`/`standard.snp`/`SnpQuery` → SIGSEGV host `pc=0x1`,
  si_addr `0x1`, **guest esp `0x08582010`** (host in `.guest_exe_code`), `[esp]=0x0530A156`,
  eax `0x18029160`. Log `build/isolated/play2/game.log`.
- Non ancora raggiunti: selezione Tutorial/Missione 1, Briefing, HUD/partita.
- Operativo: `cmd --id <id> '<comando intero>'` va **quotato**; per il gestore di crash in GDB serve
  `handle SIGSEGV nostop pass` + `break cpu_signal`.
- Istanza `play2` chiusa (`stop`); nessun display/audio reale usato.
- Report: `reports/REPORT-x86-play2.md`. Log build/QA: `build/qa/build-*.log`, `build/qa/check-play2.log`.

## Prossimo passo

GDB isolato con `handle SIGSEGV nostop pass` + `break cpu_signal`: catturare `bt`, `*(_cpu*)g_cpu_for_signal`,
pila emulata e ring degli archi; identificare chi scrive l'indirizzo host `0x08582010` in `_esp` dopo
`SnpQuery` (init rete campagna / `arm_com_thunk` / ABI stdcall / `ret` su parola host), fix mirato,
rebuild, `check.sh`, quindi riprendere Terran → Tutorial/Missione 1 → Briefing → partita.
