#!/usr/bin/env python3
"""Regression check for IDA-confirmed overlapping dispatch operands in StarCraft.exe."""
import struct
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
IMAGE = ROOT.parent.parent / "build/relocs/StarCraft.exe"
CSV = ROOT / "relocations.csv"
SRW = ROOT.parents[1] / "build/loader/srw-src/SRW.exe"


def read_pe(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise AssertionError("bad PE signature")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    imagebase = struct.unpack_from("<I", data, opt + 28)[0]
    table = opt + opt_size
    ranges = []
    for i in range(sections):
        off = table + 40 * i
        virtual_size, rva, raw_size, raw_off = struct.unpack_from("<IIII", data, off + 8)
        ranges.append((rva, max(virtual_size, raw_size), raw_off, raw_size))
    return data, imagebase, ranges


def raw_offset(va, imagebase, ranges, width=1):
    rva = va - imagebase
    for start, extent, raw, raw_size in ranges:
        if start <= rva and rva + width <= start + raw_size:
            return raw + rva - start
    raise AssertionError(f"VA {va:#x} not file-backed")


class RelocOverlapTests(unittest.TestCase):
    def test_dispatch_relocations_match_instruction_bytes(self):
        data, imagebase, ranges = read_pe(IMAGE)
        self.assertEqual(imagebase, 0x400000)
        rows = {}
        for line in CSV.read_text().splitlines():
            if not line.strip():
                continue
            fixup, target, *_ = line.split(",")
            rows[int(fixup, 0)] = int(target, 0)
        expected = {
            0x406C40: (0x406C50, 0x406C3D, bytes.fromhex("ff2485506c4000"), 0x406C4C, bytes.fromhex("ff248dd06c4000")),
            0x406DD4: (0x406DDC, 0x406DD1, bytes.fromhex("ff2485dc6d4000"), 0x406DD8, bytes.fromhex("ff248dd86e4000")),
        }
        for fixup, (target, instruction, opcode, overlapping_start, overlapping_opcode) in expected.items():
            with self.subTest(fixup=hex(fixup)):
                self.assertEqual(rows.get(fixup), target)
                inst_off = raw_offset(instruction, imagebase, ranges, len(opcode))
                self.assertEqual(data[inst_off:inst_off + len(opcode)], opcode)
                operand_off = raw_offset(fixup, imagebase, ranges, 4)
                self.assertEqual(struct.unpack_from("<I", data, operand_off)[0], target)
                # The fixup target is byte +4 (the disp32 field) of a different,
                # overlapping indirect-jump instruction. This is the precise
                # ambiguity behind SRW's reading-inside-instruction diagnostic.
                overlap_off = raw_offset(overlapping_start, imagebase, ranges, len(overlapping_opcode))
                self.assertEqual(data[overlap_off:overlap_off + len(overlapping_opcode)], overlapping_opcode)
                self.assertEqual(target, overlapping_start + 4)
        self.assertEqual(len(rows), len(set(rows)))

    def test_srw_reaches_later_gate_without_overlap_errors(self):
        if not SRW.is_file():
            self.skipTest(f"SRW not built: {SRW}")
        run = subprocess.run(
            [str(SRW), str(IMAGE)], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90,
        )
        self.assertIn("[ordinal-import-gate] DLL=storm.dll resolved=135/135", run.stdout)
        self.assertNotIn("reading inside of instruction", run.stdout)
        self.assertIn("Error: unknown flags", run.stdout)
        self.assertNotEqual(run.returncode, 0, "this test records the next known lifting gate")


if __name__ == "__main__":
    unittest.main()
