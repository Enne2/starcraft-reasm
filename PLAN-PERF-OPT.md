# Piano di ottimizzazione delle prestazioni (`x86`, `ARMv7hf` Pinebook Pro, `ARMv6` Raspberry Pi Zero/1)

**Data:** 2026-11-21
**Basato su evidenze misurate in:**
- `work/link/REPORT-arm-prof.md` (campionamento `SC_PROF=1`, 1000 campioni in-process)
- `work/qemu/REPORT-arm-pbp.md` (misure reali su Pinebook Pro RK3399, `REASM_TRACE=1` 10 s, `SC_PRESENT=cpu`, `menu3` e `menu8`)
- Analisi diretta di `runtime/common/{indirect_dispatch.c,storm_sdraw_arm.c,game_memory_arm.c}`, `runtime/shim-port/elf/host_elf.c`, `tools/dispatch-ir.py`

---

## 1. Colli di bottiglia già individuati sui test Pinebook Pro / ARM

### 1.1 Hotspot già misurati e loro stato attuale
1. **`proc_range()` in `runtime/common/storm_sdraw_arm.c` — ~65–68% CPU prima della cache:**
   - Nel profilo `SC_PROF=1` (`work/link/REPORT-arm-prof.md`), `libc` `open+0xE4` (`61,8%–63,6%`) + `vfscanf`/`sscanf` (`~4%`) dominavano la CPU perché `proc_range()` apriva e parsava `/proc/self/maps` a ogni validazione di superficie/puntatore.
   - **Stato attuale:** è stata aggiunta una cache da `4096` entry (`g_map_cache`) invalidata da `g_vm_generation`.
   - **Costo residuo:** ogni `VirtualAlloc` / `VirtualFree` / `VirtualProtect` incrementa `g_vm_generation` e forza il re-parsing completo di `/proc/self/maps`; inoltre `map_contains()` esegue una scansione lineare sotto `pthread_mutex_lock`.
2. **Anello diagnostico di `reasm_indirect_resolve()` (`runtime/common/indirect_dispatch.c:94–110`) — ~4,5% CPU misurato:**
   - Nel profilo `SC_PROF=1`, `arm_va_is_mapped` (`3,90%`) + `cpu_llasm_read32` (`0,60%`) risultavano al **2° posto assoluto** tra i simboli risolti.
   - Causa: **su ogni singolo arco indiretto (`call`/`ret`)**, *prima* ancora di controllare la cache `g_key`/`g_val` da 4096 entry, il codice chiama `cpu_llasm_current()` (TLS) e **due volte** `arm_va_is_mapped()` (ciclo lineare su `g_regions`) + `cpu_llasm_read32()`. Inoltre `g_ring` è globale senza lock/TLS (potenziale race su più thread).
3. **Helper TLS/atomiche kuser del kernel (`0xFFFF0FE0`) — 1,4% CPU misurato:**
   - Nel profilo `SC_PROF=1` su ARMv6, `0xFFFF0FE0` compare al 4° posto (`14/1000` campioni) per chiamate alla kuser helper page Linux invece di accesso diretto `TPIDRURO` / istruzioni native ARMv7.
4. **Churn `VirtualAlloc` / `VirtualFree` e micro-letture MPQ all'avvio (`work/qemu/REPORT-arm-pbp.md`):**
   - In 10 s di startup `REASM_TRACE=1` su `menu3` (3,66 FPS medi, 1 core al ~100% mentre `present` costava solo **0,76 ms**):
     - **`1.642` `VirtualAlloc` + `606` `VirtualFree`** (su Linux ogni alloc/decommit/release esegue syscall `mmap`/`mprotect`/`madvise` e invalida `g_vm_generation`).
     - **`934` `ReadFile`**, inclusa la scansione dell'header `MPQ\x1a` embedded in `BroodWar.mpq` tramite **~576 seek+read da 32 byte** ogni `0x200` byte da `0x0` fino a `0x48000`.
     - **`203` `Wait`** (28 timeout `0x102` da 250 ms).
5. **Contesa CPU con workload paralleli (recording / single-core Pi Zero):**
   - In `menu8` su Pinebook Pro senza recorder il menu tocca `173–207 FPS`, ma scendeva a **`12–13 FPS`** con encoding VP8 concorrente sullo stesso sistema; su Pi Zero (single-core ARM1176) i thread main, reader MPQ e mixer audio SDL competono su **un solo core** senza `UDIV`/`SDIV` hardware né NEON.
