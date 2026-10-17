#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d.6 — parser container "OTHER" (277 file custom
# yang BUKAN DAT/SES/SNR2): AFD (187) · T32 (8) · ev*.dat (73) · ATB (1) · BMT (1) ·
# debugoptimize (1) · sisanya diklasifikasi + header dicatat.
# Runtime: python3 stdlib. Spec: chihuahua pkg/t32 (hexpat + main.go/tex0.go) utk T32;
# AFD/ev/BMT/ATB diturunkan empiris dari ISO + divalidasi invariant ukuran.
# Usage:
#   other_parse.py            -> full walk semua OTHER, CSV + ringkasan
#   other_parse.py afd        -> AFD saja (detail)
import struct, sys, os, csv, collections

CUSTOM_DIR = "/root/ports/godhand/extract/afs/custom"
OUT_DIR = "/root/ports/godhand/extract"


def load_named():
    """idx -> (filename on disk, fst_name)."""
    import csv as _csv
    rows = list(_csv.DictReader(open(os.path.join(OUT_DIR, "afs_named_manifest.csv"))))
    return rows


def walk_afd(data):
    """AFD = {u32 total, u32 header_size(=16)} + total x record 32B.
    Invariant: size == 16 + 32*total. Field u32@4 = 16 di 187/187 file."""
    if len(data) < 16:
        raise ValueError("afd<16")
    total, hdr = struct.unpack_from("<II", data, 0)
    if hdr != 16:
        raise ValueError(f"afd hdr {hdr} != 16")
    need = 16 + 32 * total
    if need > len(data):
        raise ValueError(f"afd table past end ({need} > {len(data)})")
    recs = [struct.unpack_from("<6I2H", data, 16 + i * 32) for i in range(total)]
    # field "48000" (u16@+12 rec) = sample-rate-like, dicek konsistensi
    rate = collections.Counter(r[6] & 0xFFFF for r in recs)
    return dict(total=total, hdr=hdr, exact=(need == len(data)), size=len(data),
                table_end=need, slack=len(data) - need, rate=rate.most_common(3),
                unk=recs[0][5] if recs else 0)


def walk_t32(data):
    """T32 per chihuahua pkg/t32: header 32B + descriptor 16B (tex0 u64,tex1 u32,res u32)
    utk texture pertama; tiap texture berikutnya didahului TextureLink {u32 idx,u32 partot}.
    Invariant terukur di 8 file: img_off == 48, area palet konsisten 1248 B,
    packet 1/1, palette_total 1."""
    if len(data) < 48:
        raise ValueError("t32<48")
    (tex_total, w, h, img_off, pal_off, img_pkt, pal_pkt,
     res0, res1, pal_total) = struct.unpack_from("<IHHIIHHIII", data, 0)
    if tex_total == 0 or tex_total > 64:
        raise ValueError(f"t32 tex_total={tex_total}")
    if img_off != 32 + 16:
        raise ValueError(f"t32 img_off={img_off} != 48")
    if not (img_off < pal_off <= len(data)):
        raise ValueError(f"t32 pal_off={pal_off} size={len(data)}")
    p = 32
    tex = []
    for i in range(tex_total):
        if i:
            idx, pt = struct.unpack_from("<II", data, p)
            if idx != i:
                raise ValueError(f"t32 link[{i}].index={idx}")
            p += 8
        tex0, tex1, tres = struct.unpack_from("<QII", data, p)
        p += 16
        # GS TEX0 field decode (chihuahua pkg/t32/tex0.go DecodeGSTex0)
        psm = (tex0 >> 20) & 0x3F
        tw, th = (tex0 >> 26) & 0xF, (tex0 >> 30) & 0xF
        cpsm = (tex0 >> 51) & 0xF
        if psm not in (0x00, 0x01, 0x02, 0x0A, 0x13, 0x14):
            raise ValueError(f"t32 tex[{i}] PSM={psm:#x} tak dikenal")
        tex.append(dict(idx=i, psm=psm, w=1 << tw, h=1 << th, cpsm=cpsm, tex1=tex1))
    return dict(tex_total=tex_total, upload=(w, h), img_off=img_off, pal_off=pal_off,
                pkt=(img_pkt, pal_pkt), pal_total=pal_total, size=len(data),
                pal_area=len(data) - pal_off, img_area=pal_off - img_off, tex=tex)


