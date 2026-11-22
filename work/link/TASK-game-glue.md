# TASK — game glue ARMv6: chiudere il link di StarCraft e provarlo

Repo: `/home/enne2/dev/starcraft-armv6` (lavora QUI, niente altri path).

## 0. Preludio obbligatorio (vincolante)

1. **Oracolo primario = il decompilato del binario originale.** Se serve capire *cosa* deve fare
   una funzione di glue, la fonte è il binario x86 (`StarCraft.exe` / `storm.dll`) letto in IDA,
   non l'inferenza. Nel dubbio: apri IDA (skill `/skill:ida`) sulla sessione del progetto, leggi la
   funzione per intero, cita l'indirizzo nel report. Se una scelta è dedotta e non verificata,
   scrivila come `[?]`.
2. **Autonomia totale.** Non chiamare `ask_user` (è escluso dai tool). Non chiudere un turno con
   una domanda o con un piano: decidi l'opzione conservativa, scrivila in `## Decisioni autonome`
   nel report e prosegui. Solo per azioni irreversibili scrivi `## BLOCCATO`.
3. **Divieti**: nessun sotto-agente, nessun comando git, nessuna modifica fuori dai file elencati
   al §4, nessun download, nessun `sudo`, nessuna interazione con QEMU o con il guest.
4. **Gate oggettivo** (è il criterio di accettazione, non un'opinione):
   - `./tools/link-arm.sh` esce **0** e stampa `creato: .../starcraft-armv6`;
   - `qemu-arm-static -L build/sysroot build/starcraft-armv6` viene eseguito e il suo
     comportamento (exit code, stdout/stderr, eventuale timeout) è riportato **verbatim**.

## 1. Obiettivo

Mancano **8 simboli** per chiudere il link. Implementali in **`runtime/arm/game_glue.c`** (nuovo file)
e fai linkare ed eseguire il binario.

```
build_guard_check
g_imports
g_imports_n
loc_404C21
reasm_call_on_stack
reasm_iat_install
reasm_storm_ordinal_va
winmain_trap
```

## 2. Fatti già verificati (non ridimostrarli, non rifarli)

- Gli shim sono **già compilati** in `build/runtime-arm/shim-port/obj/*.o` (14 oggetti) e il lato
  guest-ABI in `build/runtime-arm/game/obj/*.o`.
- Il bridge chiama i thunk `<Api>__c`; il rename è in `build/runtime-arm/game/shim_rename.txt` e
  **`tools/link-arm.sh` lo applica già** con `llvm-objcopy --redefine-syms` in una dir temporanea.
  Non riscriverlo, non toccare quel file se non per aggiungere un oggetto.
- Con il rename i simboli mancanti scendono da 443 a **8** (quelli del §1). È misurato.
- Le firme sono dichiarate in `runtime/arm/elf_support_arm.h`: **leggi quel file per primo**, le
  firme lì sono il contratto.
- Il link usa `ld.lld` (da rustup) perché `arm-none-eabi-ld` non conosce `armelf_linux_eabi`;
  libc/SDL2 vengono da `build/sysroot` (già predisposti dallo script).
- Toolchain di compilazione (verificata funzionante):
  ```
  clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard \
        --sysroot=build/sysroot -O2 -c runtime/arm/game_glue.c -o /tmp/game_glue.o
  ```
- `runtime/arm/x87_crt_arm.c` copre già i `crt_unlifted_*`: non riscriverli.

## 3. Come ricavare il comportamento di ogni funzione

Leggi **chi le chiama**, sono i contratti d'uso:
- `build/runtime-arm/shim-port/tools/elf/main_elf.c` (usa `winmain_trap`, `build_guard_check`)
- `build/runtime-arm/game/bridge_table.c` (usa `g_imports`, `g_imports_n`, `reasm_iat_install`)
- `runtime/arm/game_main_arm.c` (usa `winmain_trap`)
- `runtime/arm/bridge_arm.c`, `runtime/arm/guest_call_arm.c` (usano `reasm_call_on_stack`,
  `reasm_storm_ordinal_va`)
- `runtime/arm/elf_support_arm.h` (firme e semantiche documentate)

Regole pratiche di implementazione:
- `g_imports` / `g_imports_n`: tabella degli import (array di coppie nome→indirizzo) e conteggio.
  Se nessuno la popola, definiscila vuota e coerente (`g_imports_n = 0`); `reasm_iat_install`
  deve tollerare una tabella vuota senza crashare.
- `reasm_iat_install`: scrive nella memoria emulata gli indirizzi delle API risolte; con tabella
  vuota deve essere un no-op che ritorna 0.
- `reasm_call_on_stack`: chiama un puntatore a funzione guest leggendo gli argomenti dallo stack
  emulato (`%_cpu`); riusa i meccanismi già presenti in `guest_call_arm.c` invece di inventarne
  di nuovi.
- `reasm_storm_ordinal_va`: mappa un ordinal di storm.dll in un VA; se non risolvibile, ritorna 0
  e logga una volta sola (evita spam).
- `winmain_trap`: chiama l'entry del gioco con la CPU emulata preparata; è il punto in cui parte
  WinMain. Se l'entry non è ancora nota, individua il VA nel lift (`loc_4F7C8D` è il candidato:
  è il `start` del CRT dell'exe) e documenta la scelta.
- `build_guard_check`: confronto di identità della build; implementazione minima = ritorna 0
  (successo) loggando una riga.
- `loc_404C21`: funzione del lift referenziata per nome ma assente dall'oggetto. Verifica con
  `llvm-nm build/lift-full/StarCraft.o | grep -c 404C21`; se davvero assente, fornisci uno stub
  che logga e ritorna 0, **e annotalo nel report** perché è un buco del lift da correggere a monte.

**Ogni scelta va motivata nel report citando il chiamante (file:riga) o l'indirizzo IDA.**

## 4. File che puoi modificare

- `runtime/arm/game_glue.c` (nuovo)
- `tools/link-arm.sh` (solo per aggiungere `game_glue.o` alla riga di link)
- `work/link/REPORT-game-glue.md` (nuovo, aggiornato a ogni passo)

**Non toccare** `build/lift-full/*.o`, `build/runtime-arm/**` (sono artefatti), né altri sorgenti.

## 5. Deliverable e report

`work/link/REPORT-game-glue.md`, aggiornato **a ogni passo**, con queste sezioni fisse:

```
# REPORT game-glue
## Stato (aggiornato: <ora>)
## Cosa ho fatto (misurato)
## Gate
- ./tools/link-arm.sh -> <exit, output>
- qemu-arm-static ... -> <exit, output verbatim>
## Decisioni autonome
## Prossimo passo
```

`## Prossimo passo` deve essere **sempre** compilato: serve al supervisore per rimandarti avanti.

## 6. Ordine di lavoro consigliato

1. `runtime/arm/elf_support_arm.h` + i 4 chiamanti: firme e semantica.
2. Scrivere `game_glue.c` con tutte e 8 le funzioni (anche stub, ma compilabili).
3. Aggiungere `game_glue.o` al link, compilare e linkare: iterare sugli errori finché
   `./tools/link-arm.sh` esce 0.
4. Eseguire con `qemu-arm-static -L build/sysroot build/starcraft-armv6` (timeout 60 s),
   riportare l'output verbatim.
5. Se crasha: `gdb-multiarch` (se presente) o `qemu-arm-static -strace`; leggere il primo fault,
   correggere **una cosa alla volta**, riportare.
