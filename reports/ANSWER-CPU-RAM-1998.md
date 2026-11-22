# Perché 80–140 % di core e ~140 MB? Analisi misurata (2026-11-22, frigate)

Domanda: StarCraft 1998 girava su Pentium 90–200 MHz con 16–32 MB. Perché oggi il nostro binario
nativo consuma ~80–140 % di un core moderno e ~140 MB, e cosa succede cappando a 25 FPS?

## 1. Dove va la CPU (profilo a 25 FPS, partita Boot Camp, 29.641 campioni 1 kHz)

| Bucket | % campioni |
|---|---:|
| **libc/vdso (syscall, clock, wait)** | **70,38 %** |
| codice guest tradotto (`loc_*`/`sub_*`) | 6,76 % |
| runtime reasm (dispatch/shadow/SCode) | 5,92 % |
| SDL/Mesa/LLVM (renderer) | 5,04 % |
| shim (storm/win32/smack/gdi/com) | 1,51 % |
| host_elf (presenter/input) | 0,91 % |
| altro | 9,48 % |

Il lavoro "utile" (gioco + emulazione + render) è quindi **~20 %** di un core; il **70 %** è I/O.

### Conteggio syscall reali (strace -c, 4 s di partita a 25 FPS ≈ 100 frame)

| syscall | chiamate/4 s | per frame | note |
|---|---:|---:|---|
| `futex` | 11.856 | ~118 | 93 % del tempo syscall: contesa mutex/condvar |
| `recvmsg` | 38.260 | ~383 | **38.088 falliscono con EAGAIN** → polling UDP stretto |
| `poll` | 9.780 | ~98 | |
| `openat` / `close` | 9.463 / 9.452 | ~95 | probing file per frame |
| `read` / `_llseek` | 9.436 / 9.439 | ~94 | |
| `clock_nanosleep` | 357 | ~3,6 | sleep del cap |

Tre cause radice, tutte nostre (non del gioco):
1. **`spiReceive` in polling stretto** su socket UDP non bloccante dentro il loop di gioco (SFX multiplayer provider).
2. **Nessuna cache negativa** per `GetFileAttributesA`/`open` di asset assenti (`sound/Misc/*.wav`, componenti MPQ).
3. **Handoff per-frame mutex/condvar**: `EnterCriticalSection`/`LeaveCriticalSection` (shim Win32) + worker Smacker + thread audio.

## 2. Effetto del cap a 25 FPS (misurato, 2 run per config, mediana)

| Configurazione (partita Boot Camp) | CPU (core) | RSS |
|---|---:|---:|
| Nessun cap (`SC_PRESENT=cpu`) | **129,5 %** | 111–139 MiB |
| **Cap 25 FPS** (`SC_FPS_CAP=25`, `SC_PRESENT=cpu`) | **104,2 %** | 26–147 MiB\* |
| Cap 25 FPS, presenter auto | **107,7 %** | 114–153 MiB\* |
| Cap 25 FPS, menu | **105,0 %** | 122 MiB |

\* host sotto pressione di memoria (176 MB liberi, 12,8 GB di swap): RSS molto variabile.

Il cap toglie solo **~25 punti percentuali**: conferma che il collo di bottiglia **non è il rendering**
ma l'I/O per frame. Stima del lavoro reale a 25 FPS: **~20 %**; con cache negativa attributi,
`recvmsg` non in spin e handoff batch si può ragionevolmente scendere a **20–35 %**.

## 3. Da dove vengono i ~140 MB (smaps in partita)

RSS 148 MB così composta: `[heap]` **28,6** · anon **26,3** (arena guest 16 MB riservata ma ~12 KB toccati,
blocchi RWX tradotti ~10 MB, stack) · binario `starcraft-x86` **10,0** (`.text` 8,7 + rodata + immagini PE guest)
· `libLLVM` **6,4** · `libgallium` **4,8** · `libSDL3` **1,9** · `libc` **1,5** · SYSV shm **1,2**.

Confronto con il 1998:
- framebuffer 640×480 8bpp = **300 KB in VRAM** (oggi: canvas ARGB32 = **1,2 MB in RAM di sistema**);
- filmati Smacker decodificati 640×480×3 = **900 KB per slot** (ring a 3 slot);
- 32 voci DirectSound con buffer fino a 256 KB; cache LUT 64 KiB per kernel; decompressione MPQ.
- Il renderer software (Mesa/LLVM) da solo pesa **11–50 MB** e sostituisce una scheda video del 1998.

## 4. Il binario girerebbe su un PC del 1998? No.

1. **ISA/OS**: è ELF32 i686 per Linux moderno + SDL2; nel 1998 esistevano DOS/Win9x e Linux 2.0 con glibc
   incompatibile: manca loader, SDL2, Mesa.
2. **RAM**: 148 MB RSS contro 16 MB installati (10×).
3. **Overhead per frame**: ~600 syscall/frame e la traduzione statica richiedono futex/scheduler moderni.
4. **Video**: llvmpipe/Mesa su una CPU 1998 è fuori scala.

Ciò che invece è *fedele* all'epoca è il **lavoro utile** (~20 % di un core moderno a 25 FPS): coerente con
un gioco che saturava un Pentium 90–200 MHz in software rendering (SCode è il blitter software di Storm).
L'algoritmo gira su hardware dell'epoca; questa implementazione no.
