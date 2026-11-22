# Report — runtime ARM main/init e target 0x00427714

## Modifica

In `runtime/arm/game_main_arm.c` ho dichiarato e invocato `win32_shim_init()` all’inizio di `main()`, prima di qualsiasi lavoro del guest. Lo shim inizializza `g_trace` da `REASM_TRACE` e azzera/configura `g_si` (`STARTUPINFO`); senza la chiamata il tracing dello shim restava disabilitato e lo stato d’avvio non era preparato.

Confronto init con `build/runtime-arm/shim-port/tools/elf/main_elf.c`:

- `win32_shim_init()` — aggiunta: inizializza stato globale dello shim.
- `loader_init()` — non aggiunta. Il runtime ARM esegue la funzione equivalente distribuendo la registrazione delle regioni PE, la mappatura/copia degli header e dei gap nelle fasi 1–2 di `game_main_arm.c`; chiamare anche il loader x86 duplichererebbe una strategia di mappatura diversa.
- `teb_init()` — non aggiunta. `cpu_llasm_create()` → `cpu_llasm_init_at()` inizializza il TEB e imposta lo stack guest (`arm_teb_init`, `arm_teb_set_stack`).
- `reasm_iat_install()` — non aggiunta. Nel port ARM gli import sono risolti staticamente nei bridge, come documentato nell’entry; non esiste l’IAT x86 runtime da installare.
- `win32_ui_init()` — non aggiunta. Non è chiamata dal `main()` x86: l’inizializzazione UI è demandata ai percorsi UI dello shim quando richiesti.
- DllMain Storm e reset della crypt table — già presenti in `storm_loader_init()`.

Non sono stati modificati `win32_shim.c`, `dlg_elf.c` o `build/lift-full/code_entry_points.sci`.

## IDA: 0x004207B0 e 0x00427714

Analisi sul solo DB del progetto `build/lift-full/StarCraft.orig.exe.i64` (sessione IDA dedicata `99fec89c`).

- `0x004207B0` è l’inizio della funzione reale `sub_4207B0` (dimensione IDA `0x78`). A `0x004207EF` c’è `call edx`, callback presa dalla tabella globale `dword_6D5DD8` (slot `dword_6D5DD8 + 4*index`); `0x004207F1` rilegge la tabella dopo il ritorno. Dunque il chiamante segnalato è il runner di callback/distruttori, non un salto proveniente da un vtable nullo.
- `0x00427714` appartiene a `sub_427540`, già presente tra gli entry SCI come `loc_427540` (nessuna nuova funzione o entry autonoma aggiunta). I byte/istruzioni rilevati da IDA attorno al target sono:
  ```asm
  0x0042770E  call sub_488900
  0x00427713  inc eax
  0x00427714  cdq
  0x00427715  sub eax, edx
  0x00427717  sar eax, 1
  ```
  **Istruzione esatta a `0x00427714`: `cdq`.** È codice valido e un punto di continuazione sequenziale interno a `sub_427540`, non l’inizio di una funzione separata né un vtable nullo. Nota: il `call` vicino a `0x0042770E` ritorna a `0x00427713`; l’esecuzione prosegue con `inc eax` e raggiunge `cdq` a `0x00427714`. Il runner `sub_4207B0` chiama però un puntatore callback indiretto, quindi per attribuire perché quel puntatore abbia proprio il valore `0x00427714` servirebbe osservare il contenuto dello slot runtime; IDA da sola non dimostra che sia un distruttore virtuale.

## Build e verifiche

