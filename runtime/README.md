# Runtime layout and adding an architecture

`runtime/common/` holds the architecture-independent runtime C/H, generators, bridge metadata,
patches, headers, and tests. `runtime/arch/<arch>/` holds ABI/ISA-specific assembly and TLS seams.
The current targets are `x86` (i686 Linux) and `arm` (ARMv6 hard-float Linux).

## Build

```sh
tools/build.sh --arch x86 [--skip-entry] [--no-link]
tools/build.sh --arch arm [--no-link]
```

`--skip-entry` reuses existing x86 `build/lift-full/entry-x86/*.entry.o` objects. `--no-link`
stops after compilation. The legacy `build-runtime-*`, `build-entry-obj.sh`, `link-*`, and x86
runtime test scripts remain usable.

### ARM host prerequisites / current verification boundary

On Fedora `frigate`, the ARM assembly can be assembled directly with Clang's integrated assembler
(no ARM sysroot required): `runtime/arch/arm/*.S` and generated `bridge_stubs.S` produce ELF32 ARM
objects. `tools/dispatch-ir.py` plus `opt`/`llc` can likewise verify and emit ARM objects from the
lifted IR without the libc sysroot.

The full ARM runtime C build, `guest_pe_bytes.S` generation/assembly, integrated
`tools/build.sh --arch arm`, and runtime link require the ARMv6 hard-float sysroot at
`build/sysroot/` (glibc headers/libraries, startup objects and SDL2). Host Fedora headers are not a
substitute: Clang targeting ARM rejects host glibc float modes. The generator `gen_game_layout.py`
additionally requires the ARM entry objects; `gen_pe_image_bytes.py` requires the resulting
`.guest_*` linker-layout entries and their input objects; `gen_storm_ordinals.py` requires
`analysis/storm_ordinals.json` from the companion `sc-reasm` checkout. These inputs are not
included in a lightweight source workspace. See `reports/REPORT-arm-parity.md` for the exact
frigate run and results.

## Adding a 32-bit little-endian target

Reuse the lifted 32-bit guest IR if the target ABI supports its pointer/data layout. Add only the
architecture-specific indirect-dispatch and COM trampolines, bridge-stub emission and any TLS or
`ucontext` register access required by the ABI. Add target triple, CPU/ABI flags, linker/emulation,
and selected assembly to the unified build driver. Then run runtime ABI tests, verify the ELF
class/machine and guest-address map, and perform a real smoke test. Never widen indirect target
resolution to unverified addresses; unsupported callbacks and SCode remain fail-closed.

## Adding a 64-bit target

The current lifted IR uses 32-bit pointers and cannot be linked directly as LP64. First regenerate
IR with the LLASM 64-bit pointer-offset mode, then update dispatch/map representations and guest
pointer marshalling (`uint32_t` guest VA versus host `uintptr_t`). Implement the 64-bit ABI
trampolines and validate stack/callback/variadic conventions before enabling the target. Keep the
guest VA space and host mapping policy explicit; do not assume pointers are interchangeable.
