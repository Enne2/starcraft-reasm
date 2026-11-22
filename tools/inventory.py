#!/usr/bin/env python3
"""Triage vector/x87 instructions inside IDA-exported function ranges.

Not an exact inventory of executed instructions. Function spans can contain
inline data or gaps; Capstone stops at the first undecodable byte in each span.
"""
import argparse
import collections
import json
import pathlib
import re

import capstone as cs
from capstone import x86_const as xc
import pefile


def inventory(image, export, emitter):
    metadata = json.loads(export.read_text())
    pe = pefile.PE(str(image), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    if base != metadata['imagebase']:
        raise ValueError(f'{image}: image base differs from IDA export')
    dis = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
    dis.detail = True
    interesting_groups = {
        getattr(xc, name) for name in dir(xc)
        if name.startswith(('X86_GRP_MMX', 'X86_GRP_SSE', 'X86_GRP_SSSE',
                            'X86_GRP_AVX', 'X86_GRP_3DNOW', 'X86_GRP_FPU'))
    }
    cases = set(re.findall(r'\bcase\s+UD_I([a-zA-Z0-9_]+)\s*:', emitter.read_text()))
    seen = set()
    ranges_skipped = ranges_incomplete = decoded = 0
    hits = collections.defaultdict(list)
    for f in metadata['functions']:
        start, end = f['va'], f['end']
        rva, length = start - base, end - start
        sec = pe.get_section_by_rva(rva)
        if sec is None or length <= 0 or not sec.Characteristics & 0x20000000:
            ranges_skipped += 1
            continue
        if rva + length > sec.VirtualAddress + sec.SizeOfRawData:
            ranges_skipped += 1
            continue
        pos = start
        for ins in dis.disasm(pe.get_data(rva, length), start):
            pos = ins.address + ins.size
            if ins.address in seen:
                continue
            seen.add(ins.address)
            decoded += 1
            if interesting_groups.intersection(ins.groups):
                hits[ins.mnemonic].append(ins.address)
        if pos != end:
            ranges_incomplete += 1
    return {
        'image': image.name,
        'function_ranges': len(metadata['functions']),
        'ranges_skipped': ranges_skipped,
        'ranges_incomplete': ranges_incomplete,
        'instruction_addresses_decoded': decoded,
        'instructions': {
            mnemonic: {'count': len(v), 'examples_va': [hex(x) for x in sorted(v)[:6]],
                       'literal_emitter_case_found': mnemonic in cases}
            for mnemonic, v in sorted(hits.items())
        },
        'caveat': 'Triage only: function spans may contain data; not a reachability analysis. A literal switch case does not establish operand or flag coverage.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=pathlib.Path)
    parser.add_argument('ida_functions', type=pathlib.Path)
    parser.add_argument('--emitter', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / 'vendor/sr/SRW/SR_full_llasm_instr.c')
    args = parser.parse_args()
    print(json.dumps(inventory(args.image, args.ida_functions, args.emitter), indent=2))


if __name__ == '__main__':
    main()
