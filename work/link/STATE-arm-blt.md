# Handoff state — Storm ARMv6 blitter (2026-11-20)

Primary report: `work/link/REPORT-arm-blt.md` — read its **Nuova evidenza runtime** and **Stato dei wrapper** sections first.

## Verified
- JIT at `0x20170A9C`: first 64 bytes are eight repeats of `89 0F 81 C7 04 00 00 00`; llvm-mc = `movl %ecx,(%edi); addl $4,%edi` (PATCOPY DWORD fill). SRCCOPY JIT not captured.
- Current movie run actually reaches `storm_334_arm` (not only fast path): `build/qa/run-blt-code.log`, exit 73. Return host maps to `loc_1502F42F`, caller block `loc_1502F428`; do NOT infer ROP from `EBP+0xCC`.
- SCode bytes captured at `a1[5]=0x201600BC` (size `0x1A`) and `a1[17]=0x20170FAC` (size `0x1685`); exact bytes/disassembly in primary report. The latter appears to do byte-pair/table-indexed conversion and 16-bit stores; full data/table semantics remain unproven.
- `storm_313_arm_body`, `storm_334_arm_body`, assembly trampoline, SCI/external declarations and `--map-extra` are present. `storm_334` intentionally remains fail-fast exit 73 because ROP/caller semantics are not proven. End-to-end pixels/menu unverified.
- `lift-full.sh srw storm` + `ir storm` PASS; full entry build/link done; `--dump-map` 96966 unique entries, 0 collisions. `make check` PASS before the last diagnostic-only runtime rebuild. Current ELF `build/starcraft-armv6-blt` SHA256 `8c4654cf41e4c74dbe310fdc51adec7d29598e920ffdd15234d30132b140ac2f`.
- `StarCraft.ll` unchanged (SHA256 in report), but `StarCraft.entry.o` was rebuilt (SHA in report). All compilation/link/test commands used `TMPDIR=/dev/shm`.

## Next safe steps
1. Fix `dump_bytes()` writer in `runtime/arm/storm_sdraw_arm.c`: it writes 49 bytes though only 48 are initialized, producing a stray `@`; rebuild with TMPDIR.
2. Resume IDA analysis of caller `0x1502F428` (previous IDA worker became unreachable); establish caller/API semantics and ROP from actual source, not guessed EBP offset.
3. Decode the complete SCode buffers/relocations and associated lookup table; only then implement supported `storm_334` modes and retain exit 73 for unknowns.
4. Capture SRCCOPY runtime kernel separately, verify `storm_313_arm` call path and guest stdcall cleanup; run `make check`, dump-map, smoke, and update primary report.

No menu reached; leave other IDA sessions untouched. Do not claim implementation complete.