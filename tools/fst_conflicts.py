#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d.6f — konflik nama FST + konsistensi field SNR2
# Runtime: python3 stdlib. Spec: FST record {u32 name_off, u32 a, u32 b}; SEMANTIK b
#   diturunkan empiris 1543 record: b&0xFF = flag {1=dir(47), 2=file(1445), 3=alias/non-AFS(50),
#   0=1}; utk file `a` = indeks entri _DATA.AFS (verifikasi bijeksi: 1486 record flag2/3 valid
#   -> 1445 unik idx, SEMUA entri AFS ter-referensi).
# Usage:
#   fst_conflicts.py -> konflik nama (CSV) + SNR2 field census (stdout)
#   fst_conflicts.py snr2 -> SNR2 saja
import sys, os, csv, struct, hashlib, collections, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fst_parse import load_fst, load_afs_toc, ISO, STRHDR_SIZE, STRSZ, REC

OUT = "/root/ports/godhand/extract"
CUSTOM = os.path.join(OUT, "afs/custom")


def load_recs():
    data = open(os.path.join(OUT, "GODHAND.FST"), "rb").read()
    strtab = data[STRHDR_SIZE:STRHDR_SIZE + STRSZ]
    blob = data[STRHDR_SIZE + STRSZ:]
    nrec = (len(blob) - 8) // REC

    def nm(off):
        e = strtab.find(b"\x00", off)
        return strtab[off:e].decode("ascii", "replace")
    return [struct.unpack_from("<III", blob, i * REC) + (nm(struct.unpack_from("<I", blob, i * REC)[0]),)
            for i in range(nrec)]


def cmd_conflicts():
    recs = load_recs()
    with open(ISO, "rb") as f:
        toc = load_afs_toc(f)
        byname = collections.defaultdict(list)
        for i, (n, a, b, name) in enumerate(recs):
            if (b & 0xFF) in (2, 3) and a < len(toc):
                byname[name].append((i, a, b))
        all_a = [a for v in byname.values() for _, a, _ in v]
        dup = {k: v for k, v in byname.items() if len(v) > 1}
        print(f"file-record flag2/3 dgn a valid: {len(all_a)} records, "
              f"{len(set(all_a))} unik AFS idx (bijeksi utk 1445 entri)")
        rows = []
        for name, rs in sorted(dup.items()):
            det = []
            for i, a, b in rs:
                off, sz = toc[a]
                f.seek(4769792 + off)
                md5 = hashlib.md5(f.read(min(65536, sz))).hexdigest()[:12]
                det.append((i, a, sz, md5, b & 0xFF))
            same = len({d[3] for d in det}) == 1 and len({d[2] for d in det}) == 1
            rows.append(dict(name=name, n=len(det), cmp="SAME" if same else "DIFF",
                             entries=";".join(f"fst{d[0]}/afsi{d[1]}/{d[2]}B/f{d[4]}" for d in det)))
    path = os.path.join(OUT, "fst_name_conflicts.csv")
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["name", "n", "cmp", "entries"])
        w.writeheader(); w.writerows(rows)
    ns = sum(1 for r in rows if r["cmp"] == "SAME")
    print(f"konflik nama: {len(rows)} nama, {sum(r['n'] for r in rows)} entri "
          f"(SAME={ns} DIFF={len(rows)-ns}) -> {path}")


def cmd_snr2():
    rels = [q for q in sorted(glob.glob(os.path.join(CUSTOM, "*.bin")))
            if open(q, "rb").read(4) == b"SNR2"]
    F = collections.defaultdict(list)
    for q in rels:
        d = open(q, "rb").read(64)
        sz = os.path.getsize(q)
        for off in range(4, 64, 4):
            F[off].append(struct.unpack_from("<I", d, off)[0])
    print(f"SNR2 files: {len(rels)}")
    for off in sorted(F):
        vs = F[off]
        eq = sum(1 for v, q in zip(vs, rels) if v == os.path.getsize(q))
        const = len(set(vs)) == 1
        print(f"  +{off:02d}: const={const}({vs[0] if const else '-'}) "
              f"eq_size={eq}/{len(rels)} min={min(vs)} max={max(vs)}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "conflicts"
    if mode == "conflicts":
        cmd_conflicts(); cmd_snr2()
    elif mode == "snr2":
        cmd_snr2()
    else:
        print(__doc__); sys.exit(2)


if __name__ == "__main__":
    main()
