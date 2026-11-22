# Report — scx86-play3 (21/11/2026)

## Esito

Il crash che seguiva `SnpQuery`/`Terran Episode I` (**guest `esp=0x08582010`, pc `0x1`**) è **risolto**
ed erano **due difetti distinti, entrambi di ABI di chiamata**, non uno. Dopo il fix il percorso
`Main Menu → Single Player → Original → Registry → Campagna → Terran Episode I` arriva a eseguire
**tutta** l'inizializzazione SNet (`SnpQuery` 0/1/2, `SnpBind index=0`, `spiInitialize`,
`spiLockDeviceList`, `spiUnlockDeviceList`, `spiStartAdvertisingLadderGame`) senza più SIGSEGV su
`SnpQuery`. Si apre un blocco **nuovo e diverso** (timer callback), diagnosticato fino all'istruzione.

## Difetto 1 — `SnpQuery`/`SnpBind` consegnate come simboli C

`win32_snp_get_proc()` restituiva `&snp_query` (funzione C, ABI cdecl, pila **host**) al codice
tradotto, che le invoca come export stdcall con argomenti sullo stack **emulato**. Evidenza GDB
(`build/qa/gdb-play3.log`): `snp_query` entra con `ECX=0x0E000000` (=`_cpu`) e `index=0x0E000000`,
argomenti letti dalla pila host → `SnpQuery` non poteva mai rispondere correttamente.

Fix:
- `runtime/common/bridge_dynamic.def`: `DYNAMIC(SnpBind)`, `DYNAMIC(SnpQuery)`.
- `runtime/shim-port/bridge_shims.def` + `shim_rename.txt` (manifest i686 riallineati, diff 0):
  `SHIM(SnpQuery, 5, 5, 0)`, `SHIM(SnpBind, 2, 2, 0)`.
- `runtime/shim-port/elf/snp_elf.c`: `SnpQuery`/`SnpBind` definite non-`static`; `win32_snp_get_proc`
  restituisce `arm_bridge_lookup_thunk(nome)` (stub con ABI del guest), 0 se lo stub manca.
- `runtime/shim-port/elf/win32_shim.c`: `GetProcAddress` risolve `win32_snp_get_proc` **per primo**
  (serve il modulo standard.snp) e rifiuta `SnpQuery`/`SnpBind` per altri moduli (nessuna
  risoluzione globale accidentale).

## Difetto 2 — tabella `NetFunctions` con puntatori C (causa di `0x08582010`/`pc=0x1`)

`g_functions` conteneva `&snp_initialize`, `&snp_receive`, …: il guest fa `call *slot` dal codice
tradotto, il dispatch accettava quell'indirizzo host (dentro il `.text` del port) e la funzione C
veniva eseguita con `ECX=_cpu` ma con la **pila host**: il suo `ret` tornava a `0x1` e lasciava
`0x08582010` (label host `loc_4D1C5B`) in `_esp`. Evidenza `build/qa/gdb-play3-fixed.log`:
`_ind_target=0x1802A600` = `snp_initialize`, `_ind_esp=0x021FF974`, poi `cpu_signal` con
`_esp=0x08582010` e `[esp]=0x0530A156`.

Fix (`snp_elf.c`): gli slot sono ora gli **stub guest-ABI** generati dall'import bridge, cercati per
nome (`g_slot_names[27]` → `g_slots[]`, `dwSize=0x70` in testa, `SnpBind` restituisce
`(struct SnpNetFunctions *)g_slots`). Le 27 implementazioni `snp_*` sono diventate globali (altrimenti
il linker le scarta: erano referenziate solo per nome) e le 27 voci sono `DYNAMIC()` in
`bridge_dynamic.def`. Se uno stub manca, `SnpBind` → FALSE con log (**D19**: mai un puntatore nativo
al guest, nessun allentamento di `reasm_indirect_resolve`).

## Verifica

`tools/build-shim-x86.sh` PASS (356 stub: HAND 9, SHIM 315, UNK 32; 0 collisioni globali; manifest
i686 identici ai file versionati), `./tools/build.sh --arch x86 --skip-entry` PASS,
`./tests/x86_runtime_check/check.sh` **PASS**. Log build `build/qa/build-play3.log`,
`build/qa/build-shim-play3.log`, check `build/qa/check-play3.log`.

## Nuovo blocco (aperto): TIMERPROC chiamata con ABI C

Dopo Terran il gioco va in SIGSEGV con firma **diversa** (`fault pc 0x08457DB1`, `si_addr 0x10`,
"procedura tradotta: entry VA 0x004A6030 offset 1", `esp 0x021FFBCC`, `_ind_target` non coinvolto).
Catena host misurata (`build/qa/gdb-play3-crash2.log`): `main → cpu_llasm_run_raw →
arm_com_bridge_common (slot 32 = s_Unlock) → present → host_elf_present → win32_input_pump →
host_elf_pump → win32_shim_timers_pump → 0x08457DB0` con **`ECX=0x0`**. Cioè
`win32_shim.c:451-474` chiama il TIMERPROC con l'ABI C (`cb(hwnd,msg,id,ticks)`) su un ingresso
tradotto fastcc: il prologo `mov 0x10(%ecx),%eax` legge `[0x10]`.
**Fix (non applicato, soglia di contesto)**: instradare il callback da `arm_guest_call4(proc, hwnd,
0x113, id, GetTickCount())` (vedi `win32_ui.c:950` per i WndProc). Nessun altro difetto noto residuo.

## Navigazione verificata (640×480, screenshot ispezionati con `read`)

`build/qa/play-screenshots/`: `play3-s1-menu.png`, `play3-s2-gametype.png`, `play3-s3-original.png`
(Registry), `play3-s4-selected.png`, **`play3-s5-after-ok.png` / `play3-gdb-campaign.png` (Campagna:
Terran 305,250 · Zerg 110,330 · Protoss 520,330)**, `play3-newid-typed.png` + `play3-newid-enter.png`
(creazione profilo nel Registry vuoto: `text enne2` poi **`key 13`**, il click su Ok del dialog non
basta), `play3-registry-list.png`. Neri da non usare: `play3-terran-result.png`, `play3-s6-terran.png`.

## Decisioni autonome

Ho corretto il generatore/dichiarazioni (manifest) invece di aggiungere un caso speciale nel
dispatcher; ho reso la tabella NetFunctions *fail-closed* (stub mancante ⇒ `SnpBind` FALSE) invece di
lasciare slot nativi; ho riallineato i manifest versionati ai file generati per azzerare il diff.
Nessun input su display reali, nessun audio, nessuna modifica a `vendor/`.

## Prossimo passo

1. `win32_shim_timers_pump`: `arm_guest_call4` per il TIMERPROC; rebuild + `check.sh`.
2. Rilanciare (`start --id play4 --skip-intro 1 --smack 1`) e ripetere la sequenza con pause ≥2 s:
   Campagna → `Terran Episode I` (305,250) → **selezione missione / Boot Camp** → Briefing → Start
   (attesi: schermata missione ~ (320,250)-(520,400); briefing Start ~ (520,400) da sc-reasm p12) →
   campo di battaglia (HUD). Salvare gli screenshot e verificare eventuali nuovi blocchi
   (`SetTimer`/`SNet` callback, race font `0x004D4DCA`).
3. `qmem_store`: il fix del timer e le coordinate missione/briefing/HUD.