6. **Conversione software `8bpp → 32bpp` (`host_elf.c:722`):**
   - `present_gpu()` converte `640×480` pixel su CPU a ogni frame (`307.200` lookup) e carica `1,2 MB`/frame come `ARGB8888`. Sul Pinebook Pro `present` pesa `<1 ms`, ma su Pi Zero riduce margine CPU e banda RAM/GPU.

---

## 2. Piano di ottimizzazione (ordinato per rapporto guadagno/rischio)

### Fase A — Guadagni immediati a basso rischio (runtime C condiviso)

1. **Togliere `arm_va_is_mapped` e `cpu_llasm_read32` dal fast-path di `reasm_indirect_resolve` (`indirect_dispatch.c`)**
   - Controllare **subito** la cache `g_key`/`g_val` all'ingresso di `reasm_indirect_resolve`, prima di aggiornare `g_ring`.
   - Spostare `g_ring` dentro `_cpu` (o dietro flag/macro di build `SC_EDGE_RING=1`, oppure popolarlo solo su cache miss / negli ultimi N salti prima di un fault) ed eliminare i due `arm_va_is_mapped()` sul percorso caldo.
   - *Impatto atteso:* elimina ~4–5% di CPU globale e rimuove una data race su `g_ring` in multi-thread.

2. **Fast-path range in `arm_va_is_mapped` (`game_memory_arm.c`) e `proc_range` (`storm_sdraw_arm.c`)**
   - In `arm_va_is_mapped`: testare subito i 2 range più frequenti (`ARM_STACK_BASE` e `ARM_ARENA_BASE`) come confronto diretto, poi il resto di `g_regions`.
   - In `proc_range`: testare prima gli intervalli guest statici/arena prima di prendere `g_map_mutex`; in `map_contains()` sostituire la scansione lineare su `g_map_cache` (fino a 4096 entry) con **ricerca binaria**.

3. **Ridurre il churn `VirtualAlloc` / `VirtualFree` e invalidazioni `g_vm_generation` (`win32_shim.c` / `game_memory_arm.c`)**
   - Verificare in `win32_shim.c` se le allocazioni temporanee di decompressione/loading Storm (`MEM_COMMIT`/`MEM_DECOMMIT`/`MEM_RELEASE` ripetitive) possono usare una piccola freelist/cache di blocchi già mappati invece di chiamare `mprotect`/`madvise` + incrementare `g_vm_generation` centinaia di volte al secondo.
   - Incrementare `g_vm_generation` solo quando cambia davvero la mappa di indirizzi/permessi esterni, non per allocazioni dentro un blocco arena già noto.

4. **Read-ahead / cache leggera per micro-letture MPQ (`win32_shim.c`)**
   - Aggiungere una piccola cache di lettura per file handle in sola lettura (`ReadFile`/`SetFilePointer`, es. finestra da `16–64 KiB` invalidata solo su `WriteFile`/`CloseHandle`): trasforma le ~576 `SetFilePointer`+`ReadFile` da 32 byte su `BroodWar.mpq` in **~5 syscall reali** da `64 KiB`, riducendo drasticamente il costo bridge allo startup.

---

### Fase B — Ottimizzazione presentazione video senza uscita hardware a 8 bit (`host_elf.c`)

- **Nota di fattibilità:** X11 moderno, Wayland e `SDL2 KMSDRM` (`GBM_FORMAT_ARGB8888`) non offrono un output nativo a 8 bit palettizzati senza scrivere un presenter `libdrm` custom (e `vc4` upstream non garantisce `DRM_FORMAT_C8` per piano in userspace).
- **Soluzione pulita in SDL2 / OpenGL ES 2.0 (VideoCore IV e Panfrost):**
  - Mantenere `SDL_VIDEODRIVER=kmsdrm` su Pi Zero (evitando X11/Wayland e risparmiando ~80–150 MB RAM).
  - Sostituire in `present_gpu()` il loop CPU `8bpp → 32bpp` (`1,2 MB`/frame) con upload diretto del framebuffer `640×480` a 8 bit in una texture `GL_LUMINANCE`/`GL_R8` (**`300 KB`/frame**, banda 4× inferiore) + texture `256×1 RGBA` per `g_pal`, eseguendo la conversione palette in un fragment shader GLES2 da 3 righe.
  - Conservare il percorso attuale `present_gpu` / `SC_PRESENT=cpu` come fallback automatico.

