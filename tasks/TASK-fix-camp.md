# Preludio obbligatorio
- Oracolo primario: IDA (`StarCraft.orig.exe` / `storm.orig.dll`), `llvm-objdump`, `gdb`.
- **NON modificare `runtime/`, `tools/`, `build/obj-x86/` né `build/starcraft-x86`.** Lavora solo in `build/qa/b2-camp/` e `reports/`.
- Usa SOLO `./tools/run-isolated.sh` (zero finestre su `:0`, zero suono).
- NON lanciare altri sotto-agenti.

# Missione: Risolvere Bug 2 — Personaggi neri in idle su Campaign Select

Le evidenze già raccolte in `reports/SOL-b2-camp.md` mostrano:
- In `build/qa/b2-camp/StarCraft.orig.exe.i64`: `sub_4EF100` (`0x4EF100`) è il flusso Campaign Select, chiama `sub_4EE2D0` (`0x4EE2D0`), `sub_4EEC30`, `sub_4EEE00`, `sub_4EED10`.
- `sub_4EE2D0` carica `dlgs\terran.grp`, `dlgs\protoss.grp`, `dlgs\zerg.grp` tramite `sub_4197B0` e callback `sub_4D2B30`.
- In `idle.png` i 3 personaggi/artwork sono neri; in `hover.png` l'artwork sotto il cursore compare a colori.

1. **Analisi oracolo IDA + Trace mirata (`SC_B2_TRACE=1`, NO SCode dump voluminosi!):**
   - Decompila in IDA `0x4EF100`, `0x4EE2D0`, `0x4EEC30`, `0x4EEE00`, `0x4EED10`, `0x4D2B30` e `0x4197B0` (su `build/qa/b2-camp/StarCraft.orig.exe.i64`) per capire:
     - Cosa disegna i 3 personaggi in **idle** (GRP statico? SMK al frame 0? `storm_457` / `SDraw` / `SMK` / `SBlt` / palette / `GetTickCount` / `Update`?) vs **hover** (`SmackOpen` / `storm_457`?)!
   - Strumenta in `build/qa/b2-camp/src/` SOLO una trace leggera (`SC_B2_TRACE=1`) che logghi in `Campaign Select` (senza stampare i milioni di SCode) le chiamate di `SmackOpen` / `SmackWait` / `SmackToBuffer` / `storm_313` / `storm_334` in idle vs hover, oppure metti un breakpoint GDB su `0x08xxxxxx` (`sub_4EF100` / `sub_4D2B30` / `sub_4EE2D0`) per vedere esattamente quale ramo diverge in idle!
2. **Prototipo & Verifica visiva su binario privato (`build/qa/b2-camp/starcraft-x86-b2`):**
   - Ricorda la ricetta esatta di link (`tools/link-x86.sh`): path **assoluti** di `$PWD/build/lift-full/entry-x86/{StarCraft,storm}.entry.o`, `--redefine-syms=runtime/shim-port/shim_rename.txt` su ogni shim `.c` ricompilato, `guest_data_link.ld` + `runtime_bounds.ld`!
   - Verifica la navigazione passo-passo con `shot` + `read` fino a Campaign Select, cattura `idle-fixed.png` (mouse `10,10`) e `hover-fixed.png` (mouse `130,220`), e dimostra con `read` che i personaggi/pod sono visibili anche da fermi!
3. **Consegna:**
   - Ferma `./tools/run-isolated.sh stop --id b2-camp-fix`.
   - Salva il diff pulito relativo alla root in `build/qa/b2-camp/fix.patch`.
   - Scrivi `reports/SOL-b2-camp.md` e `reports/STATE-b2-camp.md` (max 70 righe) e salva in `qmem_store` (`project_id="starcraft-reasm-generic"`).
