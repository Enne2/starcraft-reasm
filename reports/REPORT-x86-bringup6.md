# Report — x86 bring-up 6 (frigate)

## Esito

Il SIGSEGV `0x00056F24` è isolato e corretto: nasceva nel ramo x86 di `WaitForMultipleObjects`, che dereferenziava `0x15045264` come puntatore host di `WaitForSingleObject`; al runtime quello slot valeva `0x00056F24` (RVA guest), quindi la chiamata indiretta saltava in memoria non mappata. Fix: su i386 chiamata diretta allo shim locale `WaitForSingleObject` e riconoscimento/join degli handle thread guest `0x72...` via `arm_guest_thread_join`, come sul percorso ARM.

Corretto anche lo skip intro: `SC_SKIP_INTRO=1` era già la variabile giusta (menu8/menu report); il path `smk\\blizzard.smk` arrivava come puntatore host nell'ELF, ma il controllo usava solo `arm_va_is_mapped` e lo rifiutava. `storm_video_skip_enabled` ora convalida i byte sia nelle regioni guest sia con `proc_range` sulle mappe leggibili del processo, senza dereferenziare puntatori non validati. GDB ha verificato path e ritorno `1`.

## Evidenze e test

- GDB pre-fix, senza `SC_BRIDGE_TRACE`: `WaitForMultipleObjects(n=63,h=0x021F9F58,all=1,ms=50)`; `*(u32*)0x15045264 == 0x00056F24`; l'handle in attesa era `0x5E000054`. `bt` pone il fault dentro `WaitForMultipleObjects` alla chiamata `w1`, confermando la causa, non un crash nel callback UI/SCode/DirectSound.
- GDB su `storm_video_skip_enabled`: puntatore guest-stack `0x18041704` → stringa `smk\\blizzard.smk`; dopo il fix la funzione ritorna `1`.
- Build finale `build-shim-x86.sh`, `build-runtime-x86.sh`, `link-x86.sh` e `tests/x86_runtime_check/check.sh`: PASS. ELF32/EXEC/Intel 80386; dispatch map 102376 voci, 0 collisioni.
- `build/qa/run-x86-bringup6-smoke.log`, timeout 60 s (exit 124): supera il SIGSEGV iniziale, carica/decodifica animazioni SMK del menu, crea buffer audio, produce FPS ripetuti ~329–338. Poi il fail-closed dispatcher ferma l'indirect target `0x1805F2D4` (`in mappa=0`), chiamante guest `0x00404F26`; **non** è il precedente `0x56F24` né lo STOP ARM `0x0041CAC8`. Menu asset/animazione e FPS sono raggiunti; lo stop finale resta da investigare, senza allentare D19/resolver.
- Test SDL reale tentato, senza input desktop, con `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-1`, `xdpyinfo` PASS e `SDL_VIDEODRIVER=x11`: il log `build/qa/run-x86-bringup6-sdl-real.log` (5 s) e il probe da 15 s terminano con kill 137; entrambi stampano `[host] init: video driver = x11` ma non `[host] finestra creata`, audio reale o `[fps]`. Quindi la prova SDL2 reale **non è riuscita** e non è dichiarata PASS.

## File sorgente modificati

- `runtime/arm/storm_sdraw_arm.c`: validazione sicura di string literal host e guest per `SC_SKIP_INTRO`.
- `runtime/shim-port/elf/win32_shim.c`: WFMO i386 usa lo shim locale e riconosce/join gli handle guest `arm_guest_thread_start`.

Artefatti di build in `build/obj-x86/` e `build/starcraft-x86`. Log: `build/qa/gdb-x86-bringup6-fault.log`, `gdb-x86-bringup6-wfmo.log`, `gdb-x86-bringup6-skip-verify.log`, `run-x86-bringup6-skip.log`, `run-x86-bringup6-smoke.log`, `run-x86-bringup6-sdl-real.log`, `run-x86-bringup6-sdl-real-probe.log`. Il log storico `run-x86-bringup5-fix3.log` è 76 MiB: conservato.

## Decisioni autonome

Ho applicato solo i fix supportati dalle evidenze GDB, mantenuto invariati fail-closed e SCode, e non ho inviato input desktop. Non ho considerato lo STOP `0x1805F2D4` come menu completo o come motivo per aggiungere target arbitrari.

## Prossimo passo

Diagnosi non-invasiva dell'inizializzazione SDL reale con GDB (senza input):

```sh
cd /home/enne2/dev/starcraft-reasm-generic/build/asset-sandbox
SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=x11 \
gdb -q -batch -ex 'set pagination off' -ex 'set debuginfod enabled off' -ex 'break SDL_CreateWindow' -ex 'break host_elf_display_mode' -ex run -ex 'info threads' -ex bt --args ../starcraft-x86
```

Dopo aver chiarito perché la prova resta a `[host] init`, rilanciare la verifica reale di 5 s e richiedere nel log `[host] finestra creata`, backend audio non dummy e `[fps]`:

```sh
cd /home/enne2/dev/starcraft-reasm-generic/build/asset-sandbox
SC_SMACKER_SO="$PWD/../i386-libs/libsmacker-1.2.0.so" SC_SKIP_INTRO=1 SC_FPS_LOG=1 SDL_VIDEODRIVER=x11 timeout -k 1s 5s ../starcraft-x86
```