- Build richiesta, con nome isolato: `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-main` — completata; ricompilato `game_main_arm.c`, link ARM creato. Un warning preesistente di commento annidato alla riga 40 non ha impedito il link.
- `make check` — **PASS**: toolchain up-to-date; test freestanding ARMv6 sotto QEMU; gate PE ordinale/Wine e LLASM→ARM1176; attributi ARMv6/VFPv2 verificati.
- Test da `build/asset-sandbox`: `timeout 20s env REASM_TRACE=1 qemu-arm-static -cpu arm1176 -L ../sysroot ../starcraft-armv6-main` — il tracing è effettivamente attivo: log con `[trace] ReadFile`, `[file]` per `Stardat.mpq`, `patch_rt.mpq` e `BroodWar.mpq` (1353 righe `[trace]`/`[file]` complessive nel log catturato). Il processo non completa: termina con SIGSEGV (exit 9) dopo `CreateWindowExA`; diagnosi runtime: guest entry `0x004D1D70` + offset `0x14`, fault address `0x010027FC`, PC `0x1864C810`, LR `0x18A0461C`. Questo blocco successivo non è stato corretto perché esula dai file autorizzati.
- SHA-256 `build/starcraft-armv6-main`: `ef3e59a7546c2bfaebd295acbb48057d17f6795a359aadffd69e7cf966c73a08`.

## File toccati

- `runtime/arm/game_main_arm.c` — dichiarazione e chiamata `win32_shim_init()` più commento sul confronto init.
- `work/link/REPORT-arm-main.md` — questo report.

Gli artefatti di build richiesti sono stati creati in `build/lift-full/entry/` e `build/starcraft-armv6-main`.

## Decisioni autonome

Ho aggiunto soltanto `win32_shim_init()`: gli altri init x86 hanno già equivalenti ARM o sono sostituiti dagli import statici. Non ho aggiunto entry per `0x00427714`, perché IDA lo colloca nel corpo già indicizzato di `sub_427540`; il punto interno può essere investigato separatamente senza toccare file fuori ambito.

## Follow-up: SIGSEGV durante la creazione della finestra (20/11/2026)

### IDA: identità e chiamanti di `0x004D1D70`

Analisi eseguita sulla sessione già aperta del solo DB progetto `build/lift-full/StarCraft.orig.exe.i64` (`99fec89c`). IDA identifica `0x004D1D70` come `sub_4D1D70`, funzione `int __stdcall(HWND, UINT, int, LPARAM)`, dimensione `0x840`: è la WndProc della finestra principale. Il decompilato completo gestisce i messaggi Win32; per `WM_CREATE` (1) non ha un ramo speciale e arriva al default `DefWindowProcA`.

Xref dati unico a `0x004D1D70`: `0x004E05DA`, in `sub_4E05B0`, dove viene assegnata a `WNDCLASSEXA.lpfnWndProc`. La funzione chiamante `sub_4E05B0` è a sua volta chiamata da `sub_4E0820` (`0x004E083D`). La medesima funzione esegue `GetSystemMetrics(0)` a `0x004E0674` e passa il risultato come `nWidth` a `CreateWindowExA` a `0x004E0688`: `eax=0x280` è quindi il valore normale di larghezza 640 pixel, non un puntatore o un codice errore. La finestra richiesta è 640×altezza schermo.

### Istruzione del fault e stack

Disassemblato del candidato locale `build/starcraft-armv6-main` con `llvm-objdump` (ELF VMA, non RVA):

```asm
0x1864C7FC  loc_4D1D70:
0x1864C800  ldr r1, [r0, #0x10]       ; _cpu->_esp
0x1864C804  ldr r2, [r0, #0x14]       ; _cpu->_ebp
0x1864C810  str r2, [r1, #-0x4]!      ; push guest EBP, poi aggiorna guest ESP
```

La scrittura fault a `0x010027FC` è esattamente `--ESP` con `ESP=0x01002800`. Il port definisce lo stack guest a partire da `ARM_STACK_BASE=0x02000000` (massimo per stack `0x00400000`); `0x010027FC` non appartiene a quello stack e la store fallisce. Non è evidenza di accesso TEB/SEH né DirectDraw.

### Causa dimostrata: callback WndProc invocata fuori dall’adattatore guest

