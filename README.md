# starcraft-reasm — StarCraft + Storm, a native port via lifting and shims

**A native port of StarCraft and Brood War that uses no Wine, no DirectX and no CPU emulation.**

The original game's code is neither run inside an emulator nor through a compatibility layer: it is
**statically translated** into an intermediate representation (LLVM IR) and then **recompiled for the
host architecture**. The services the game expects from the operating system (Win32, DirectDraw,
DirectSound, GDI, Smacker, SNet/SNP networking) are reimplemented by a C runtime that talks directly
to the platform's native APIs (SDL2, OpenGL/GLES, POSIX sockets).

The result: **StarCraft runs as a native program** on i686 and on ARMv7 (Pinebook Pro), with menus,
campaigns, save/load and LAN multiplayer between two instances. No game assets are included: you need
your own copy of StarCraft 1.16.1.

---

## Contents

1. [Why this project exists](#1-why-this-project-exists)
2. [How it works](#2-how-it-works)
3. [Current status](#3-current-status)
4. [Performance](#4-performance)
5. [Requirements](#5-requirements)
6. [Guide: building for a software/hardware platform](#6-guide-building-for-a-softwarehardware-platform)
7. [Guide: using the port](#7-guide-using-the-port)
8. [Tests and verification](#8-tests-and-verification)
9. [Future work](#9-future-work)
10. [Repository layout](#10-repository-layout)
11. [Licence, game data and trademarks](#11-licence-game-data-and-trademarks)

---

## 1. Why this project exists

Three motivations, in order of importance.

**1. Take the game beyond its software ecosystem.** StarCraft 1.16.1 is a 32-bit PE binary for
Windows 9x/XP that depends on DirectDraw, DirectSound, GDI and Storm.dll: on modern Linux it only
exists through Wine. Wine works well, but it remains a *Windows emulator*: you pay the cost of an
entire simulated operating system (traps, translation, synchronisation) to run a single program.

**2. Take it to other hardware architectures, not just other operating systems.** An x86 emulator on
an ARM or RISC-V board pays for dynamic translation on every run; a native port does not. With
lifting, the game's code becomes code of the target architecture: on a Pinebook Pro (ARMv7) the game
runs at full frame rate with modest CPU load, something an x86 emulator could not achieve.

**3. Preservation.** A 1998 binary depends on a graphics API (DirectDraw) that the operating system
has stopped supporting. Lifting + shims is a way to preserve the program's *behaviour* by describing
its dependencies in terms that outlive the operating system: the game remains the same code, but its
dependencies live in our runtime, where they can be reimplemented, tested and ported.

Hence the shape of the project: **a lifting pipeline** (which turns the original binary into
compilable code) plus **a runtime/shim** (which reimplements its services). Neither half is enough on
its own: together they make the game native on any platform for which an LLVM compiler exists.

---

## 2. How it works

```
StarCraft.exe / storm.dll            (original binaries: the user's own copy)
        │
        │  1. LIFTING  (vendor/sr: SR/llasm, static recompiler)
        ▼
StarCraft.llasm / storm.llasm        (intermediate representation of the translated code)
        │
        │  2. TRANSLATION  (tools/, LLVM)
        ▼
StarCraft.ll / storm.ll              (LLVM IR, with dispatch table and guest ABI)
        │
        │  3. COMPILATION  (llc -mtriple=<architecture>)
        ▼
StarCraft.entry.o / storm.entry.o    (native code for the host)
        │
        │  4. LINK  (with our runtime)
        ▼
build/starcraft-x86  |  build/starcraft-armv7 ...
```

The key step is **3**: the same IR is compiled for different triples. Changing architecture does not
mean rewriting the port, it means changing `-mtriple` and providing the minimal ISA-dependent layer.

The **runtime** (`runtime/`) provides what the translated code expects:

| Layer | What it does |
|---|---|
| `runtime/common/` | guest CPU (registers, flags, x87), guest virtual memory mapping, indirect-call dispatch, bridge to C code, SCode (Storm's blitters), SComp (decompressor) |
| `runtime/arch/<arch>/` | only the unavoidably architecture-specific assembly: indirect dispatch trampoline, COM bridge, guest zeroing, TLS |
| `runtime/shim-port/elf/` | Win32 (files, processes, synchronisation, registry, messages), DirectDraw, DirectSound, GDI, Smacker, SNet/SNP (networking), SDL2 + OpenGL for presentation |
| `tools/` | build pipeline, lifting, bridge generation, QA (`sc_ctl`, isolated sandbox) |

Two project rules that explain many decisions in the code:

- **the binary is the oracle**: every constant, struct layout, `stdcall` arity or address in the
  runtime cites the address in the decompiled binary that justifies it (see `reports/ANALYSIS-*.md`);
- **fail-closed**: a function whose contract has not been proven returns an explicit error and logs it,
  instead of returning invented values that would look like they work. A game that visibly gets stuck
  is better than a game that silently corrupts state.

---

## 3. Current status

| Area | Status |
|---|---|
| **i686 (Linux, 64-bit host included)** | playable: menus, campaigns (Terran/Zerg/Protoss), save and load, movies, audio |
| **ARMv7 hard-float (Pinebook Pro)** | playable: parity with i686 on menus, campaigns, save/load, intro movie |
| **ARMv6 (Raspberry Pi 1 / Pi Zero) — experimental** | the pipeline exists and the binary runs, but it has **not yet been validated on real hardware** (QEMU only, see §9.1) |
| **LAN multiplayer** | working between two instances: UDP discovery, game list, join, game with lockstep running |
| **Battle.net / PvPGN** | **not supported** (see §9.3) |
| **64-bit architectures** | **not supported**: the current IR uses 32-bit pointers (see §9.1) |

Useful numbers: code divergence between the two architectures is **1.08%** (224 lines out of 20,697,
listed in `reports/REPORT-x86-vs-arm.md`); the dispatch map has **102,392 entries, 0 collisions**.

---

## 4. Performance

Every number below is measured, not estimated; the method and the raw logs are in
`reports/PERF-WINE-COMPARISON.md`, `reports/PROF-CPU.md`, `reports/PROF-RAM.md`,
`reports/REPORT-opt-*.md` and `reports/REPORT-graphics-final.md`.

### 4.1 i686: native port vs the same game under Wine

Same machine (16 logical CPUs), CPU and memory sampled from `/proc` at 1 Hz, 5 samples per phase,
medians:

| Phase | CPU, native (% of one core) | CPU, Wine | RSS game, native | RSS game, Wine | RSS whole tree, native | RSS whole tree, Wine |
|---|---:|---:|---:|---:|---:|---:|
| Boot | 84.3 | 116.7 | 140.7 MiB | 206.5 MiB | 144.1 MiB | 437.4 MiB |
| Menu | 141.2 | 92.8 | 142.3 MiB | 202.9 MiB | 142.3 MiB | 412.4 MiB |
| Gameplay | 130.1 | 83.4 | 92.7 MiB | 164.5 MiB | 92.7 MiB | 339.1 MiB |

**Read these numbers with the caveat that matters**: the comparison is *not* frame-for-frame. The
native port is uncapped and rendered **79–610 FPS** in those runs, while the original under Wine keeps
its DirectDraw loop cap at **~24–30 FPS**. Per frame the native port does 2–6× the rendering work and
still costs the same or less CPU, so on equal frames it is clearly cheaper. What is structural is the
memory: Wine pays **+246…+293 MiB of whole-tree RSS** (explorer.exe, 2× winedevice.exe, services.exe,
wineserver) and **+60…+72 MiB on the game process alone**; the native port is a single ELF32 with no
extra processes.

### 4.2 What was optimised, and what it bought

| Optimisation | Effect (measured) |
|---|---|
| **SCode 1×/2× LUT fast paths** (Storm's pixel kernels, `runtime/common/storm_sdraw.c`) | **55.75×** faster than the interpreter on a 100×364×4 workload, with bit-exact parity for widths 1..364 |
| **Native SComp decompressor** (`storm_scomp.c`, Huffman + ADPCM) | 125.7 MiB/s on PKWARE-compressed sectors, 50.0 MiB/s on Huffman+ADPCM; 4292/4292 sectors byte-exact |
| **Frame pacing + syscall reduction** (`SC_FPS_CAP`) | from ~108% of one core (uncapped menu) to **~24% at 25 FPS** |
| **Presentation path** (CPU framebuffer with palette LUT, GPU texture, GL/GLES2 palette shader) | `SC_GL_VERIFY=1` confirms **307,200/307,200 pixels** identical between the GL frame and the CPU canvas |
| **Guest memory map cache** (`proc_range`) and dispatch cache | removed the per-call `/proc/self/maps` parsing that dominated the ARMv6 profile (~62–68%) |

### 4.3 ARMv7 (Pinebook Pro, RK3399 + Mali-T860)

| Path | Result |
|---|---|
| GL/GLES (Panfrost, GLES 3.1) | menu **170–207 FPS**; in-game **23–24 FPS** at 640×480 with pacing, about **18% of one core** |
| CPU framebuffer fallback | **64–67 FPS** in menus when the GL path is unavailable (software renderer refused) |
| Pixel parity GL vs CPU canvas | `SC_GL_VERIFY PASS: 307200 pixel esatti` |

### 4.4 How to measure it yourself

```sh
SC_FPS_LOG=1     # per-second frame-rate lines
SC_PROF=1        # in-process sampling profile
SC_GL_VERIFY=1   # compare the GL frame against the CPU canvas, pixel by pixel
SC_FPS_CAP=25    # cap the frame rate (the CPU cost scales down with it)
```
`reports/PROF-CPU.md` documents the sampling method (threads, phases, how to read the percentages).

---

## 5. Requirements

| Item | Notes |
|---|---|
| Your own copy of **StarCraft / Brood War 1.16.1** | the repository contains neither assets nor binaries |
| `clang`, `ld.lld`, `llc`, `opt`, `llvm-nm` | LLVM 18+ recommended |
| Python 3 | for the lifting and generation tools |
| SDL2 | runtime (x11/wayland) |
| `x86`: 32-bit multilib (`gcc-multilib` or equivalent) | to compile the `-m32` shims |
| `ARMv7`: armhf sysroot | in `build/sysroot`, or pass `SYSROOT=...` |
| `libsmacker` for your architecture | sources in `vendor/libsmacker`, loaded at runtime |

For the assets: copy (or symlink) into the game directory the `.mpq` archives, the `.snp` files, the
`characters/` and `Save/` directories, and the `maps/` folder.

---

## 6. Guide: building for a software/hardware platform

### 5.1 First of all: run the lifting

Lifting runs **once**, on the machine that holds the original binaries, and produces the artefacts
(`build/lift-full/StarCraft.ll`, `storm.ll`) that are then compiled for any target.

```sh
# 0) once: build the lifting tools (SR/llasm + llasm)
tools/build-tools.sh

# 1) lift the two binaries: PE to .llasm, and .llasm to LLVM IR (LLASM_TO_IR=1, the default)
tools/lift.sh /path/to/StarCraft.exe build/lift-full/StarCraft.llasm
tools/lift.sh /path/to/storm.dll     build/lift-full/storm.llasm

# results: build/lift-full/StarCraft.ll and build/lift-full/storm.ll
# (directory ignored by git: these are artefacts derived from the original binary)
```

The two names `StarCraft.ll` and `storm.ll` under `build/lift-full/` are what the entry pipeline
expects (`tools/build-entry-x86.sh` checks them and stops with an explicit message if they are
missing).

The IR is not versioned: **it derives from the original binary** and stays on your disk.

### 5.2 Building for x86 32-bit (Linux)

```sh
./tools/build.sh --arch x86 --skip-entry     # reuse the already compiled entry
./tools/build.sh --arch x86                  # recompile the entry too
# output: build/starcraft-x86
```

### 5.3 Building for ARMv7 hard-float (example: Pinebook Pro)

```sh
# entry for the target (creates build/lift-full/entry-armv7)
./tools/build-entry-arm.sh

# runtime + link for armv7a hard-float, with sysroot
ENTRY_DIR=$PWD/build/lift-full/entry-armv7 \
SYSROOT=$PWD/build/sysroot \
  ./tools/build-runtime-armv7.sh
# output: build/starcraft-armv7
```

> **Warning**: use `ENTRY_DIR=…/entry-armv7`. The `build/lift-full/entry/` directory holds an ARMv6
> entry: the resulting binary crashes with `SIGSEGV` at startup. This is the most common mistake when
> rebuilding from scratch.

### 5.4 Adding a new architecture or platform

The path is always the same, in five steps:

1. **Triple and flags**: choose clang's `--target` (`armv6-…-gnueabihf`,
   `riscv32-unknown-linux-gnu`, `aarch64-…`, …) and the CPU flags.
2. **Entry**: `tools/build-entry-arm.sh` is the template — copy it to `tools/build-entry-<arch>.sh`
   and change `TRIPLE`, `CPU_FLAGS`, `--sysroot`. If the IR has 32-bit pointers and the target is
   64-bit, see §9.1: you need llasm's 64-bit mode (`PTROFS_64BIT`).
3. **ISA seams**: `runtime/arch/<arch>/` only needs four things — indirect dispatch, COM bridge,
   guest zeroing, TLS. That is a few hundred lines of assembly and the only code that *must* be
   architecture-specific.
4. **Shared runtime**: `runtime/common/` and `runtime/shim-port/elf/` compile as they are. The only
   places to touch are the `#if defined(__arch__)` blocks: on this project they are **224 lines out of
   20,697**, listed one by one in `reports/REPORT-x86-vs-arm.md`.
5. **Presentation**: the shim speaks SDL2 + OpenGL/GLES; on a platform without them (direct
   framebuffer, KMS/DRM) you add a path in `host_elf.c` like the existing ones (`present_gpu`,
   `present_gl`, CPU framebuffer).

Verification of the step: `./tools/build.sh --arch <arch> --skip-entry` must produce the binary, and
`tools/run-isolated.sh` (or the equivalent launcher) must reach the menu with `FPS > 0` and
`mean > 0.02` on a screenshot.

---

## 7. Guide: using the port

The game looks for its assets in the **current directory**: run it from the folder that contains them.

```sh
cd /path/to/your/starcraft/copy
/path/to/repo/build/starcraft-x86
```

### Main environment variables

| Variable | Effect |
|---|---|
| `SDL_VIDEODRIVER` / `SDL_AUDIODRIVER` | `x11`/`wayland`, `pulseaudio` (or `dummy` in tests) |
| `SC_SMACKER_SO` | path of the `libsmacker` to load |
| `SC_SKIP_INTRO=1` | skip only the intro movie (`SC_SMACK=0` is **not** usable: the game exits) |
| `SC_PRESENT` | force the presentation path (`cpu`, `gl`, …) |
| `SC_GL_VERIFY=1` | compare the GL frame against the CPU canvas and print PASS/mismatch |
| `SC_FPS_CAP`, `SC_FPS_SLEEP`, `SC_FPS_LOG` | pacing and FPS logging |
| `SC_LAN_BIND`, `SC_LAN_PORT`, `SC_DISC_PORT`, `SC_LAN_PEERS` | LAN networking |
| `SC_CTL=1`, `SC_CTL_SOCK` | in-process automation API (`tools/scctl`) |
| `SC_PRESENT_DBG`, `SC_SNP_TRACE`, `REASM_TRACE` | diagnostic traces |

### LAN multiplayer between two instances on the same host

```sh
SC_LAN_BIND=127.0.0.51 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
SC_LAN_PEERS=127.0.0.52:16051 SC_PRESENT=cpu \
  ./tools/run-isolated.sh start --id lan-a --skip-intro 1 --smack 1

SC_LAN_BIND=127.0.0.52 SC_DISC_PORT=16051 SC_LAN_PORT=16052 \
SC_LAN_PEERS=127.0.0.51:16051 SC_PRESENT=cpu \
  ./tools/run-isolated.sh start --id lan-b --skip-intro 1 --smack 1
```

Then, in the two windows: *Multiplayer → Original → Local Area Network (UDP) → Ok → profile*; the host
creates the game, the client selects it from the list, the host starts. Details and diagnostics:
`reports/REPORT-lan-lockstep-fix.md`, `reports/REPORT-lan-step2b.md`.

### Automation and verification without looking at the screen

```sh
SC_PRESENT=cpu ./tools/run-isolated.sh start --id check --skip-intro 1 --smack 1
python3 tools/scctl --sock build/isolated/check/sandbox/sc-ctl.sock shot /tmp/f.png
tools/shot-stat.sh /tmp/f.png        # mean/max/standard deviation: "is it black or is there content?"
./tools/run-isolated.sh stop --id check
```

`sc_ctl` exposes `shot`, `rawshot`, `click`, `move`, `down`, `up`, `key`, `keydown`, `keyup`, `text`,
`state`. Clicks must be sent as `move` → `down` → ~250 ms pause → `up`: with a zero-latency click the
game's UI does not register it.

---

## 8. Tests and verification

```sh
for t in tests/*/check.sh; do echo "== $t"; bash "$t"; done    # 9 checks, x86
```

They cover: shim ABI (stdcall/`ret N`), the SComp decompressor (4292/4292 sectors byte-exact), the
SCode kernel (interpreter/LUT parity for widths 1..364), the GL palette (307,200 exact pixels), guest
memory mapping, the virtual memory pool, thread affinity, IO readahead, the `sc_ctl` API.

Beyond the tests, the project relies on two forms of verification that are worth more than a suite:

- **comparison with the oracle** (`reports/ANALYSIS-*.md`): every contract is proven against the
  decompiled binary, with the address cited;
- **numeric verification of screenshots** (`tools/shot-stat.sh`): mean luminance, maximum and standard
  deviation instead of visual inspection — so a graphics regression is a number, not an impression
  (see `docs/POLICY-immagini.md`).

---

## 9. Future work

### 9.1 ARMv6 and the Raspberry Pi Zero — the minimum hardware target

There is a **second ARM pipeline**, `tools/build-runtime-arm.sh`, targeting **ARMv6 + VFPv2 without
NEON** — that is the Raspberry Pi 1 and the **Pi Zero**. It is **experimental**: the binary builds and
runs, but it has **not been validated on real hardware yet**; the only run so far is in QEMU
(`reports/REPORT-armv6-native-qemu.md`).

The Pi Zero (single-core ARM1176JZF-S at 1 GHz, 512 MB, VideoCore IV) is the **absolute minimum
hardware we aim for**. It is the honest stress test: no NEON, no hardware 3D worth using, one core. If
the port is usable there, it is comfortable on anything above it. What is already in place for it:
scalar VFP paths in the blitters and in the guest CPU helpers, a software palette LUT for the
framebuffer path, and the elimination of the per-call `/proc/self/maps` parsing that used to dominate
the profile. What is missing: measurement and tuning on the device itself — frame pacing, the cost of
the intro movies, and the memory ceiling (512 MB shared with the GPU).

### 9.2 Other architectures and platforms

- **ARMv7**: done and validated on hardware (Pinebook Pro). **ARMv6 / Pi Zero**: pipeline ready,
  validation on hardware still to do — see §9.1.
- **64-bit (x86_64, aarch64, riscv64)**: the blocker is not the compiler, it is the IR: the current
  `.ll` files use 32-bit pointers (`p:32:32`, `inttoptr i32`), while the guest must keep seeing 32-bit
  addresses. The planned path, already identified in `PLAN-MULTIARCH.md` §7.2, is:
  1. use llasm's 64-bit mode (`PTROFS_64BIT`, `REG2PTR`/`PTR2REG`), so that the guest keeps
     `uint32_t` addresses while the generated code uses 64-bit pointers;
  2. extend `tools/dispatch-ir.py` to the `inttoptr i64`/`ptrtoint … to i64` forms;
  3. marshal `uint32_t` ↔ `uintptr_t` in the shims where guest pointers are translated into host
     pointers;
  4. verify `stdcall` arities and `va_list` for the LP64 ABIs (System V AMD64 / AAPCS64).
  Once that is done, the rest of the runtime is already architecture-neutral: the 64-bit port is
  *pipeline* work, not reimplementation.
- **RISC-V 32/64, LoongArch, PowerPC**: same five-step scheme as §5.4; for 32-bit little-endian
  targets the existing IR works without regenerating it.
- **Platforms without X11/Wayland** (framebuffer, KMS/DRM, handheld consoles): a dedicated
  presentation path is needed in `host_elf.c` (the code is already organised in separate paths: CPU
  framebuffer, GPU texture, GL/GLES).
- **Non-Linux platforms** (BSD, macOS): the shims use POSIX calls + SDL2; the work is mostly in path
  mapping (`/proc`, `mmap`, thread affinity) and in the loader.
- **Any other target**: the project is deliberately structured so that adding a platform is a matter
  of *pipeline*, not of rewriting the port. The evidence is in the repository itself: two
  architectures already share 98.9% of the same source (`reports/REPORT-x86-vs-arm.md`), the guest
  code is compiled from one single IR, and the architecture-specific surface is four small assembly
  files plus 224 conditional lines. Concretely, the candidates we consider realistic are 32-bit
  RISC-V boards, ARM boards with weak or absent GPUs (where the CPU framebuffer path is used), and
  retro handhelds/consoles that expose a framebuffer and SDL2 — each of them a self-contained
  experiment that does not destabilise the existing ports.

### 9.3 A private, customisable Battle.net

Today multiplayer goes through the game's native SNP UDP provider (`standard.snp`), which we have
reimplemented: discovery, game list, join and lockstep all work. **Battle.net is not supported**, and
this is the area with the most interesting room for growth, because a private server allows things
the original service no longer did.

Three routes, in order of increasing invasiveness:

1. **A compatible server (PvPGN or similar) plus a client that speaks BNCS.** The server already
   exists; the client is missing: in our port the game must talk to a TCP server. The two technical
   options are (a) also *lift* `battle.snp` — the PE that implements the Battle.net protocol — and give
   it the Winsock shims that do not exist today (18 `WSOCK32` ordinals), plus reading the gateways from
   the registry (`Software\Battle.net\Configuration`); or (b) write a **native provider** that
   implements the protocol towards the server directly, skipping `battle.snp`.
2. **A "local" Battle.net with new features.** With our own server we can add what the original never
   had or has lost: a persistent chat room with history, a lobby with game search, spectators,
   server-side shared replays, statistics and ladders, modern authentication (no CD key), a server that
   runs on a Raspberry Pi or in a container.
3. **Interoperability.** Our server can speak both BNCS (for original clients or via Wine) and a new
   protocol (JSON/WebSocket) for a modern client or a bot, acting as a bridge between the two.

Mind the constraints: `battle.snp` is Blizzard code (not redistributable: lifting stays local), the
original authentication is CD-key based (a private server must decide how to replace it), and the
classic Battle.net protocol is documented only by community reverse engineering (our approach is in
`PLAN-MULTIPLAYER.md` §4 with the analysis in `reports/ANALYSIS-snp-contracts.md`).

### 9.4 Other open work

- **Multiplayer determinism**: measure lockstep with a state checksum to rule out desync (especially
  cross-architecture, where 80-bit x87 and 64-bit `double` could diverge).
- **Network robustness**: disappearing host, game-list entry TTL, recovery after packet loss.
- **Performance**: `PLAN-PERF-OPT.md` still has open phases (memory-mapping cache invalidated by
  `VirtualAlloc`, accelerating Storm's hot code via `reasm_extern`, ARM tuning).
- **ARM test suite**: today the nine automatic checks are x86 only; on ARM verification is manual or
  through `sc_ctl`.

---

## 10. Repository layout

| Path | Content |
|---|---|
| `runtime/common/` | runtime shared between architectures (guest CPU, memory, dispatch, SCode, SComp) |
| `runtime/arch/{arm,x86}/` | ISA-specific assembly seams |
| `runtime/shim-port/elf/` | Win32/DirectDraw/DirectSound/GDI/Smacker/SNet shims + SDL2/GL presentation |
| `tools/` | build pipeline, lifting, bridge generation, `scctl`, `run-isolated.sh` sandbox |
| `tests/` | executable checks, all without a graphical interface |
| `reports/` | analyses (IDA oracle), phase reports, measurements — **index in `reports/INDEX.md`** |
| `docs/` | policies and cross-cutting notes |
| `vendor/` | third-party components (SR/llasm, udis86, libsmacker) with their own licences |

---

## 11. Licence, game data and trademarks

- **This project's code**: GNU General Public License v3.0 — see [`LICENSE`](LICENSE).
- **Third-party components**: their own licences, listed in [`THIRD-PARTY.md`](THIRD-PARTY.md)
  (SR/llasm MIT, udis86 BSD, libsmacker LGPL 2.1 loaded at runtime).
- **No game assets or binaries** are included or redistributed: you need your own, legitimate copy of
  StarCraft/Brood War 1.16.1, and the artefacts derived from lifting (IR, objects, IDA databases) stay
  on the user's machine, excluded from version control.
- StarCraft, Brood War, Battle.net and Blizzard Entertainment are trademarks of Blizzard
  Entertainment, Inc. This project is **not affiliated with or endorsed by** Blizzard.
