# Report — scx86-play2 (21/11/2026, seconda passata)

## Esito

**`0x90909090` risolto** (causa radice dimostrata, D19 intatto) e due blocchi successivi dell'interprete
SCode risolti. La navigazione ora arriva a: Main Menu → Single Player → `StarCraft Original` →
**Registry con il profilo `enne2ù` listato** → **Campagna: Terran Episode I**. Il click su Terran
apre un nuovo blocco (SIGSEGV host `pc=0x1`), descritto sotto: è il punto di ripresa.

## Causa radice di `0x90909090`

Tabella di salto *dentro il .text guest* la cui base coincide con l'ingresso di una funzione tradotta.
Oracolo PE (`llvm-objdump` su `build/lift-full/StarCraft.orig.exe`, `memcpy` a `0x00406BF0`):
`0x00406C3D jmp *0x00406C50(,%eax,4)`, `0x00406C44 jmp *0x00406D4C(,%ecx,4)` — con indice
**negativo** (`0xFFFFFFFC..`) che ricade sulla tabella reale a `0x00406D3C` — e
`0x00406C4C jmp *0x00406CD0(,%ecx,4)`; le tabelle contengono VA guest (`0x00406D4C`, `0x00406D54`,
`0x00406D60`, `0x00406D74`, e `0x00406CF0..0x00406D33`). llasm risolveva la base a
`ptrtoint (void(%_cpu*)* @loc_406D4C to i32)`, cioè l'indirizzo **host** `0x0802D530`: la `load`
leggeva codice tradotto e quattro byte `0x90` di padding producevano il target `0x90909090`.

Evidenza GDB (hardware watchpoint su `_cpu._ind_target == 0x90909090`):
`build/qa/gdb-play2-table-load-writer.log` — `eip 0x802CE4B` (`loc_406C10+43`),
`mov 0x802d530(,%edx,4),%eax` / `mov %eax,0x88(%ecx)` / `jmp reasm_indirect`.
Stop fail-closed completo: `build/qa/gdb-play2-indirect-stop.log`.

## Fix applicati

1. `tools/dispatch-ir.py` — nuova regola `RE_TABLE_BASE`: `add i32 ptrtoint (void(%_cpu*)* @loc_VA to i32), X`
   diventa `add i32 <VA guest>, X`. **12 siti** (6 exe: `loc_406C4C+4`, `loc_406D4C`, `loc_406DD8+4`,
   `loc_40902C+4`, `loc_40912C`, `loc_4091B8+4`; 6 storm: `loc_150062BC+4`, `loc_150063BC`,
   `loc_15006448+4`, `loc_1500B47C+4`, `loc_1500B57C`, `loc_1500B608+4`). La lettura avviene ora dalla
   tabella reale del guest (`.guest_exe_text_0` mappata ai VA originali) e il valore caricato passa
   comunque da `reasm_indirect_resolve`: nessun target arbitrario, nessuna tolleranza aggiunta.
2. `runtime/common/storm_sdraw_arm.c` — l'interprete SCode (fail-closed) non copriva le forme
   **speculari** dei kernel LUT generati da Storm: aggiunte `8A 0E` (`mov cl,[esi]`), `8A 17`
   (`mov dl,[edi]`), `8B 0E` (`mov ecx,[esi]`), `8B 17` (`mov edx,[edi]`). Primo stop
   `unsupported x86 at 0x2016011A` (size1 `0x4A`, blocchi `8A 0E 8A 17 8A C1 8A E2 FF C6 8A 04 28
   88 07 FF C7 C7 C0 00000000`); secondo `0x20176A5A` (blocchi a 4 byte per passo `8B 0E 8B 17 … 89 1F`).
   Semantica x86 fedele, PC convalidato come prima: nessuno stub, nessun kernel eseguito "a fiducia".

Verifica: `tools/build-entry-x86.sh` PASS (dispatch `102376` VA, 0 collisioni; 12 basi corrette),
`tools/build.sh --arch x86 --skip-entry` PASS, `./tests/x86_runtime_check/check.sh` **PASS**.

## Navigazione verificata (640×480)

`Single Player` (220,120) → `Select Game Type`; `StarCraft Original` (260,226) → **Registry** con
`enne2ù` in lista; voce lista (100,109) + `OK` (517,391) → **Campagna** (`Terran Episode I` 320,250;
Zerg 125,330; Protoss 540,330). Screenshot in `build/qa/play-screenshots/`: `play2-campaign-select.png`,
`play2-registry-fixed.png`, `play2-registry-profile.png`, `play2-select-game-type.png`,
`play2-main-menu.png`, `play2-after-terran-crash.png`.

## Nuovo blocco (aperto)

Dopo il click su Terran: `[risorse] battle.snp`, `caps.dat`, `standard.snp`, `SnpQuery index>=2`, poi
`[arm-cpu] SIGSEGV`: fault pc `0x00000001`, si_addr `0x1`, **guest esp `0x08582010`** (indirizzo host
dentro `.guest_exe_code` finito nell'ESP emulato), `[cpu->_esp] = 0x0530A156`, eax `0x18029160`.
Log `build/isolated/play2/game.log`; GDB parziale `build/qa/gdb-play2-terran-crash.log`.

## Decisioni autonome

Ho fermato la passata alla soglia di contesto concordata, con `play2` chiusa. Ho scelto di correggere
il generatore IR (12 siti, non un workaround nel resolver) e di **estendere fedelmente** l'interprete
SCode invece di aggiungere un fingerprint o uno stub. Nessun input su display reali, nessun audio.

## Prossimo passo

Rilancio isolato (`start --id play2 --skip-intro 1 --smack 1`) con GDB `handle SIGSEGV nostop pass` +
`break cpu_signal`: stampare `bt`, `*(_cpu*)g_cpu_for_signal`, la pila emulata e il ring degli archi per
capire **chi ha scritto un indirizzo host in `_esp`** subito dopo `SnpQuery` (candidati: init rete
campagna, callback `arm_com_thunk`/`guest_call_at` con ABI stdcall, o un `ret` che ha consumato una
parola host). Poi fix mirato, rebuild, check e ripresa Terran → Tutorial/Missione 1 → Briefing → partita.
