#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6c — inner AFS (nested) → ADX inventory (stdlib)
# Runtime: python3 | Input: extract/afs/afs-nested/*.bin (Phase 6b, byte-verified vs TOC)
# Usage: adx_batch.py [manifest.csv]   -> argv[1] = path output langsung
# Format (empiris PROJECT.md log 2026-10-17): inner AFS = AFS\0 + u32 cnt + cnt*{u32 off,u32 size}.
# ADX: u16BE[0]=0x8000, hdr_size u16BE@2 (0x24 plain / 0x6e4 extended), rate u32BE@8,
#      sample_count u32BE@0x0C, channels u8@0x0A(?), "(c)CRI"@0x20.
import struct, sys, os, csv, glob
from collections import Counter

NESTED_DIR = "/root/ports/godhand/extract/afs/afs-nested"
MAGIC = b"AFS\x00"

def main():
    outp = sys.argv[1] if len(sys.argv) > 1 else "/root/ports/godhand/extract/adx_inner_manifest.csv"
    files = sorted(glob.glob(os.path.join(NESTED_DIR, "entry_*.bin")))
    assert files, f"tidak ada nested file di {NESTED_DIR}"
    rows = []
    kind = Counter(); hdr = Counter(); rate = Counter()
    for nf in files:
        outer = os.path.basename(nf)[6:10]
        szf = os.path.getsize(nf)
        with open(nf, "rb") as f:
            magic, cnt = struct.unpack("<4sI", f.read(8))
            assert magic == MAGIC, (nf, magic)
            assert 0 < cnt < 100000 and 8 + cnt * 8 <= szf, (nf, cnt, szf)
            toc = f.read(cnt * 8)
            for i in range(cnt):
                io_, isz = struct.unpack_from("<II", toc, i * 8)
                assert io_ + isz <= szf, f"{nf} inner {i}: {io_}+{isz} > {szf}"
                f.seek(io_)
                head = f.read(min(isz, 0x700))
                k, det = "unknown", ""
                if len(head) >= 0x24 and head[0] == 0x80 and head[1] == 0x00:
                    hs = struct.unpack_from(">H", head, 2)[0]
                    rate_, scnt = struct.unpack_from(">II", head, 8)
                    ch = head[0x08 - 2] if False else head[0x07]  # byte@7 = channel type umum
                    crci = b"(c)CRI" in head[:hs if hs <= len(head) else len(head)]
                    k = "adx"
                    det = f"hs=0x{hs:x} rate={rate_} samples={scnt} ch_byte7={ch} cri={crci}"
                    hdr[hs] += 1; rate[rate_] += 1
                elif head[:4] == MAGIC:
                    k = "nested-again"
                elif head[:4] == b"VAGp":
                    k = "vag"
                elif head[:4] == b"RIFF":
                    k = "riff"
                else:
                    det = head[:8].hex()
                kind[k] += 1
                rows.append([outer, i, io_, isz, k, det])
    with open(outp, "w", newline="") as fp:
        w = csv.writer(fp)
        w.writerow(["outer_entry", "inner_idx", "inner_off", "inner_size", "class", "detail"])
        w.writerows(rows)
    tot = sum(r[3] for r in rows)
    print(f"nested files: {len(files)} | inner entries: {len(rows)} | inner bytes: {tot:,}")
    for k, v in kind.most_common():
        print(f"  class {k}: {v}")
    print(f"  hdr_size: {dict(hdr)}")
    print(f"  rates: {dict(rate)}")
    print(f"manifest -> {outp}")

if __name__ == "__main__":
    main()
