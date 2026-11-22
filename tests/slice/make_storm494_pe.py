#!/usr/bin/env python3
"""Minimal PE32 slice carrying exact Storm #494 instructions from IDA."""
import struct
from pathlib import Path
OUT=Path(__file__).resolve().parents[2]/'build/slice'
OUT.mkdir(parents=True,exist_ok=True)
code=bytes.fromhex('55 8b ec 8b 4d 0c 85 c9 8b 45 08 74 0a 8d 49 00 c6 00 00 40 49 75 f9 5d c2 08 00')
assert len(code)==0x1b
dos=bytearray(0x80); dos[:2]=b'MZ'; struct.pack_into('<I',dos,0x3c,0x80)
coff=struct.pack('<HHIIIHH',0x14c,2,0,0,0,224,0x2102)
opt=bytearray(224)
struct.pack_into('<HBB',opt,0,0x10b,1,0)
struct.pack_into('<IIIIII',opt,4,0x200,0,0,0x1000,0x1000,0x1000)
struct.pack_into('<II',opt,28,0x400000,0x1000)
struct.pack_into('<I',opt,36,0x200)
struct.pack_into('<II',opt,56,0x3000,0x200)
struct.pack_into('<H',opt,68,3)
struct.pack_into('<IIII',opt,72,0x100000,0x1000,0x100000,0x1000)
struct.pack_into('<I',opt,92,16)
struct.pack_into('<II',opt,96,0x2000,40)
# One unnamed export at ordinal base 494; EAT points at exact routine RVA 0x1000.
edata=bytearray(0x200)
struct.pack_into('<IIHHIIIIIII',edata,0,0,0,0,0,0x2030,494,1,0,0x2028,0x202c,0)
struct.pack_into('<I',edata,40,0x1000)
struct.pack_into('<I',edata,44,0)
edata[48:58]=b'storm.dll'+bytes([0])
sh1=struct.pack('<8sIIIIIIHHI',b'.text'+bytes(3),len(code),0x1000,0x200,0x200,0,0,0,0,0x60000020)
sh2=struct.pack('<8sIIIIIIHHI',b'.edata'+bytes(2),59,0x2000,0x200,0x400,0,0,0,0,0x40000040)
headers=dos+b'PE'+bytes(2)+coff+opt+sh1+sh2
headers+=bytes(0x200-len(headers))
image=headers+code+bytes(0x200-len(code))+edata
(OUT/'storm494_slice.exe').write_bytes(image)
print(f'{OUT}/storm494_slice.exe: {len(image)} bytes; exact source VA 0x15023B00; 27-byte body; export #494')
