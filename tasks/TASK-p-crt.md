# Preludio obbligatorio (direttiva IDA + autonomia)

1. **Oracolo primario (IDA)**: usa `llvm-objdump -d` su `build/lift-full/storm.orig.dll` (all'indirizzo esatto di ciascun `crt_unlifted_1504xxxx`), `llvm-readobj` e `rg` su `build/lift-full/storm.llasm` / `storm.orig.dll.i64` (se IDA risponde; controlla `ida_list` prima).
2. **Autonomia**: procedi subito senza chiedere conferme (`ask_user` vietato).
3. **Fail-closed (D19)**: non scrivere finti stub che tornano `0`/`1` alla cieca! Classifica ogni indirizzo per semantica reale dal codice macchina originale di `storm.orig.dll`.
4. **Divieti**:
   - NON toccare file fuori dalla tua proprietà! (`win32_shim.c`, `bridge_unk.def`, `bridge_table.def`, `build/starcraft-x86`, `build/obj-x86/`).
   - Niente `git`, niente sotto-agenti, niente input desktop.

---

# Missione `scx86-crt`: Censimento completo e implementazione dei 20 `crt_unlifted_1504xxxx` in `storm.orig.dll`

## File di proprietà (esclusivi)
- `runtime/common/storm_crt_unlifted.c` (nuovo)
- `runtime/common/storm_crt_unlifted.h` (nuovo)
- `tests/x86_crt_unlifted_test.c` (nuovo)
- `build/qa/crt/` (output compilazione/test privati)
- `reports/REPORT-crt-unlifted.md`
- `reports/STATE-crt-unlifted.md` (max 70 righe)

## Lista esatta dei 20 simboli (`runtime/common/bridge_unk.def`)
`crt_unlifted_1504048B`, `crt_unlifted_15040DF3`, `crt_unlifted_15040DF7`,
`crt_unlifted_15041A41`, `crt_unlifted_15041B81`, `crt_unlifted_15041D4D`, `crt_unlifted_15041D52`,
`crt_unlifted_15043005`, `crt_unlifted_15043058`, `crt_unlifted_150430B9`, `crt_unlifted_150430E4`,
`crt_unlifted_1504311F`, `crt_unlifted_15043177`, `crt_unlifted_15043197`, `crt_unlifted_150431CF`,
`crt_unlifted_15043224`, `crt_unlifted_15043241`, `crt_unlifted_15043271`, `crt_unlifted_15043289`,
`crt_unlifted_1504339F`.
(Nota: `crt_unlifted_15040E07` è già implementato in `bridge_table.def` / `crt_init_arm.c` — controlla come è fatto!)

## Obiettivi
1. **Disassemblaggio e classificazione di tutti i 20 VA in `build/lift-full/storm.orig.dll`**:
   - Per ciascuno dei 20 indirizzi (`0x1504048B .. 0x1504339F`), disassembla con `llvm-objdump -d` in `storm.orig.dll` e trova tutti i call-site/xref in `storm.llasm` / `storm.orig.dll`!
   - Identifica la funzione MSVC CRT reale (es. `_EH_prolog`, `__except_handler3`, `__global_unwind2`, `__local_unwind2`, `__alloca_probe`, `__ftol`, `__allmul`, `__alldiv`, `_initterm`, `__setusermatherr`, `_amsg_exit`, ecc.), il numero di argomenti, i registri d'ingresso/uscita (es. `eax`/`ecx`/`edx`/`ebp` se MSVC `__fastcall` / custom CRT helper invece che `stdcall`!), e se è raggiungibile a runtime.
2. **Implementa in `runtime/common/storm_crt_unlifted.c`**:
   - Prendi a modello `crt_unlifted_15040E07` in `runtime/common/crt_init_arm.c` (leggi `crt_init_arm.c`!).
   - Per ogni helper raggiungibile con semantica chiara e portabile su `_cpu*` (e compatibile sia `__i386__` sia `__arm__`), implementa la funzione `void crt_unlifted_<VA>_body(_cpu *cpu)` (o la forma prevista da `bridge_table.def` / `crt_init_arm.c`)!
   - Per eventuali stub SEH/eccezioni fatali CRT (es. `_amsg_exit` / `abort` / `__except_handler3`), implementa una gestione fail-closed esplicita e diagnostica (senza silenziare errori).
3. **Compila e testa (`clang --target=i686-unknown-linux-gnu -m32 -fno-pie`)**:
   - Compila `storm_crt_unlifted.c` + `tests/x86_crt_unlifted_test.c` in `build/qa/crt/` e verifica ogni funzione implementata!
4. **Report, STATE e `qmem_store`**:
   - Documenta la tabella completa di tutti i 20 VA (`VA`, nome MSVC CRT, calling convention, call-site in Storm, implementato vs fail-closed SEH) in `reports/REPORT-crt-unlifted.md`, lista esatta delle entry per `bridge_table.def`/`bridge_unk.def`, `reports/STATE-crt-unlifted.md` e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
