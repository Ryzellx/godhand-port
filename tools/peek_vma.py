#!/usr/bin/env python3
"""peek_vma.py <elf> <vma> [len] — dump bytes/words/ascii at ELF virtual address (via PT_LOAD map)."""
import sys, struct
elf, vma = sys.argv[1], int(sys.argv[2], 0)
n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 64
d = open(elf, 'rb').read()
phoff, = struct.unpack_from('<I', d, 0x1c); phentsize, = struct.unpack_from('<H', d, 0x2a); phnum, = struct.unpack_from('<H', d, 0x2c)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<IIIIIIII', d, o)
    if t == 1: segs.append((va, va + fsz, off, va, msz))
def read(addr, ln):
    for s, e, off, va, msz in segs:
        if s <= addr < e:
            k = min(ln, e - addr)
            return d[off + (addr - s): off + (addr - s) + k]
    return None
b = read(vma, n)
if b is None: print(f"0x{vma:08x} NOT MAPPED"); sys.exit(1)
for i in range(0, len(b), 16):
    chunk = b[i:i+16]
    words = ' '.join(f"{struct.unpack_from('<I', chunk, j)[0]:08x}" for j in range(0, len(chunk) - 3, 4))
    asc = ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)
    print(f"0x{vma+i:08x}  {words:<39} |{asc}|")
print("--- ascii run ---")
print(repr(b.split(b'\0')[0][:200]))
