#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d.6f-d — SNR2 container parser (r*.rel / event.rel)
# Runtime: python3 stdlib. 81 file custom dgn magic 'SNR2' == berkas rel/room module
# (modul relocatable EE: code + strtab + reloc table + sym table).
# Struktur header DITURUNKAN empiris dari entry_1079 (r002.rel, 3201 B) + invarian
# lintas sampel, divalidasi penuh di 81/81 file oleh mode `all`.
#
# HEADER (u32 LE, region 0..128; field @40 = 128 = offset data pertama):
#   @0  u32 magic 'SNR2' (0x32524E53)
#   @4  u32 reloc_off   (= end strtab; tabel reloc mulai di sini)
#   @8  u32 n_reloc     (tabel reloc: n_reloc x 12B)
#   @12 u32 sym_off     (= @4 + 12*@8; tabel simbol mulai di sini)
#   @16 u32 n_sym       (tabel simbol: n_sym x 12B; @32 == @12 + 12*@16)
#   @20 u32 path_off    (offset string terakhir = path sumber '..\PS2_EE_Release\*.elf';
#                        @4 == @20 + strlen(path) + 1 terverifikasi)
#   @24 u32 ctor_addr   (alamat __do_global_ctors — cocok dgn field addr simbol tsb)
#   @28 u32 dtor_addr   (alamat __do_global_dtors; @24 - @28 == 88 konstan)
#   @32 u32 table_end   (== @52 == @56)
#   @36 0   @40 = 128   @44 = ukuran file   @48 = 0   @60 = 0   (sisa nol)
# LAYOUT FILE: [0..128 hdr][128..strtab_start code][strtab .. @4][reloc @4..@12]
#              [sym @12..@32][tail @32..EOF]
# RELOC 12B: {u32 loc (offset dlm code), u8 type, u8 sym_idx, u16 pad, u32 addend}
#            type empiris {4,5,6}; sym_idx < n_sym.
# SYM 12B   : {u32 name_off (strtab), u32 value (0 = undef/import), u32 id}
#            id = identifier STABIL lintas file utk nama sama (hash global/link ID;
#            algoritma belum terpetak — 12+ keluarga hash klasik dicoba, 0 match).
# Usage:
#   snr2_parse.py all      -> full walk semua 81 file + invariant census + csv + stdout
#   snr2_parse.py dump <file> [n] -> header grid + dump reloc/sym pertama n entri
import struct, sys, os, csv, collections, hashlib

CUSTOM_DIR = "/root/ports/godhand/extract/afs/custom"
OUT_DIR = "/root/ports/godhand/extract"
MAGIC = 0x32524E53        # u32 LE dari ascii 'SNR2'


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def cstr(data, off, limit):
    if off >= limit:
        raise ValueError(f"name_off {off} >= strtab_end {limit}")
    end = data.find(b"\x00", off, limit)
    if end < 0:
        raise ValueError(f"name_off {off} tak ada NUL sebelum {limit}")
    raw = data[off:end]
    if not all(b == 0 or 0x20 <= b < 0x7F for b in raw):
        raise ValueError(f"name @{off} non-printable {raw[:16]!r}")
    return raw.decode("ascii", "replace")


