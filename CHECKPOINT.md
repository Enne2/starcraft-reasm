# CHECKPOINT — starcraft-armv6

**Data:** 2026-11-20, ~13:10 (Europe/Rome).
**Scopo di questo file:** permettere a una sessione nuova (modello diverso, contesto vuoto) di
riprendere il lavoro senza rileggere la trascrizione. Sostituisce ogni vecchio file di stato
(`work/link/STATE-game-glue.md` è stato assorbito e rimosso).
**Sorgente primaria del progetto:** questo file. I `work/*/REPORT*.md` restano come evidenza storica.

---

## 1. Stato in una riga

Il binario ARMv6 **esiste, linka con zero simboli mancanti ed esegue**: entra nel CRT del gioco e
legge le risorse, poi muore con **SIGILL perché la CPU emulata è in modalità Thumb**.

---

## 2. Cosa funziona (verificato oggi)

**Fase 1 QA: conclusa. Prerequisiti fase 2: COMPLETATI** (SDL2 + 3 MPQ verificati, VM attiva
pid 542755). Evidenze, misure, piano e blocchi in `work/qemu/REPORT-flash-qa.md`. In attesa del
**candidato con hash** dal supervisore: nessun binario del gioco è stato eseguito.

```bash
cd ~/dev/starcraft-armv6
./tools/link-arm.sh          # -> exit 0, "creato: .../build/starcraft-armv6" (8.079.992 byte)
timeout 60s qemu-arm-static -L build/sysroot build/starcraft-armv6
```

Output verbatim dell'esecuzione:

```
[arm-game] runtime ARMv6 — immagini tradotte, import statici
[arm-game] header PE exe a 0x00400000
[arm-game] header PE storm a 0x15000000
[arm-game] cpu=0x0E000000
[arm-game] TEB=0x0E000088
[arm-game] PEB=0x0E001088
[arm-game] bridge: 324 import, 33 UNK dichiarati
[arm-game] entry: start() del CRT a 0x00404C21
[risorse] eseguibile: .rsrc RVA=0x002DE000 VA=0x006DE000
qemu: uncaught target signal 4 (Illegal instruction) - core dumped
timeout: the monitored command dumped core
```

Exit code **132 (SIGILL)**, non un timeout, non un crash di link.

---

## 3. Dove si ferma e perché (diagnosi, non ipotesi)

Fault rilevato con `qemu-arm-static -d in_asm,cpu`:

```
R15 = 0x004060C0      R01 = 0x004060C1      PSR: bit Thumb ATTIVO
```

In IDA (copia originale) `0x4060C0` è un `retn` dentro `__onexit_lk` (`0x406041–0x4060C0`,
chiamata da `__onexit` a `0x406101`). Con il bit Thumb acceso la CPU interpreta i byte come
Thumb-2 e salta a un indirizzo dispari (`0x4060C1`): da lì **SIGILL**.