---

### Fase C — Accelerazione codice caldo di `Storm.dll` via `reasm_extern` (stato Storm intatto)

- **Principio (da lezione `qmem`):** non shimmare `SFile*` esternamente (rompe `crypt table` e stato interno di Storm). Intercettare invece **solo routine foglia stateless** dentro `storm.ll` tramite `tools/dispatch-ir.py` + `reasm_extern` (meccanismo già collaudato per `#313`, `#334`, `#454`, `#457`, `#458`).
- **Candidati ad alto impatto da verificare in `build/lift-full/storm.orig.dll.i64`:**
  1. **`#551` (`SCompDecompress` @ `0x1501D310`)**: decompressione buffer-to-buffer (PKWARE DCL `explode`, Huffman, ADPCM mono/stereo, multi-pass). Eliminare `_cpu`/`eflags`/`reasm_indirect` nei loop bitstream velocizza il caricamento di tutti gli asset compressi (MPQ, GRP, PCX, TBL, mappe).
  2. **Loop di decifratura settori MPQ (`loc_15013C80` / routine collegate da verificare in IDA):** routine pura `decrypt(uint32_t *buf, size_t bytes, uint32_t key)` sulla tabella `0x500` dword già inizializzata da Storm.
  3. **`storm_sdraw_arm.c` (`#313` / `#334`):** per `ARMv7hf` (Pinebook Pro) abilitare path vettoriali/NEON o word-unrolled sui ROP più frequenti in partita (`0xCC0020`, `0xF00021`, `PATCOPY`, RLE/trasparenze man mano che emergono dal Single Player).

---

### Fase D — Ottimizzazioni per target CPU (`ARMv7hf` vs `ARMv6`) e `dispatch` in assembly

1. **Fast-path assembly per la cache `reasm_indirect` (`runtime/arch/{arm,x86}/indirect_dispatch*.S`):**
   - Controllare `g_key[(target >> 2) & 4095] == target` direttamente in assembly prima di creare un frame/chiamare `reasm_indirect_resolve` in C (attualmente anche su cache hit si entra in C e si chiama `cpu_llasm_current()`).
2. **Profilo dedicato `armv7` in `tools/build.sh` (Pinebook Pro):**
   - `-mtriple=armv7a-unknown-linux-gnueabihf -mcpu=cortex-a53 -mattr=+vfp3,+neon,+hwdiv-arm -float-abi=hard` (o `-mfpu=neon-vfpv4` / `vfpv3-d16`).
   - Abilita `UDIV`/`SDIV` hardware, 32 registri VFP, auto-vettorizzazione NEON su `llasm` / `storm_sdraw_arm.c` / `libsmacker`, barriere `dmb` native ed evita le chiamate a `0xFFFF0FE0` (kuser helper).
3. **Profilo `armv6` (Raspberry Pi Zero / Pi 1):**
   - Mantenere `opt -passes='default<O2>'`, `-mcpu=arm1176jzf-s -mattr=+vfp2,-neon -float-abi=hard -marm`, linker senza padding inutile del segmento `LOAD` (modello `link-arm.sh` ~21 MB ELF, non i 217 MB attuali di `link-x86.sh`), esecuzione da console con `SDL_VIDEODRIVER=kmsdrm`.

---

## 3. Protocollo di verifica A/B (obbligatorio per ogni intervento)

1. **Correttezza prima della velocità:** ogni modifica deve superare i test ABI/unit (`make check` / `x86_runtime_check`), `--dump-map` (`0` collisioni) e raggiungere il medesimo stato funzionale (menu / partita) senza violare D19.
2. **Metriche da misurare su 3+ esecuzioni per ogni passo:**
   - **Wall time di avvio:** dal lancio processo alla comparsa del menu / primo frame di gioco.
   - **Profilo `SC_PROF=1`:** Top-20 simboli su 1000+ campioni (confronto `%` prima/dopo).
   - **`REASM_TRACE=1` conteggi:** numero di `ReadFile`, `VirtualAlloc`, `VirtualFree`, `Wait`.
   - **FPS e `present medio` (`SC_FPS_LOG=1`)** + **CPU% e RSS** da `/proc/<pid>/status` (verifica margine sotto i ~384–448 MB reali del Pi Zero).
