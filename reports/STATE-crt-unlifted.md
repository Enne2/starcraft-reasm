# Stato audit CRT unlifted

- Esito: 0/20 indirizzi sono funzioni CRT autonome confermate; nessun body implementato.
- Rilevati ret/epiloghi, istruzioni interne a helper x87/parser e una jump-table (`0x1504339F`).
- Call IR rilevanti: `loc_15042FFE` dispatcha al blocco `0x15043271` e carica target dalla tabella `0x1504339F`.
- Tutti i 20 alias/declaration in `storm.entry.ll` non implicano entry callable autonome.
- Deliverable: `runtime/common/storm_crt_unlifted.c/.h`, `tests/x86_crt_unlifted_test.c`, `reports/REPORT-crt-unlifted.md`.
- Test di compilazione da eseguire: clang i686 senza link; non è un test semantico.
- Nessun file fuori proprietà modificato. Nessun bridge mapping alterato.
- Limite: call/reference per ogni alias nei corpi IR non sono stati enumerati separatamente oltre ai due target del dispatch verificati; interpretazione basata su bytes originali + alias/declarations.
