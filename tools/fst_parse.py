#!/usr/bin/env python3
# God Hand (SLUS-215.03) asset toolchain — FST manifest + _DATA.AFS extractor
# Runtime: python3 stdlib | Target: port pipeline Phase-1 recon
# Usage:
#   fst_parse.py manifest                    -> print asset manifest (FST x AFS cross-ref)
#   fst_parse.py extract <idx|all> <outdir>  -> extract AFS entries by index
import struct, sys, os

ISO = "/root/ports/godhand/God_Hand_USA_SLUS21503.iso"
FST = "/root/ports/godhand/extract/GODHAND.FST"
AFS_BASE = 4_769_792          # byte offset _DATA.AFS di ISO (terverifikasi via magic scan)
STRHDR_SIZE = 16
STRSZ = 0x3844
REC = 12                       # {u32 name_off, u32 field_a, u32 field_b} + 8B footer

def load_fst():
    data = open(FST, "rb").read()
    assert data[:4] == b"FST\x00"
    strtab = data[STRHDR_SIZE:STRHDR_SIZE + STRSZ]
    blob = data[STRHDR_SIZE + STRSZ:]
    nrec = (len(blob) - 8) // REC
    def nm(off):
        end = strtab.find(b"\x00", off)
        return strtab[off:end].decode("ascii", "replace")
    recs = []
    for i in range(nrec):
        noff, a, b = struct.unpack_from("<III", blob, i * REC)
        recs.append({"i": i, "name": nm(noff), "a": a, "b": b,
                     "is_dir": (b == 1)})
    return recs

def load_afs_toc(f):
    f.seek(AFS_BASE)
    head = f.read(8)
    magic, cnt = struct.unpack("<4sI", head)
    assert magic == b"AFS\x00", magic
    toc = f.read(cnt * 8)
    return [struct.unpack_from("<II", toc, i * 8) for i in range(cnt)]

def classify(head: bytes) -> str:
    if head[:4] == b"AFS\x00":  return "afs-nested"
    if head[:4] == b"TIM2":     return "tim2-texture"
    if head[:4] == b"VAGp":     return "vag-audio"
    if head[:4] == b"STRM":     return "strm-audio"
    if head[3:6] == b"\x01\xba": return "mpeg-movie"
    if head[:4] == b"Riff" or head[:4] == b"RIFF": return "riff"
    return "custom"

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "manifest"
    recs = load_fst()
    with open(ISO, "rb") as f:
        toc = load_afs_toc(f)

        if mode == "manifest":
            dirs = [r for r in recs if r["is_dir"]]
            files = [r for r in recs if not r["is_dir"]]
            print(f"FST: {len(dirs)} dirs, {len(files)} files | AFS entries: {len(toc)}")
            # classify sample tiap 37 entry biar cepat
            from collections import Counter
            kinds = Counter()
            for i in range(0, len(toc), 37):
                off, sz = toc[i]
                f.seek(AFS_BASE + off)
                kinds[classify(f.read(8))] += 1
            print("AFS content classes (sample):", dict(kinds))

        elif mode == "extract":
            idx_arg = sys.argv[2]
            outdir = sys.argv[3]
            os.makedirs(outdir, exist_ok=True)
            idxs = range(len(toc)) if idx_arg == "all" else [int(idx_arg)]
            for i in idxs:
                off, sz = toc[i]
                f.seek(AFS_BASE + off)
                remaining, path = sz, os.path.join(outdir, f"entry_{i:04d}.bin")
                with open(path, "wb") as out:
                    while remaining > 0:
                        chunk = f.read(min(1 << 20, remaining))
                        if not chunk: break
                        out.write(chunk)
                        remaining -= len(chunk)
                print(f"extracted #{i} -> {path} ({sz - remaining} B)")

if __name__ == "__main__":
    main()