IDA e il log collocano il fault dentro la WndProc chiamata durante `CreateWindowExA`/`WM_CREATE`. Il disassemblato ARM di `CreateWindowExA__c` a `0x18A04618` mostra `blx r7` per la callback; **non** passa da `arm_guest_call4`. L’oggetto shim in questo candidato quindi chiama una funzione di codice guest come callback nativa, invece di usare il call-out che prepara/ripristina lo stack emulato.

Conferma nel sorgente non modificato `build/runtime-arm/shim-port/tools/elf/win32_ui.c`: `SendMessageA` (righe 934–938) chiama direttamente `p(hwnd,msg,wp,lp)`. Il progetto contiene già l’adattatore corretto in `runtime/arm/patches/0002-guest-callbacks.patch`: sotto ARM inoltra la WndProc a `arm_guest_call4(proc,hwnd,msg,wp,lp)`. La catena runtime-only copia gli oggetti shim precompilati da `build/runtime-arm/game/obj` (vedi `tools/build-entry-obj.sh`); la sequenza `blx r7` osservata dimostra che il relativo oggetto non contiene quel ramo di adattamento. L’errore è quindi nel confine callback dello shim UI / nel suo oggetto compilato, non nel codice `runtime/arm/` che gestisce il TEB o DirectDraw.

### Handoff e perimetro

**Assegnare a un agente proprietario di `win32_ui.c`/build degli shim:** rigenerare l’oggetto UI includendo `0002-guest-callbacks.patch` (e verificare che il call-site usi `arm_guest_call4`); poi ripetere il boot. Non ho modificato `win32_ui.c`, `com_ddraw.c`, `win32_shim.c`, `dlg_elf.c` né altri file runtime/build: il fix è fuori dal perimetro assegnato. Il binario esaminato mantiene SHA-256 `ef3e59a7546c2bfaebd295acbb48057d17f6795a359aadffd69e7cf966c73a08`; il report è aggiornato per il passaggio di consegne.

## Follow-up: callback guest VA/host e ritorno sentinel

### Verifica e correzione in `runtime/arm/guest_call_arm.c`

Letto per intero il file. Prima della correzione, `guest_call()` respingeva subito ogni `proc` per cui `arm_va_is_mapped(proc,4)` era falso; soltanto dopo chiedeva `reasm_indirect_resolve()`. Quindi:

- un VA guest, ad es. `0x004D1D70`, passa la mappa e viene risolto dalla tabella VA→entry;
- un puntatore host tradotto, ad es. `0x1864C7FC` memorizzato come `ptrtoint(@loc_4D1D70)` nel `.data` liftato, veniva respinto prima che il resolver potesse riconoscerlo nella finestra `.text`.

Ho rimosso il pre-filtro: ora entrambi i formati passano da `reasm_indirect_resolve()`. Questo mappa il VA guest tramite tabella e accetta un target host solo nel range di `.text`; target non risolti continuano a fallire in modo controllato (`status=-4`). Nessun cambiamento a `indirect_dispatch.c`.

Ritorno del guest: il `ret 0x10` carica il return target (sentinel) e aggiusta l’ESP guest; `tcall` scrive il target in `_cpu._ind_target` e passa da `reasm_indirect`. Nel binario linkato i limiti sono `reasm_text_lo=0x18000000`, `reasm_text_hi=0x18A078B0`; `arm_guest_call_sentinel=0x189CFC60`, quindi il sentinel è incluso nel range. `reasm_indirect_resolve()` lo riconosce al ramo “host text” e `reasm_indirect.S` fa `bx` con `r0=_cpu`. Il sentinel esegue `longjmp`; `guest_call_at()` decrementa `g_depth` e ripristina `saved_esp`. La sequenza è coerente anche quando l’entry WndProc proviene da un puntatore host relocato.

### Patch shim e artefatti datati

Confronto read-only di `0001` e `0003` con `build/runtime-arm/shim-port/tools/elf/` e gli oggetti:

