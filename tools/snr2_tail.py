#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d.6f-e — analisa TAIL SNR2 (region [table_end..EOF], 81 file)
# Runtime: python3 stdlib. Temuan utama (divalidasi lintas 81/81):
#   1. Tail = stream record: mayoritas pasangan {u8 tag, u8 operand}; ada record ALAMAT
#      {tag 0x01/0x02, u32 LE} — semua u32 tag{01,02} = alamat KODE modul (file offset,
#      code mulai @128): 4-aligned + dalam [128, 128+code_size), 0 outlier (1026/1026).
#   2. Framing solver: cari peta panjang-payload per tag (domain kecil, brute force)
#      yang bikin SEMUA 81 tail parse presisi-EOF + 0 alamat invalid.
#      Banyak peta lolos (underdetermined) -> dipilih dgn kriteria deterministik
#      (ok_file, n_alamat, map_key) — dicatat jujur di log.
#   3. Output: extract/snr2_tail_manifest.csv + stdout ringkas + md5 determinism.
# Usage:
#   snr2_tail.py all    -> brute force framing + manifest + stdout
#   snr2_tail.py dump <file> [n] -> parse tail file dgn peta terbaik + dump record
import csv, os, struct, itertools, collections, hashlib, json, sys

CUSTOM = "/root/ports/godhand/extract/afs/custom"
MAN = "/root/ports/godhand/extract/snr2_manifest.csv"
OUT = "/root/ports/godhand/extract"

DOMAIN = {                       # tag -> kandidat panjang payload (1 = pasangan default)
    0x01: (4, 5), 0x02: (4, 5), 0x00: (0, 4), 0x08: (0, 1),
    0x0a: (0, 1), 0x0e: (0, 1), 0x0c: (1, 3), 0x15: (1, 2), 0x1c: (1, 5),
}
KEYS = list(DOMAIN)
ADDRTAGS = (0x00, 0x01, 0x02)   # tag dgn payload>=4 berisi u32 alamat code


def load():
    out = []
    for r in csv.DictReader(open(MAN)):
        d = open(os.path.join(CUSTOM, r["file"]), "rb").read()
        out.append((r["file"], d[int(r["table_end"]):], int(r["code_size"])))
    return out


def parse_tail(t, cs, mp):
    """Return (exact_eof, bad_addr, list_addr). default payload = 1."""
    n = len(t); pos = 0; bad = 0; addrs = []
    while pos < n:
        tag = t[pos]
        L = mp.get(tag, 1)
        if pos + 1 + L > n:
            return False, bad, addrs
        if tag in ADDRTAGS and L >= 4:
            v = struct.unpack_from("<I", t, pos + 1)[0]
            addrs.append(v)
            if not (128 <= v < 128 + cs) or v % 4:
                bad += 1
        pos += 1 + L
    return pos == n, bad, addrs


def solve(files):
    """Brute force 512 kombinasi -> daftar peta dgn skor tertinggi (deterministik)."""
    results = []
    for combo in itertools.product(*[DOMAIN[k] for k in KEYS]):
        mp = dict(zip(KEYS, combo))
        ok = naddr = 0
        for fn, t, cs in files:
            e, b, a = parse_tail(t, cs, mp)
            if e and b == 0:
                ok += 1
                naddr += len(a)
        if ok == len(files):
            results.append((naddr, tuple(sorted(mp.items())), mp))
    results.sort(key=lambda x: (-x[0], x[1]))
    return results


def cmd_all():
    files = load()
    res = solve(files)
    print(f"SNR2 tail: {len(files)} file, "
          f"total {sum(len(t) for _, t, _ in files)} B")
    print(f"peta presisi-EOF+valid (81/81): {len(res)}/512 kombinasi domain")
    if not res:
        print("TIDAK ADA peta valid"); return 1
    naddr, key, best = res[0]
    print(f"peta terpilih (max alamat, deterministik): "
          f"{ {hex(k): v for k, v in sorted(best.items())} } default=1")
    print(f"total record alamat (tag 01/02): {naddr}")
    rows = []
    for fn, t, cs in files:
        e, b, a = parse_tail(t, cs, best)
        rows.append([fn, len(t), int(e and b == 0), len(a),
                     hex(a[0]) if a else "", hex(a[-1]) if a else ""])
    out = os.path.join(OUT, "snr2_tail_manifest.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "tail_len", "parse_ok", "n_addr", "first_addr", "last_addr"])
        w.writerows(rows)
    md5 = hashlib.md5(open(out, "rb").read()).hexdigest()
    n_ok = sum(1 for r in rows if r[2] == 1)
    print(f"manifest -> {out} ({len(rows)} baris, md5 {md5})")
    print(f"parse_ok {n_ok}/{len(rows)}; alamat per file min/med/max: "
          f"{min(r[3] for r in rows)} / {sorted(r[3] for r in rows)[len(rows)//2]}"
          f" / {max(r[3] for r in rows)}")
    # kandidat peta runner-up (jujur: model underdetermined)
    print(f"runner-up (5 teratas n_addr): "
          f"{[(n, dict((hex(k), v) for k, v in key)) for n, key, _ in res[:5]]}")
    json.dump({"best": {str(k): v for k, v in best.items()}, "n_maps_ok": len(res)},
              open(os.path.join(OUT, "_tail_map_best.json"), "w"), indent=1)
    return 0


def cmd_dump(path, n=40):
    rows = {r["file"]: r for r in csv.DictReader(open(MAN))}
    fn = os.path.basename(path)
    d = open(path, "rb").read()
    te = int(rows[fn]["table_end"]); cs = int(rows[fn]["code_size"])
    t = d[te:]
    best = json.load(open(os.path.join(OUT, "_tail_map_best.json")))["best"]
    best = {int(k): v for k, v in best.items()}
    print(f"{fn}: tail {len(t)} B @ file {te}..{len(d)}  code_size={cs}")
    pos = 0; cnt = 0
    while pos < len(t) and cnt < n:
        tag = t[pos]
        L = best.get(tag, 1)
        if pos + 1 + L > len(t):
            print(f"  @{pos}: tag={tag:#04x} TRUNC"); break
        if tag in ADDRTAGS and L >= 4:
            v = struct.unpack_from("<I", t, pos + 1)[0]
            print(f"  @{pos:4}: ADDR tag={tag:#04x} -> {v:#06x} (code-rel {v-128:#06x})")
        else:
            arg = t[pos + 1] if L else None
            print(f"  @{pos:4}: tag={tag:#04x} arg={arg if arg is None else hex(arg)}")
        pos += 1 + L; cnt += 1
    print(f"  ... total pos akhir {pos}/{len(t)}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode == "all":
        sys.exit(cmd_all())
    elif mode == "dump":
        cmd_dump(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 40)
    else:
        print(__doc__); sys.exit(2)


if __name__ == "__main__":
    main()
