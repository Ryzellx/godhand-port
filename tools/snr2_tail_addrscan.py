#!/usr/bin/env python3
# Probe tail SNR2 tahap 7 — u32 dari record {01|02} di tail: test membership
# terhadap reloc loc, sym value, alamat fungsi (4-aligned), dsb.
import csv, os, struct, collections

CUSTOM = "/root/ports/godhand/extract/afs/custom"
MAN = "/root/ports/godhand/extract/snr2_manifest.csv"

def load():
    out = []
    for r in csv.DictReader(open(MAN)):
        d = open(os.path.join(CUSTOM, r["file"]), "rb").read()
        m = {"file": r["file"], "data": d, "tail": d[int(r["table_end"]):]}
        for k in ("table_end","size","code_size","n_sym","n_reloc","reloc_off","sym_off"):
            m[k] = int(r[k])
        out.append(m)
    return out

def main():
    files = load()
    agg = collections.Counter()
    samples = []
    align_ok = 0; align_bad = 0
    in_code = 0; out_code = 0
    reloc_hit = 0; sym_hit = 0; total = 0
    range_lo, range_hi = 10**9, -1
    for f in files:
        d, t = f["data"], f["tail"]
        reloc_locs = set()
        for k in range(f["n_reloc"]):
            loc = struct.unpack_from("<I", d, f["reloc_off"] + k*12)[0]
            reloc_locs.add(loc)
        symvals = set()
        for k in range(f["n_sym"]):
            _n, v, _i = struct.unpack_from("<III", d, f["sym_off"] + k*12)
            if v: symvals.add(v)
        # kandidat: pos dgn byte {1,2} lalu u32 masuk akal
        i = 0
        while i < len(t) - 4:
            if t[i] in (1, 2):
                v = struct.unpack_from("<I", t, i+1)[0]
                if 64 <= v <= 0x40000:
                    total += 1
                    if v % 4 == 0: align_ok += 1
                    else: align_bad += 1
                    if 128 <= v < 128 + f["code_size"]: in_code += 1
                    else: out_code += 1
                    if v in reloc_locs: reloc_hit += 1
                    if v in symvals: sym_hit += 1
                    range_lo = min(range_lo, v); range_hi = max(range_hi, v)
                    if len(samples) < 12: samples.append((f["file"], i, v))
                    i += 5; continue
            i += 1
    print(f"kandidat u32 tag 1/2: total={total}")
    print(f"  4-aligned: {align_ok}  bukan: {align_bad}")
    print(f"  dalam code region [128,128+code): {in_code}  di luar: {out_code}")
    print(f"  == reloc loc: {reloc_hit}   == sym value: {sym_hit}")
    print(f"  rentang nilai: {range_lo}..{range_hi}")
    print("  samples:", samples)

    # Test lanjut: apakah nilai u32 = offset dalam region CODE file-offset ATAU code-relative?
    # r000: bandingkan dgn isi .text module — baca instruksi di v (file) dan v-128 (rel)
    f = [x for x in files if x["file"] == "entry_1072.bin"][0]
    print("\nr000 (entry_1072): u32 kandidat + 16 byte di posisi tsb (interpretasi file-offset):")
    d, t = f["data"], f["tail"]
    i = 0; shown = 0
    while i < len(t) - 4 and shown < 10:
        if t[i] in (1, 2):
            v = struct.unpack_from("<I", t, i+1)[0]
            if 64 <= v <= 0x40000:
                a = d[v:v+16]
                b = d[128+v:128+v+16] if 128+v+16 <= len(d) else b""
                print(f"  @{i} tag={t[i]} v={v:#x}  @file: {a.hex(' ')}   @file+128: {b.hex(' ')}")
                shown += 1
                i += 5; continue
        i += 1

if __name__ == "__main__":
    main()
