# Stato — intro video ARMv7

- **Causa:** non determinata. Il frame Smacker decodificato e il percorso SCode 2× LUT producono contenuto non-zero secondo la sonda interna; una cattura accoppiata ARM riporta invece canvas indicizzato tutto zero e RGB nero. Non attribuire la causa alla palette senza un readback sincronizzato.
- **Overrun:** escluso per i valori osservati: `320×308 = 98560 = 0x18100`. Il dato 98304 è `0x18000`, non `0x18100`. La build controlla ora l'estensione allocata prima della copia.
- **Deploy ARMv7:** `/home/enne2/starcraft-armv7-test/starcraft-armv7`, SHA256 `9164a7a6ad42d438acb1f956f7cd622dcce404e24dd0cef8303a7e32aebc83a3` (uguale al locale). Processo lasciato avviato con `SC_SKIP_INTRO=1`; socket di controllo risponde.
- **x86:** build, `x86_scomp_test`, `x86_runtime_check` PASS. Cattura intro visibile: `build/qa/intro-video/x86-pair.jpg` (compatta); ARM nera: `build/qa/intro-video/arm-final.jpg` (compatta).
- **Modifiche:** `runtime/shim-port/elf/sc_ctl.c`, `runtime/shim-port/elf/smack_elf.c`, `runtime/common/storm_sdraw.c`, `tools/scctl`, `tests/sc_ctl_test/check.sh`.
- **Non verificato:** partita ARM `(2)Bottleneck.scm`; conferma visiva del menu dopo `SC_SKIP_INTRO=1`; root cause del disallineamento SCode→canvas/readback.
- **Prossimo passo:** correlare in un unico frame il canvas dopo SCode e dentro `sc_ctl_present`, poi seguire clear/handoff/present. Evitare altre modifiche speculative a palette/SCode.

## Decisioni autonome

Conservare la build diagnostica deployata e lasciare `SC_SKIP_INTRO=1` come workaround operativo; nessun'altra modifica comportamentale finché il readback sincronizzato non localizza la perdita.
