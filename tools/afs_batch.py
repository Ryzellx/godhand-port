#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6b — AFS batch classify + unpack (stdlib)
# Runtime: python3 | Input: ISO + FST verified PROJECT.md | Output: manifest CSV + per-class artifact
# Usage:
#   afs_batch.py classify <manifest.csv>       -> klasifikasi SEMUA 1445 entry (head-only, tanpa extract)
#   afs_batch.py unpack <outdir>               -> extract semua entry per-kelas + verifikasi byte-count
# Detail kelas: tim2 (dims/type), afs-nested (inner TOC count+bytes), adx (CRI marker),
# mpeg (pack header), vag/strm/riff, custom (head hex buat RE tahap 6d).
import struct, sys, os, csv, hashlib
from collections import Counter

ISO = "/root/ports/godhand/God_Hand_USA_SLUS21503.iso"
AFS_BASE = 4_769_792

def load_toc(f):
    f.seek(AFS_BASE)
    magic, cnt = struct.unpack("<4sI", f.read(8))
    assert magic == b"AFS\x00", magic
    toc = f.read(cnt * 8)
    return [struct.unpack_from("<II", toc, i * 8) for i in range(cnt)]

def classify_one(f, off, sz):
    """-> (kelas, detail)"""
    f.seek(AFS_BASE + off)
    head = f.read(min(0x200, sz))
    if head[:4] == b"AFS\x00":
        # nested AFS: parse inner TOC utk jumlah + total byte isi
        cnt = struct.unpack_from("<I", head, 4)[0]
        total = 0; n = 0
        if 0 < cnt < 100000 and 8 + cnt * 8 <= sz:
            f.seek(AFS_BASE + off + 8)
            toc = f.read(cnt * 8)
            for i in range(cnt):
                io_, isz = struct.unpack_from("<II", toc, i * 8)
                total += isz; n += 1
            return "afs-nested", f"inner={n} inner_bytes={total}"
        return "afs-nested", f"inner={cnt}(TOC tak valid)"
    if head[:4] == b"TIM2":
        pics = struct.unpack_from("<H", head, 6)[0]
        dims = []
        # layout pic-header (OpenKh tm2.md + empiris entry_0027): relatif ke off pic=16:
        # total u32@0, clutSize u32@4, imageSize u32@8, hdrSize u16@0xC, clutColors u16@0xE,
        # pf@0x10, mip@0x11, clutType@0x12, imageType@0x13, w u16@0x14, h u16@0x16
        if len(head) >= 16 + 24 and pics > 0:
            _tot, _cs, _isz, _hs, _cc, _pf, _mip, _ct, it, w, h = \
                struct.unpack_from("<IIIHHBBBBHH", head, 16)
            dims.append(f"pic0={w}x{h} imgtype={it}")
        return "tim2-texture", f"pics={pics} " + " ".join(dims)
    # CRI ADX standard: u16BE[0]=0x8000 (magic), u16BE[2]=header size (0x24=36 atau 0x6e4=1764 extended),
    # rate u32BE @0x08 (24000/48000 empiris), "(c)CRI" @0x20 — verifikasi empiris 2026-10-17
    if len(head) >= 32 and head[0] == 0x80 and head[1] == 0x00 and b"(c)CRI" in head[:0x40]:
        hs, rate = struct.unpack_from(">HI", head, 2)
        return "adx-audio", f"hdr_size={hs} rate={rate}"
    if head[:4] == b"VAGp":
        return "vag-audio", f"VAGp"
    if head[:4] == b"STRM":
        return "strm-audio", f"STRM"
    if head[:4] == b"\x00\x00\x01\xba":
        return "mpeg-movie", "PS pack header"
    if head[3:6] == b"\x01\xba":
        return "mpeg-movie", "PS pack (shift)"
    return "custom", head[:16].hex()

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "classify"
    with open(ISO, "rb") as f:
        toc = load_toc(f)
        if mode == "classify":
            outp = sys.argv[2]
            rows = []
            kinds = Counter(); bytes_by = Counter()
            for i, (off, sz) in enumerate(toc):
                k, det = classify_one(f, off, sz)
                rows.append((i, off, sz, k, det))
                kinds[k] += 1; bytes_by[k] += sz
            with open(outp, "w", newline="") as fp:
                w = csv.writer(fp)
                w.writerow(["idx", "offset_iso", "size", "class", "detail"])
                w.writerows(rows)
            tot = sum(s for _, s in toc)
            print(f"AFS entries: {len(toc)} | total isi: {tot} B ({tot/1e9:.3f} GB)")
            for k in sorted(bytes_by, key=lambda x: -bytes_by[x]):
                print(f"  {k:14s} count={kinds[k]:5d} bytes={bytes_by[k]:>12,}")
            print(f"manifest -> {outp}")

        elif mode == "unpack":
            outdir = sys.argv[2]
            os.makedirs(outdir, exist_ok=True)
            manifest = {}
            with open(sys.argv[3] if len(sys.argv) > 3 else
                      os.path.join(outdir, "..", "afs_manifest.csv")) as fp:
                for row in csv.DictReader(fp):
                    manifest[int(row["idx"])] = row
            stats = Counter(); bstats = Counter()
            for i, (off, sz) in enumerate(toc):
                k = manifest[i]["class"]
                path = os.path.join(outdir, f"{k}", f"entry_{i:04d}.bin")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                f.seek(AFS_BASE + off)
                rem, h = sz, hashlib.md5()
                with open(path, "wb") as out:
                    while rem > 0:
                        chunk = f.read(min(1 << 20, rem))
                        if not chunk:
                            print(f"FAIL entry {i}: ISO EOF (sisa {rem})"); sys.exit(1)
                        out.write(chunk); h.update(chunk); rem -= len(chunk)
                got = os.path.getsize(path)
                assert got == sz, f"entry {i}: size {got} != toc {sz}"
                stats[k] += 1; bstats[k] += sz
            print("UNPACK DONE, semua size cocok TOC")
            for k in sorted(stats, key=lambda x: -bstats[x]):
                print(f"  {k:14s} files={stats[k]:5d} bytes={bstats[k]:>12,}")
        else:
            print(__doc__)

if __name__ == "__main__":
    main()