def walk_snr2(data, strict=True):
    """Full walk satu file SNR2. Return dict statistik; raise ValueError saat invariant patah."""
    n = len(data)
    if n < 128:
        raise ValueError("size<128")
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != MAGIC:
        raise ValueError(f"magic {magic:#x}")
    (reloc_off, n_reloc, sym_off, n_sym, path_off, ctor, dtor,
     table_end, z1, data_off, fsize, z2, t52, t56, z3, z4) = struct.unpack_from("<16I", data, 4)
    # invariant header wajib
    if data_off != 128:
        raise ValueError(f"@40 data_off {data_off} != 128")
    if fsize != n:
        raise ValueError(f"@44 fsize {fsize} != actual {n}")
    if z1 or z2 or z3 or z4:
        raise ValueError(f"field nol tercemar: @36={z1} @48={z2} @60={z3} @64={z4}")
    if not (t52 == table_end and t56 == table_end):
        raise ValueError(f"@32/@52/@56 tak sama: {table_end} {t52} {t56}")
    if reloc_off + 12 * n_reloc != sym_off:
        raise ValueError(f"reloc table: {reloc_off}+12*{n_reloc} != sym_off {sym_off}")
    if sym_off + 12 * n_sym != table_end:
        raise ValueError(f"sym table: {sym_off}+12*{n_sym} != table_end {table_end}")
    if table_end > n:
        raise ValueError("table_end > size")
    # path string terakhir harus berakhir tepat di reloc_off
    if not (0 < path_off < reloc_off):
        raise ValueError(f"path_off {path_off} di luar (0,{reloc_off})")
    pend = data.find(b"\x00", path_off, reloc_off)
    if pend < 0:
        raise ValueError("path tanpa NUL sebelum reloc_off")
    # padding strtab: sisa setelah NUL path s/d reloc_off = 0..3 byte NOL (align-4)
    pad = data[pend + 1:reloc_off]
    if len(pad) > 3 or any(pad):
        raise ValueError(f"padding strtab tak valid: {len(pad)} B {bytes(pad[:8])!r} "
                         f"(end={pend + 1} vs {reloc_off})")
    if reloc_off & 3:
        raise ValueError(f"reloc_off {reloc_off} tak align-4")
    path = data[path_off:pend].decode("ascii", "replace")
    if not all(0x20 <= b < 0x7F for b in data[path_off:pend]):
        raise ValueError("path non-printable")
    # strtab: semua simbol harus resolve & printable
    names = []
    for k in range(n_sym):
        name_off, value, sid = struct.unpack_from("<III", data, sym_off + k * 12)
        names.append(cstr(data, name_off, reloc_off))
    # relocs
    type_hist = collections.Counter()
    sym_idx_hist_ok = 0
    undef_syms = 0
    defined_syms = 0
    for k in range(n_reloc):
        loc, rest = struct.unpack_from("<II", data, reloc_off + k * 12)[:2]
        typ = rest & 0xFF
        sidx = (rest >> 8) & 0xFF
        _pad = (rest >> 16) & 0xFFFF
        if loc >= reloc_off:
            raise ValueError(f"reloc[{k}] loc {loc:#x} >= reloc_off")
        if sidx >= n_sym:
            raise ValueError(f"reloc[{k}] sym {sidx} >= n_sym {n_sym}")
        type_hist[typ] += 1
        sym_idx_hist_ok += 1
    # sym values
    ctor_hit = dtor_hit = 0
    id_by_name = {}
    min_name_off = reloc_off
    for k in range(n_sym):
        name_off, value, sid = struct.unpack_from("<III", data, sym_off + k * 12)
        min_name_off = min(min_name_off, name_off)
        if value == 0:
            undef_syms += 1
        else:
            if value < 128 or value >= table_end:
                raise ValueError(f"sym[{k}]({names[k]}) value {value} di luar code/table")
            defined_syms += 1
        if value == ctor and "ctor" in names[k]:
            ctor_hit += 1
        if value == dtor and "dtor" in names[k]:
            dtor_hit += 1
        if names[k] in id_by_name and id_by_name[names[k]] != sid:
            raise ValueError(f"id tak stabil utk {names[k]!r}: {id_by_name[names[k]]} != {sid}")
        id_by_name[names[k]] = sid
    # strtab_start = min(name_off semua simbol) — definisi eksak (region nama simbol);
    # rodata inline (mis. 'bad_alloc') bisa duduk di bawahnya dan ikut terhitung sbg code+rodata
    ss = min_name_off
    return dict(
        size=n, n_reloc=n_reloc, n_sym=n_sym, reloc_off=reloc_off, sym_off=sym_off,
        table_end=table_end, tail=n - table_end, ctor=ctor, dtor=dtor, dtor_gap=ctor - dtor,
        path=path, strtab_start=ss, code_size=ss - 128, strtab_size=reloc_off - ss,
        undef=undef_syms, defined=defined_syms, ctor_hit=ctor_hit, dtor_hit=dtor_hit,
        types=dict(sorted(type_hist.items())), id_by_name=id_by_name,
        reloc_ok=sym_idx_hist_ok,
    )


def load_files():
    rows = []
    with open(os.path.join(OUT_DIR, "dat_inventory.csv"), newline="") as f:
        for r in csv.DictReader(f):
            if r["kind"] == "SNR2":
                rows.append(r["file"])
    return rows


