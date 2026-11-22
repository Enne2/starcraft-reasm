# Report — scx86-play4 (21/11/2026)

## Esito

Fix TIMERPROC, fix del kernel SCode senza LUT e percorso visivo fino al briefing Boot Camp completati. La build supera il check runtime. Dal briefing, **Start** porta a un nuovo STOP fail-closed su target indiretto `0x0435A008`; non è stata raggiunta la schermata di gioco. Per richiesta del supervisore la sessione è stata fermata subito dopo il check corrente, senza tentare una diagnosi GDB del nuovo target.

## Fix TIMERPROC

`runtime/shim-port/elf/win32_shim.c`: `win32_shim_timers_pump()` invoca il `TIMERPROC` guest con `arm_guest_call4(proc, hwnd, 0x113, id, GetTickCount())`, weak su ARM/i386, come il percorso WndProc di `win32_ui.c:949-950`; mantiene il fallback ABI host solo se il guest-call non è linkato. Questo elimina il crash ABI precedente al timer.

## Diagnosi SCode — oracolo e GDB isolato

Conferma ordinal: `llvm-readobj --coff-exports build/lift-full/storm.orig.dll` indica export **#334, RVA 0x30AB0**, quindi VA `0x15030AB0`; `storm.llasm:1393` definisce `loc_15030AB0 ordinal_73746f726d2e646c6c_334`. `externals-storm.txt` è la lista degli esterni/importi e non è la tabella export; #334 non vi compare. PE disassemblato con `llvm-objdump -d` in `0x15030AD4–0x15030CEA`: Storm costruisce/patcha descriptor e salti SCode; al `0x15030D15–0x15030D2A` prepara parametri e salta al kernel generato. Il bytecode, non un LUT sempre obbligatorio, determina quali dati il kernel legge.

Breakpoint GDB isolato su `storm_sdraw_arm.c:397 if base==0`, log `build/qa/gdb-play4-scode2.log`:
- `a[0]=0x201210d8`, `p=0x20f90298`; `p[0]=0x40` (flag accettato), larghezza `60`, righe `56`, `dst_delta=120`, `src_delta=0`, `dst=0x21589e28`, `src=0x1c514e50`.
- Descriptor flags `a[0]+8 = 0x04000000`; SCode1 `0x201600fc`, size `0x0b`; SCode2 `0x201737cc`, size `0x12c5`; code3 nullo. `base = *(p+0x20) = 0`.
- SCode1 bytes: `C7 C0 00 00 00 00 E9 00 00 00 00`. SCode2 inizia `8A 06 FF C6 8A E0 66 89 07 81 C7 02 00 00 00`, con kernel generato ripetuto. Semantica x86: leggi byte sorgente in AL, incrementa ESI, copia AL→AH, scrivi AX a destinazione, incrementa EDI di 2. È un kernel di conversione pixel byte→word senza accesso a LUT.
- Chiarimento: il vecchio log `ROP=0 flags=0` proviene dagli argomenti del formatter fail73 (`rop=0`, `base=0`); **non** rappresenta `p[0]`/descriptor flags, che sono rispettivamente `0x40` e `0x04000000`.

Fix in `runtime/common/storm_sdraw_arm.c`: tolto il requisito globale della LUT 64 KiB; il kernel SCode può avere base nullo se non la legge. L'interprete implementa le istruzioni osservate `8A 06` e `8A E0`; gli accessi dati/LUT dell'interprete sono convalidati con `proc_range` al punto d'uso. Il fast path LUT fingerprinted continua a richiedere l'intera tabella 64 KiB prima di eseguire. Opcode sconosciuti, accessi non mappati e kernel non fingerprinted restano fail-closed.

## Navigazione e schermate (640×480)

Runner esclusivamente `./tools/run-isolated.sh start --id play4 --skip-intro 1 --smack 1`, Xvfb privato/audio dummy. Sequenza: menu → Single Player `(220,120)` → Original `(260,226)` → profilo `enne2ù` `(100,109)` + OK `(517,391)` → Terran Episode I `(305,250)` → story “TERRAN CAMPAIGN: REBEL YELL” → click `(320,440)` → title card **BOOT CAMP** (“Office of the Colonial Magistrate”, “Mar Sara Colony”, `2:25 SCT, 12.12.2499`).

Dopo il fix SCode, il gioco prosegue da solo al briefing. `build/qa/play-screenshots/play4-17-after-scode-fix.png` (ispezionato): pannello “Objectives” mostra “Build 3 Supply Depots”, “Build a Refinery”, “Gather 100 Vespene Gas”; pulsante **Start** a circa `(538,390)`, “Skip Tutorial” `(150,445)`, “Replay” `(380,419)`, “Cancel” `(545,430)`. L’interfaccia è renderizzata solo parzialmente/nera in alcune finestre, ma obiettivi e pulsanti sono visibili. Screenshot precedenti: `play4-11-menu-rebuild.png` … `play4-16-boot-camp.png`.

Click Start `(538,390)` termina con STOP: salto indiretto non tradotto, `target=0x0435A008`, `eip chiamante=0`, `esp=0x021FF9E8`, `_eax=0x0066FFA0`, `_ecx=0xFFFFFFFA`. Catena ring include gli ingressi guest `0x0046A4D2`, `0x0046A427`, `0x0046A45A` e il passaggio `0x0040912C`; il target non è in mappa guest né nelle sezioni eseguibili tradotte. Evidenza: `build/isolated/play4/game.log`; screenshot nero dopo Start: `build/qa/play-screenshots/play4-18-after-start.png`. **Briefing raggiunto; partita/HUD/SCV/Command Center non verificati.**

## Verifiche, isolamento e decisioni

`./tools/build.sh --arch x86 --skip-entry`: **PASS** (`build/qa/build-play4-scode.log`). `./tests/x86_runtime_check/check.sh`: **PASS**. Nessun audio reale o display utente. `./tools/run-isolated.sh stop --id play4` eseguito; PID verificato assente con `ps`.

### Decisioni autonome

Ho mantenuto D19: niente target arbitrari o stub per `0x0435A008`. Il fix SCode è limitato a semantica osservata nel bytecode originale e mantiene verifiche memoria a runtime. Nessun input dopo lo STOP; la sessione è stata chiusa immediatamente su richiesta del supervisore.

## Prossimo passo

Rilanciare `play4` isolato e attaccare GDB prima della navigazione; impostare breakpoint condizionale `reasm_indirect_unresolved` su `target==0x0435A008`. Da lì acquisire CPU/stack emulati, backtrace host, valore che genera il target e memoria dell’oggetto/indirect-call site; correlare il chiamante `0x0046A4D2` e il percorso `0x0040912C` con il PE originale via `llvm-objdump -d`/IDA. Correggere solo dopo aver provato la causa senza allentare `reasm_indirect_resolve`, poi rebuild + runtime check e riprendere da Boot Camp → Start.