def walk_atb(data):
    """seattrtbl.atb = {char[4] 'ATB\\0', f32 1.1, u16 x4, u32 hdr_size=32, u32 offs[3]}."""
    if data[:4] != b"ATB\x00":
        raise ValueError("atb magic")
    hdr, = struct.unpack_from("<I", data, 16)
    if hdr != 32:
        raise ValueError(f"atb hdr {hdr}")
    a, b, c = struct.unpack_from("<3I", data, 20)
    if not (a < b < c <= len(data)):
        raise ValueError(f"atb offs {a},{b},{c} size={len(data)}")
    return dict(hdr=hdr, offs=(a, b, c), size=len(data), unk4=struct.unpack_from("<4H", data, 8))


def walk_bmt(data):
    """bgmtbl.bmt = {u32 hdr_size=32, u32 rec_size=16, u32 off..} — validasi ukuran+off."""
    if len(data) < 32:
        raise ValueError("bmt<32")
    f0, f1 = struct.unpack_from("<II", data, 0)
    if f0 != 32:
        raise ValueError(f"bmt f0={f0}")
    offs = struct.unpack_from("<6I", data, 8)
    if any(o > len(data) for o in offs if o):
        raise ValueError("bmt off past end")
    return dict(hdr=f0, rec=f1, offs=offs, size=len(data))


def walk_ev(data):
    """ev*.dat = {u8 ver=1, u8 a, u8 b, u8 0, u32 offs[4], ...} — off naik & < size."""
    if len(data) < 20 or data[0] != 1 or data[3] != 0:
        raise ValueError("ev hdr")
    a, b = data[1], data[2]
    offs = struct.unpack_from("<4I", data, 4)
    if any(o >= len(data) for o in offs):
        raise ValueError(f"ev off past end {offs}")
    strict = all(offs[i] <= offs[i + 1] for i in range(3))
    if not strict:
        raise ValueError(f"ev off not sorted {offs}")
    return dict(ver=1, a=a, b=b, offs=offs, size=len(data), strict=strict)


def main():
    rows = load_named()
    byidx = {int(r["idx"]): r for r in rows}
    import dat_parse as dp
    inv = dp.load_inventory()
    oth = []
    for f, size, kind, detail in inv:
        if kind != "OTHER":
            continue
        idx = int(f.split("_")[1].split(".")[0])
        oth.append((f, byidx[idx]["fst_name"]))

    stats = collections.Counter()
    out_rows = []
    errors = collections.Counter()
    for f, name in oth:
        data = open(os.path.join(CUSTOM_DIR, f), "rb").read()
        ext = os.path.splitext(name)[1].lower()
        try:
            if ext == ".afd":
                r = walk_afd(data); kind = "AFD"
                stats["afd_ok"] += 1
                if not r["exact"]:
                    stats["afd_table_mismatch"] += 1
                    errors[f"afd slack {name}"] += 1
            elif ext == ".t32":
                r = walk_t32(data); kind = "T32"
                stats["t32_ok"] += 1
            elif ext == ".atb":
                r = walk_atb(data); kind = "ATB"; stats["atb_ok"] += 1
            elif ext == ".bmt":
                r = walk_bmt(data); kind = "BMT"; stats["bmt_ok"] += 1
            elif name == "debugoptimize":
                r = walk_afd(data); kind = "AFD-like"; stats["afdlike_ok"] += 1
            elif ext == ".dat" and name.lower().startswith("ev"):
                r = walk_ev(data); kind = "EV"; stats["ev_ok"] += 1
            else:
                r = {"head": data[:4].hex(), "size": len(data)}
                kind = "OTHER-UNCLASSIFIED"
                stats["unclassified"] += 1
                errors[f"unclassified {name}"] += 1
        except ValueError as ex:
            kind = "FAIL"
            r = {"err": str(ex)}
            stats["fail"] += 1
            errors[f"{name}: {ex}"] += 1
        out_rows.append(dict(file=f, fst_name=name, ext=ext or "<none>",
                             size=len(data), kind=kind,
                             detail=str(r)[:160]))

    out = os.path.join(OUT_DIR, "other_manifest.csv")
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["file", "fst_name", "ext", "size", "kind", "detail"])
        w.writeheader()
        w.writerows(out_rows)

    byext = collections.Counter(r["ext"] or "<none>" for r in out_rows)
    bykind = collections.Counter(r["kind"] for r in out_rows)
    print(f"OTHER files walked: {len(out_rows)}")
    print("by ext :", dict(byext.most_common()))
    print("by kind:", dict(bykind.most_common()))
    print("stats  :", dict(stats))
    if errors:
        print("errors :", dict(list(errors.items())[:12]))
    print(f"csv -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