Escluso: non è ARMv7/NEON generato da `llc` (verificato esaminando il PC e l'istruzione).
L'ipotesi di lavoro è che il registro di stato del `_cpu` (CPSR/`_flags` mappato) riceva il bit
Thumb da una conversione flag x86 → CPSR, o che l'entry venga invocata con un CPSR non mascherato.

---

## 4. Le tre scoperte che hanno sbloccato il link (non rifarle)

1. **Gli shim esistono già**: `build/runtime-arm/shim-port/obj/*.o` (14 oggetti C: win32_shim,
   gdi_elf, com_ddraw, com_dsound, input_elf, dlg_elf, smack_*, snp_elf, host_elf, main_elf,
   win32_game, win32_ui, storm_ext) + il lato guest-ABI in `build/runtime-arm/game/obj/*.o`.
2. **Mancava il rename**: il bridge chiama `Api__c`, gli oggetti espongono `Api`. Il mapping è in
   `build/runtime-arm/game/shim_rename.txt` e va applicato con
   `llvm-objcopy --redefine-syms` (lo fa `tools/link-arm.sh` in una dir temporanea).
   Effetto misurato: **443 → 8** simboli mancanti.
3. **`loc_404C21` spariva per DCE**: nell'IR è `define private fastcc` e **nessuno la chiama
   dentro il modulo**; `llc` la elimina. Fix applicato: copia `build/lift-full/StarCraft.entry.ll`
   con la privata rinominata `@sc_loc_404C21_impl` e un **wrapper globale** `@loc_404C21` in coda
   (C-ABI, chiama la `fastcc`). Compilata in `build/lift-full/StarCraft.entry.o`
   (`llvm-nm` mostra `T loc_404C21`). Effetto: **8 → 0** simboli mancanti.

Il "glue" (`runtime/arm/game_glue.c`, 3.016 byte) implementa gli 8 simboli legacy restanti
(`g_imports`, `g_imports_n`, `reasm_iat_install`, `reasm_call_on_stack`,
`reasm_storm_ordinal_va`, `winmain_trap`, `build_guard_check`, `loc_404C21` come stub — ora il
simbolo vero arriva dal wrapper, quindi lo stub va rimosso se non già fatto).

---

## 5. Comandi e toolchain (esatti)

```bash
# compilazione ARMv6 (glue)
clang --target=armv6-unknown-linux-gnueabihf -march=armv6zk -mfpu=vfp -mfloat-abi=hard \
      --sysroot=build/sysroot -O2 -c runtime/arm/game_glue.c -o /tmp/game_glue.o

# ricompilazione dell'IR con il wrapper (pipeline usata dal sotto-agente)
opt -passes=verify <copiall> | opt -passes='default<O2>' | \
llc -mtriple=armv6-unknown-linux-gnueabihf -mcpu=arm1176jzf-s -mattr=+vfp2,-neon \
    -float-abi=hard -filetype=obj -o build/lift-full/StarCraft.entry.o

# link: usa ld.lld (rustup), NON arm-none-eabi-ld (conosce solo armelf bare-metal)
~/.rustup/toolchains/*/lib/rustlib/*/bin/gcc-ld/ld.lld
```

Il link prende libc/libm/libgcc_s/libSDL2 dal sysroot `build/sysroot/usr/lib/arm-linux-gnueabihf`.
Le .so vanno passate esplicitamente: `-lc` risolverebbe al path assoluto dell'host.

---

## 6. Prossimo passo (in quest'ordine)

1. In IDA aprire `__onexit` (`0x4060E9`) con gli xref, leggere per intero `__onexit_lk`
   (`0x406041`) e il ramo che produce `0x004060C1`.
2. Confrontare con la sezione IR di `loc_406041` e con il codice che imposta il registro di stato
   del `_cpu` (cercare `_flags`, CPSR, bit 5 / Thumb).
3. Correggere **una sola** causa nella copia IR (o nel runtime), ricompilare `StarCraft.entry.ll`,
   rilinkare, ripetere il gate.
4. Se il gioco supera l'entry: attendersi il primo uso di GDI/DirectDraw o degli MPQ. Gli MPQ non
   sono collegati nel sandbox (`qemu-arm-static`): serviranno symlink dai file originali in
   `~/dev/final-starcraft/Starcraft/`.
5. Poi il test vero: copiare il binario nel guest QEMU e lanciarlo con `DISPLAY=:0` sul server X
   già attivo (`minimal-x.service`).

---

## 7. Sotto-agente (riusabile)

| campo | valore |
|---|---|
| id / label | `11645455` / `game-glue` |
| pid / porta | 3485333 / 41747 |
| cwd | `~/dev/starcraft-armv6` |
| modello | `openai-codex/gpt-6-luna`, thinking `max` (verificato) |
| stato | idle, `compaction_done`, contesto compattato |

Ripresa:
```bash
SUP=~/.pi/agent/extensions/subagent-control/tools/supervise.py
python3 $SUP list | grep game-glue
python3 $SUP unblock 11645455 --answer-file /tmp/missione.md --deliver-as followUp
python3 $SUP guard --only-prefix game-glue --once     # obbligatorio a fine turno
```
Se la sessione è morta: rilanciare `spawn` passando **questo file** come brief (costa ~3k token).

---

## 8. VM QEMU (**attiva**, pid 542755, dal 15:48:11)

> **Aggiornamento QA 2026-11-20 16:47** — VM rilanciata dalla QA dopo lo stop della fase 2a.
> **Prerequisiti completati**: `apt-get clean`, `libsdl2-2.0-0` installata e configurata,
> **3 MPQ trasferiti e verificati** (`sha256sum -c` OK/OK/OK), 163 MiB liberi. Nessun binario del
> gioco è stato eseguito. La riga `[build] …`/SIGILL di §1 e §3 **non** è stata corretta: nessuna
> evidenza runtime nuova da questa QA.
> Dettagli: `work/qemu/REPORT-flash-qa.md` §4, §6.1, §13, §14.

| cosa | valore |
|---|---|
| processo | **pid 542755** (`qemu-system-arm -M raspi0 -m 512M`), avviato 15:48:11 |
| viewer | **nessuno**: il TigerVNC non è stato riaperto (aprire solo se serve vedere, non per misurare) |
| rete | **slirp + hostfwd 2222→22** (`SSH_FWD=1`); guest `10.0.2.15` |
| SSH | `build/qemu/ssh/gssh` — **timeout 60–150 s** se il guest è saturo; il banner risponde subito ma l'autenticazione è lenta (CPU emulata) |
| disco | overlay `build/qemu/runtime/guest-lite.qcow2` (~2,0 GB, virtual 4 GiB) su `build/qemu/lite/lite.img` (intatta, mtime 10:42:42) |
| kernel/dtb | `build/qemu/lite/boot/{kernel.img,bcm2708-rpi-zero.dtb}` (Trixie 6.18.50+rpt-rpi-v6) |
| GUI guest | `minimal-x.service` → Xorg fbdev 800×480 + openbox + lxterminal (X auth: `-auth` in `/proc/<Xorg>/cmdline`) |
| screenshot | QMP `build/qemu/runtime/qmp.sock`, path **assoluto** (`tools/qemu/qa-shot.py`) |
| **asset nel guest** | `/home/pi/sc/`: `patch_rt.mpq` 1.086.853, `StarDat.mpq` 66.184.568, `Starcraft.mpq` 606.857.006, `MANIFEST-assets.sha256` (in `work/qemu/`), tutti verificati |
| **spazio** | **171.085.824 B (163 MiB) liberi** su `/` (96%) |
| **SDL2** | `libsdl2-2.0-0:armhf 2.32.4+dfsg-1`, `dpkg --audit` vuoto, `ldd` 0 mancanti |
| **prossimo passo** | ricevere il **candidato con hash** → copia *identica* in `/home/pi/sc/`, `sha256` bilaterale, poi prova nel guest (mai prima) |

### 8b. Trappola nuova: killer cross-progetto sulle VM

`~/projects/esp95/build-lean/run-gate.sh` fa
`for p in $(ps -eo pid,comm | awk '$2 ~ /^qemu-system/'); do kill $p; done`.
`comm` matcha **tutti** gli emulatori `qemu-system*`, quindi **uccide anche questa VM**:
pid killer 331341 (15:20:30, 15:22:09) e **478340** (15:38:46 e 15:40:24, dalla stessa esecuzione
del loro gate). Non è QEMU, non è il guest, non è il port. In più il loro gate porta il **carico host
a 9,9–13,6**: con quel carico il guest è inservibile via SSH e **nessuna misura di performance è
attendibile**. Rimedio: filtrare il loro kill (`-name`, cwd, pid-file) o concordare una finestra;
QA **non** tocca `~/projects/esp95` né i suoi processi.



Avvio:
```bash
IMAGE=$PWD/build/qemu/lite/lite.img KERNEL=$PWD/build/qemu/lite/boot/kernel.img \
DTB=$PWD/build/qemu/lite/boot/bcm2708-rpi-zero.dtb \
DISK=$PWD/build/qemu/runtime/guest-lite.qcow2 SSH_FWD=1 QEMU_DISPLAY=none \
  setsid nohup env QEMU=/tmp/qemu-arm-install/usr/local/bin/qemu-system-arm \
  ./tools/qemu/run.sh -vnc 127.0.0.1:1 -k it >build/qemu/launch.log 2>&1 &
```
Note: `NET_MODE=passt` esiste in `run.sh` ma **non funziona** (blocca il boot sull'enumerazione
USB). Non usarlo.

---

## 9. Profilazione del boot (misurata, non stimata)

- QEMU **non emula una frequenza**: nessuna accel disponibile (solo TCG). `/proc/cpuinfo` è
  equivalente al Pi Zero (ARM1176 rev 7, part 0xb76, 512 MB). BogoMIPS 1869 vs 697 reali = artefatto.
- Benchmark: SHA256 23,7 MB/s (**più veloce** di un Pi Zero vero, ~10–15 MB/s); fork+exec 20,6 ms
  (reale ~5–8 ms) → l'emulatore paga 2,5–4× solo sui processi.
- Boot immagine Lite stock: kernel 6,2 s + userspace 3min57, `multi-user` a **141,4 s**.
  Su Pi Zero **reale** la stessa immagine fa 153,9 s totali (Hallard, ago 2026) → l'emulatore è
  ~1,4×, non 10×.
- Con cloud-init/NetworkManager/swap-resize fuori: multi-user ~103 s, prompt a 108 s.
- Guest a regime: **138 MB RAM usati** su 427, 16 servizi.

---

## 10. Trappole già pagate (non ripeterle)

1. **`e2fsck -fy` dopo le scritture `debugfs` riproduce il journal e annulla le modifiche.**
   Sequenza corretta: `e2fsck` → `debugfs` → ricomposizione, **senza** e2fsck finale.
2. **Mai `pkill -f`/`pgrep -f` con un pattern presente nella propria command line**: uccide la
   propria shell (successo due volte). Usare `pgrep -x <nome>` o i PID dai file/registry.
3. La dimensione del disco SD in QEMU deve essere **potenza di 2** (2,6 GB → 8 GB).
4. `passt` vuole `-4` (IPv4-only) e `-D` per il DNS (`-d` è `--debug`); il socket funziona solo
   in `/tmp`, non in `build/qemu/runtime`.
5. Comandi lunghi sulla **console seriale** perdono caratteri e vengono troncati: usare SSH.
6. Il **client VNC** si chiude quando muore QEMU, e può finire dietro altre finestre: riaprirlo,
   non dichiarare "visibile" senza uno screenshot dell'host.
7. Gli oggetti `*__c` non esistono finché non si applica il rename: un link che li cerca senza
   `llvm-objcopy --redefine-syms` produce 443 falsi errori.
8. Mai passare **due volte** gli stessi oggetti (archivio + `.o`): `duplicate symbol`.

---

## 11. Vincoli del progetto

- Non modificare i repo originali `~/dev/sc-reasm`, `~/dev/m-ht-sr`, né il sorgente QEMU condiviso
  `~/projects/bevel-sim/.tools/qemu-src/qemu`.
- `build/qemu/lite/lite.img` è la **base intatta**: si lavora solo sull'overlay.
- Niente `sudo`, niente install di sistema, niente push su GitHub.
- Il guest non fa parte del perimetro del sotto-agente `game-glue`.

---

## 12. Cosa NON è verificato (onestà)

- La **causa esatta** del bit Thumb: formulata come ipotesi, non dimostrata nel runtime.
- Il gioco **non è mai arrivato** oltre il parsing delle risorse: nessuna finestra, nessun MPQ
  (`~/dev/final-starcraft/Starcraft` esiste su questa macchina e contiene gli MPQ).
- Il binario **non è mai stato eseguito nel guest** QEMU (solo con `qemu-arm-static` sull'host).
- `winmain_trap` è implementato in modo minimale: nessun VA di WinMain verificato.
- La rete reale in LAN (passt/macvtap) è **abbandonata** in attesa di decisione.
