# Stato — ARMv7 in-game Pinebook

- **Completato:** StarCraft ARMv7 entra in partita via Play Custom → `(2)Bottleneck.scm`.
- **Causa:** `0x10000` usato come `O_DIRECTORY` è giusto su i686 ma su armhf è `O_DIRECT`; ARM `O_DIRECTORY=0x4000`. L’open della directory falliva con `EINVAL`.
- **Fix:** macro `GAME_O_DIRECTORY` ABI-specifica in `runtime/shim-port/elf/win32_game.c`, usata per enumerazione file e apertura directory.
- **Prova:** nuova traccia `[ff] dir-open` + `[ff] first-match` sul pattern `maps\\*.*`; lista maps popolata, apertura Bottleneck OK, HUD in-game visibile.
- **Build/test:** build x86, `x86_scomp_test`, `x86_runtime_check`, build ARMv7 — tutti PASS.
- **Screenshot:** `build/qa/pbp-test/ingame-bottleneck.png` e JPEG compresso `build/qa/pbp-test/ingame-bottleneck.jpg`.
- **Target:** partita lasciata attiva in `~/pbst/scctl-api`; altro processo nel CWD `~/pbst/game` lasciato intatto. Test x86 isolato fermato.
- **Git:** nessun commit/push; `tasks/TASK-arm-ingame.md` resta untracked e le modifiche diagnostiche preesistenti in `win32_game.c` sono state preservate.
- Consulto `gpt-5.2-codex` e dettagli completi: `reports/REPORT-arm-ingame.md`.