def cmd_all():
    files = load_files()
    ok = fail = 0
    errors = collections.Counter()
    stats = []
    agg = collections.Counter()
    global_ids = {}
    id_conflicts = 0
    ctor_gap_hist = collections.Counter()
    type_hist = collections.Counter()
    for fn in files:
        data = read_file(os.path.join(CUSTOM_DIR, fn))
        try:
            w = walk_snr2(data)
            ok += 1
            ids = w.pop("id_by_name")
            w["file"] = fn
            stats.append(w)
            for name, sid in ids.items():
                if name in global_ids and global_ids[name] != sid:
                    id_conflicts += 1
                global_ids[name] = sid
            agg["n_reloc"] += w["n_reloc"]
            agg["n_sym"] += w["n_sym"]
            agg["undef"] += w["undef"]
            agg["defined"] += w["defined"]
            agg["tail"] += w["tail"]
            agg["code_size"] += w["code_size"]
            ctor_gap_hist[w["dtor_gap"]] += 1
            for t, c in w["types"].items():
                type_hist[t] += c
            if not (w["ctor_hit"] and w["dtor_hit"]):
                errors[f"ctor/dtor symbol tak match (file {fn})"] += 1
        except ValueError as e:
            fail += 1
            errors[str(e)[:80]] += 1
            continue
    print(f"SNR2 files: {len(files)}  walk OK: {ok}  FAIL: {fail}")
    if errors:
        print("errors:", dict(errors))
    print(f"totals: reloc={agg['n_reloc']} sym={agg['n_sym']} (undef={agg['undef']} "
          f"defined={agg['defined']}) tail_bytes={agg['tail']} code_bytes={agg['code_size']}")
    print(f"reloc type census: {dict(sorted(type_hist.items()))}")
    print(f"ctor-dtor gap histogram: {dict(ctor_gap_hist)}")
    print(f"global name->id: {len(global_ids)} unique names, conflicts={id_conflicts}")
    if stats:
        sizes = sorted(s["size"] for s in stats)
        print(f"size min/median/max: {sizes[0]} / {sizes[len(sizes)//2]} / {sizes[-1]}")
        print("samples (file, size, n_reloc, n_sym, undef, defined, tail, path):")
        for s in stats[:6]:
            print("   ", (s["file"], s["size"], s["n_reloc"], s["n_sym"], s["undef"],
                          s["defined"], s["tail"], s["path"]))
    out = os.path.join(OUT_DIR, "snr2_manifest.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "size", "n_reloc", "n_sym", "undef", "defined",
                    "reloc_off", "sym_off", "table_end", "tail", "code_size",
                    "strtab_size", "ctor", "dtor", "dtor_gap", "path"])
        for s in stats:
            w.writerow([s[k] for k in ("file", "size", "n_reloc", "n_sym", "undef", "defined",
                                       "reloc_off", "sym_off", "table_end", "tail", "code_size",
                                       "strtab_size", "ctor", "dtor", "dtor_gap", "path")])
    md5 = hashlib.md5(open(out, "rb").read()).hexdigest()
    print(f"csv -> {out} (md5 {md5})")
    return ok, fail


def cmd_dump(path, n=10):
    data = read_file(path)
    w = walk_snr2(data)
    print(f"{path} size={w['size']}")
    for off in range(0, 128, 16):
        words = " ".join(f"{struct.unpack_from('<I', data, off + i)[0]:>10}" for i in range(0, 16, 4))
        raw = "".join(chr(b) if 32 <= b < 127 else "." for b in data[off:off + 16])
        print(f"  {off:04x}: {words}  |{raw}|")
    print(f"layout: code[128..{w['strtab_start']}) strtab[{w['strtab_start']}..{w['reloc_off']}) "
          f"reloc[{w['reloc_off']}..{w['sym_off']}) sym[{w['sym_off']}..{w['table_end']}) "
          f"tail[{w['table_end']}..{w['size']}]")
    print(f"path: {w['path']}  ctor={w['ctor']} dtor={w['dtor']} gap={w['dtor_gap']}")
    print(f"reloc type census: {w['types']}")
    print(f"first {n} relocs:")
    for k in range(min(n, w["n_reloc"])):
        loc, rest, add = struct.unpack_from("<III", data, w["reloc_off"] + k * 12)
        print(f"  [{k:3}] loc={loc:6} type={rest & 0xFF} sym={(rest >> 8) & 0xFF} pad={(rest >> 16) & 0xFFFF} addend={add}")
    print(f"first {n} syms:")
    for k in range(min(n, w["n_sym"])):
        no, val, sid = struct.unpack_from("<III", data, w["sym_off"] + k * 12)
        end = data.find(b"\x00", no, w["reloc_off"])
        print(f"  [{k:3}] name={data[no:end].decode('ascii','replace')!r} value={val} id={sid}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode == "all":
        cmd_all()
    elif mode == "dump":
        if len(sys.argv) < 3:
            print(__doc__)
            sys.exit(2)
        cmd_dump(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 10)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