- `0001-i386-only-seams-to-arm.patch`: le guardie ARM sono presenti nei sorgenti correnti (`win32_shim.c`: getdents64/statfs/TEB; `win32_game.h`: supporto ARM e rimozione stdcall; `main_elf.c`: ramo crash handler ARM). Il dry-run non è pulito solo sul vecchio hunk path di `win32_shim.c`, evoluto dalle modifiche CI/path; gli altri hunk risultano già applicati. Non emerge una regressione ARMv6 da quel confronto.
- `0003-getprocaddress-bridge.patch`: il dry-run forward si applica; **manca** nel `win32_shim.c` corrente e l’oggetto `win32_shim.o` non ha riferimento a `arm_bridge_lookup_thunk`. GetProcAddress può quindi restituire indirizzi C non adattati al guest ABI: residuo concreto da assegnare al proprietario dello shim.
- `0002-guest-callbacks.patch`: `win32_ui.c` e il suo oggetto sono ora aggiornati dall’agente `sc-arm-dlg`; il nuovo `win32_ui.o` ha il riferimento weak a `arm_guest_call4`. Il `dlg_elf.c`/`dlg_elf.o` nella cartella `shim-port` non mostra invece quell’adattatore. La chiamata DialogBox del runtime ARM ha già il thunk dedicato in `bridge_arm.c`, quindi l’impatto va valutato dall’agente dialog; non ho toccato quei file.
- Gli oggetti `gdi_elf.o`, `com_ddraw.o`, `host_elf.o`, `input_elf.o`, `smack_elf.o`, `smack_audio.o`, `snp_elf.o`, `storm_ext.o`, `win32_game.o` datati 01:10 sono vecchi, ma `readelf -A` riporta per tutti `Tag_CPU_arch: v6` e `Tag_ABI_VFP_args: VFP registers`: non risultano incompatibili con ARMv6/VFP. Non sono target dei due patch 0002/0003. `storm_ext.o` è però un residuo legacy: stage 8 lo esclude esplicitamente, mentre `tools/link-arm.sh` collega con glob tutti gli oggetti di `shim-port/obj`; annotato come incoerenza di selezione/link, non causa dimostrata del fault.
- `tools/build-runtime-arm.sh` stage 8 applica 0001/0002/0003 su una copia e compila gli shim in `build/runtime-arm/game/shim/obj`; invece `tools/link-arm.sh` usa `build/runtime-arm/shim-port/obj`. Quindi gli oggetti stage 8 corretti non sono quelli che il link entry/runtime-only consuma; controllare questo wiring nel task del proprietario degli shim.

### Verifica dopo il fix

- `./tools/build-entry-obj.sh --runtime-only --no-smoke --link-out build/starcraft-armv6-win` — PASS, `guest_call_arm.c` ricompilato e link completato.
- `make check` — **PASS**, incluso smoke LLASM→ARM1176 e attributi ARMv6/VFPv2.
- Smoke da `build/asset-sandbox` con `SC_DLG_AUTOCLOSE=1 REASM_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`: apre `Stardat.mpq`, `patch_rt.mpq`, `BroodWar.mpq`, `Broodat.mpq`, crea la finestra 640×480 e procede a leggere impostazioni/attivare la finestra; non si ripresenta il fault all’entry WndProc. Si ferma poi con un **nuovo SIGSEGV** in Storm (`guest entry 0x15040E07 + 0xA880`, PC `0x189D5F4C`, LR `0x189D5F18`, fault `0x7BA`, ESP `0x021FF9C4`). È successivo al callback fix e non è stato analizzato/corretto in questo task.
- SHA-256 `build/starcraft-armv6-win`: `17db74cadd0c9b848cea9316f65961b90e4305c6a8faf3a7207a412a8e5aad78`.

File modificato per questo follow-up: `runtime/arm/guest_call_arm.c`; aggiornato anche questo report. Nessun altro sorgente modificato.
