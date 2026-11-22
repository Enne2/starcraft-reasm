# Stato — scx86-g-gfx2

- Build privata i686 di com_ddraw/gdi/Smack/SDraw riuscita; `starcraft-x86-gfx` ELF32 linkato in `build/qa/gap-gfx/` senza toccare i binari protetti.
- `tests/x86_gfx_test.c` compilato ma FALLISCE alla seconda CreateSurface: ABI dei cast COM non ancora corretta. Test pixel e conversioni non dimostrati.
- Avvio isolato: SIGSEGV guest (`pc=0x188717A4`, guest EIP=0, fault `0x1505FC20`) prima della UI. Nessuna diagnosi/fix briefing, nessuna Refinery.
- `gap-gfx` fermato; game/Xvfb spenti. D19, `proc_range`, SCode invariati. Nessun file vietato modificato.
- Dettagli: `reports/REPORT-gap-gfx.md`.

## Decisioni autonome
- Fail-closed: fermarsi sul crash e non forzare il gameplay o disabilitare controlli. Riprendere dopo correzione ABI del test e diagnosi del fault.
