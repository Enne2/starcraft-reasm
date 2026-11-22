# STATE — sc_ctl API
- Implementato: `sc_ctl.c/.h` (server Unix/TCP, comandi shot/click/move/down/up/key/text/state/quit),
  client `tools/scctl`, hook snapshot in `host_elf_present()`, incluso nei build x86 e ARM.
- x86: verificato end-to-end (`state` + `shot` 640x480 + click) fino a **Play Custom → Create con mappe**.
- ARM (Pinebook): API funzionante (screenshot dal framebuffer), ma lista mappe vuota e campagna bloccata
  su `standard.snp` → **non ancora in partita**.
- Fix: `win32_game.c` non usa piu' l'offset cablato 19 per `d_name` di `dirent64`.
- Report: `reports/REPORT-scctl-api.md`.
