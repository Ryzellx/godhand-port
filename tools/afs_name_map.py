#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d — AFS entry name map from FST
# Intel (verified this session): FST file record = {u32 name_off, u32 afs_index, u32 field_b}
#   proof: FST 'el00.dat' a=77 -> TOC[77].size 327392 == extract/afs/custom/entry_0077.bin size.
# This tool builds extract/afs_named_manifest.csv and verifies the mapping batch-wide:
#   name resolution + FST.a < 1445 + TOC[FST.a].size == size(on disk) per named entry.
# Runtime: python3 stdlib
import struct, os, csv, collections

ISO = "/root/ports/godhand/God_Hand_USA_SLUS21503.iso"
FST = "/root/ports/godhand/extract/GODHAND.FST"
AFS_BASE = 4_769_792
AFS_DIR = "/root/ports/godhand/extract/afs"
OUT = "/root/ports/godhand/extract/afs_named_manifest.csv"
INV = "/root/ports/godhand/extract/dat_inventory.csv"


def load_fst():
    data = open(FST, "rb").read()
    assert data[:4] == b"FST\x00", "bad FST magic"
    strsz = struct.unpack_from("<I", data, 0x0C)[0]
    strtab = data[0x10:0x10 + strsz]
    blob = data[0x10 + strsz:]
    nrec = (len(blob) - 8) // 12
    recs = []
    for i in range(nrec):
        noff, a, b = struct.unpack_from("<III", blob, i * 12)
        end = strtab.find(b"\x00", noff)
        recs.append((i, strtab[noff:end].decode("ascii", "replace"), a, b))
    return recs


def load_toc():
    with open(ISO, "rb") as f:
        f.seek(AFS_BASE)
        magic, cnt = struct.unpack("<4sI", f.read(8))
        assert magic == b"AFS\x00", "bad AFS magic"
        toc = f.read(cnt * 8)
    return [struct.unpack_from("<II", toc, i * 8) for i in range(cnt)]


def main():
    recs = load_fst()
    toc = load_toc()
    n = len(toc)

    # entry size on disk (cheap: classify dir layout used by afs_batch.py)
    sizes = {}
    for klass in os.listdir(AFS_DIR):
        d = os.path.join(AFS_DIR, klass)
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            m = None
            if fn.startswith("entry_") and fn[6:10].isdigit():
                m = int(fn[6:10])
            if m is not None:
                sizes[m] = os.path.getsize(os.path.join(d, fn))

    # kind from dat_inventory.csv
    kinds = {}
    with open(INV, newline="") as f:
        for row in csv.DictReader(f):
            base = row["file"][6:10]
            if base.isdigit():
                kinds[int(base)] = row["kind"]

    # FST -> index map (first claim wins; conflicts counted)
    name_of = {}
    conflicts = 0
    in_range = 0
    for _, nm, a, _b in recs:
        if a < n:
            in_range += 1
            if a in name_of and name_of[a] != nm:
                conflicts += 1
                continue
            name_of.setdefault(a, nm)

    # verification: TOC[a].size == size on disk for named entries
    verified = 0
    size_mismatch = 0
    missing = 0
    for a, nm in name_of.items():
        if a not in sizes:
            missing += 1
            continue
        if toc[a][1] == sizes[a]:
            verified += 1
        else:
            size_mismatch += 1

    with open(OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["idx", "fst_name", "size", "toc_size", "kind"])
        for i in range(n):
            w.writerow([i, name_of.get(i, ""), sizes.get(i, ""), toc[i][1], kinds.get(i, "")])

    total_named = len(name_of)
    print(f"FST records                 : {len(recs)}")
    print(f"FST records with a<AFS count: {in_range}")
    print(f"AFS entries named           : {total_named} / {n}")
    print(f"name conflicts (same idx)   : {conflicts}")
    print(f"SIZE VERIFIED (toc==disk)   : {verified}")
    print(f"size mismatch               : {size_mismatch}")
    print(f"named but file missing      : {missing}")
    print(f"coverage                    : {verified}/{n} = {100.0*verified/n:.1f}%")
    # name groups
    grp = collections.Counter()
    for nm in name_of.values():
        grp["".join(c for c in nm if not c.isdigit()) or nm] += 1
    print("top name families:", grp.most_common(15))
    # spot check player + rooms + a few
    for probe in ("pl00.dat", "pl01.dat", "em00.dat", "r000.rel", "el00.dat"):
        hit = [i for i, nm in name_of.items() if nm == probe]
        if hit:
            i = hit[0]
            print(f"  probe {probe}: entry_{i:04d} size={sizes.get(i)} kind={kinds.get(i)}")
    print(f"csv -> {OUT}")


if __name__ == "__main__":
    main()
