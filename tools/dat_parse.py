#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6d — DAT container parser + custom-family classifier
# Runtime: python3 stdlib. Spec refs (vendored): refs/chihuahua (anasrar/chihuahua):
#   pkg/dat/dat.hexpat  -> struct Dat { u32 total; u32 offsets[total]; char types[total][4]; }
#   pkg/scr/main.go     -> SCR container inside DAT "MD" chunks (sig 0x00726373)
#   pkg/mdb/main.go     -> MDB model chunks (sig 0x0062646D), bones + vertex buffers
#   pkg/mot/main.go     -> MOT animation chunks (sig 0x3362746D / "mtb3")
# Usage:
#   dat_parse.py inventory  -> per-family inventory + DAT validation (csv + stdout)
#   dat_parse.py chunks     -> chunk magic census over all valid DAT files
#   dat_parse.py scr        -> SCR->MDB full walk: bones/vertex buffers, range invariants
#   dat_parse.py mot        -> MOT full walk: records + curve data ranges, channel census
import struct, sys, os, csv, collections

CUSTOM_DIR = "/root/ports/godhand/extract/afs/custom"
OUT_DIR = "/root/ports/godhand/extract"


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def parse_dat(data):
    """Return dict(total, offsets, types, header) or raise ValueError with reason.

    Semantics per chihuahua pkg/dat/main.go:
      - u32 total; u32 offsets[total] (rel to file start); char types[total][4]
      - offset == 0 -> NULL entry (type may be garbage; skipped by engine)
      - chunk size = next NON-NULL offset - this offset; last non-null -> EOF
      - header padded to 16B in game files (offsets[0] >= align16(4+8*total) typical)
    """
    if len(data) < 4:
        raise ValueError("size<4")
    total = struct.unpack_from("<I", data, 0)[0]
    if total == 0 or total > 65536:
        raise ValueError(f"total={total} out of range")
    hdr = 4 + 8 * total          # u32 total + offsets[total] + types[total]
    if hdr > len(data):
        raise ValueError(f"header {hdr} > size {len(data)}")
    offsets = list(struct.unpack_from(f"<{total}I", data, 4))
    types = []
    base = 4 + 4 * total
    for i in range(total):
        t = data[base + i * 4: base + i * 4 + 4]
        core = t.rstrip(b"\x00")
        if offsets[i] == 0:
            types.append("NULL")     # null entry: engine ignores type bytes
            continue
        if len(core) < 2 or not all(0x20 <= b < 0x7F for b in core):
            raise ValueError(f"type[{i}] non-printable {t!r}")
        types.append(core.decode("ascii"))
    nonzero = [o for o in offsets if o != 0]
    if nonzero != sorted(nonzero):
        raise ValueError("non-null offsets not monotonic")
    if not nonzero:
        raise ValueError("no non-null offsets")
    if nonzero[0] < hdr:
        raise ValueError(f"first offset {nonzero[0]} < header {hdr}")
    if nonzero[-1] > len(data):
        raise ValueError(f"last offset {nonzero[-1]} > size {len(data)}")
    return {"total": total, "offsets": offsets, "types": types, "header": hdr}


def iter_chunks(data, dat):
    """Yield (idx, magic, start, end) for non-null entries."""
    offs, types = dat["offsets"], dat["types"]
    for i, (o, t) in enumerate(zip(offs, types)):
        if o == 0:
            continue
        end = len(data)
        for j in range(i + 1, len(offs)):
            if offs[j] != 0:
                end = offs[j]
                break
        yield i, t, o, end


def load_inventory():
    rows = []
    for name in sorted(os.listdir(CUSTOM_DIR)):
        path = os.path.join(CUSTOM_DIR, name)
        data = read_file(path)
        size = len(data)
        if size >= 4 and data[:4] == b"SES\x00":
            rows.append((name, size, "SES", "audio-pack"))
            continue
        if size >= 4 and data[:4] == b"SNR2":
            rows.append((name, size, "SNR2", "unknown"))
            continue
        try:
            dat = parse_dat(data)
            rows.append((name, size, "DAT", f"total={dat['total']}"))
        except ValueError as e:
            rows.append((name, size, "OTHER", str(e)))
    return rows


def find_chunks(rows, magic):
    """Yield (file, data, start, end) for every DAT chunk with given type magic."""
    for name, size, kind, _ in rows:
        if kind != "DAT":
            continue
        data = read_file(os.path.join(CUSTOM_DIR, name))
        dat = parse_dat(data)
        for i, t, s, e in iter_chunks(data, dat):
            if t == magic:
                yield name, data, s, e


def cmd_inventory():
    rows = load_inventory()
    kind_ct = collections.Counter(r[2] for r in rows)
    ok_path = os.path.join(OUT_DIR, "dat_inventory.csv")
    with open(ok_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "size", "kind", "detail"])
        w.writerows(rows)
    print(f"total custom files scanned : {len(rows)}")
    for k, v in kind_ct.most_common():
        print(f"  {k:6} {v}")
    reasons = collections.Counter(r[3].split("=")[0].split(" ")[0] for r in rows if r[2] == "OTHER")
    print("OTHER reasons:", dict(reasons))
    tot = collections.Counter(int(r[3].split("=")[1]) for r in rows if r[2] == "DAT")
    print("DAT total(chunks) top:", tot.most_common(10))
    print(f"csv -> {ok_path}")
    return rows


def cmd_chunks():
    rows = load_inventory()
    census = collections.Counter()
    chunk_bytes = collections.Counter()
    files_per_magic = collections.defaultdict(set)
    tail_short = 0
    n_dat = 0
    for name, size, kind, _ in rows:
        if kind != "DAT":
            continue
        n_dat += 1
        data = read_file(os.path.join(CUSTOM_DIR, name))
        dat = parse_dat(data)
        for i, t, s, e in iter_chunks(data, dat):
            census[t] += 1
            chunk_bytes[t] += (e - s)
            files_per_magic[t].add(name)
            if e > size:
                tail_short += 1
    print(f"DAT files parsed: {n_dat}")
    print(f"{'magic':10} {'chunks':>8} {'files':>6} {'bytes':>14}")
    for t, c in census.most_common():
        print(f"{t:10} {c:8} {len(files_per_magic[t]):6} {chunk_bytes[t]:14}")
    print(f"chunks past EOF: {tail_short}")
    out = os.path.join(OUT_DIR, "dat_chunk_census.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["magic", "chunks", "files", "bytes"])
        for t, c in census.most_common():
            w.writerow([t, c, len(files_per_magic[t]), chunk_bytes[t]])
    print(f"csv -> {out}")
    return census


# ---------------------------------------------------------------- SCR -> MDB

def parse_scr_mdb_walk(chunk, start, end):
    """Walk SCR container (DAT type 'MD') -> nodes -> MDB models (chihuahua pkg/scr+pkg/mdb).

    Returns dict(nodes, mdbs, bones, vbs, verts, room_vbs).
    Raises ValueError on any invariant violation.
    """
    n = end - start
    if n < 16:
        raise ValueError("scr<16")
    sig = struct.unpack_from("<I", chunk, start)[0]
    if sig != 0x00726373:
        raise ValueError(f"scr sig {sig:#x}")
    node_total = struct.unpack_from("<I", chunk, start + 8)[0]
    if node_total == 0 or node_total > 4096:
        raise ValueError(f"node_total={node_total}")
    hdr = 16 + 4 * node_total
    if hdr > n:
        raise ValueError("node table past end")
    node_offs = struct.unpack_from(f"<{node_total}I", chunk, start + 16)
    if list(node_offs) != sorted(node_offs):
        raise ValueError("node offsets not monotonic")

    bones_total = 0
    vb_total = 0
    verts_total = 0
    room_vb = 0
    mdb_seen = set()
    names = []
    for k, no in enumerate(node_offs):
        if no + 112 > n:
            raise ValueError(f"node[{k}] off {no:#x} outside scr chunk")
        np = start + no
        mdb_rel = struct.unpack_from("<i", chunk, np)[0]
        name = chunk[np + 8: np + 16]
        if not all(b == 0 or 0x20 <= b < 0x7F for b in name):
            raise ValueError(f"node[{k}] name non-printable {name!r}")
        names.append(name.split(b"\x00")[0].decode("ascii", "replace"))
        vals = struct.unpack_from("<9f", chunk, np + 16)
        for v in vals:
            if v != v or abs(v) > 1e7:
                raise ValueError(f"node[{k}] transform not finite")
        mp = np + mdb_rel
        if mp < start or mp >= end:
            raise ValueError(f"node[{k}] mdb target {mdb_rel:+#x} outside scr chunk")
        if mp in mdb_seen:
            continue
        mdb_seen.add(mp)
        if mp + 32 > end:
            raise ValueError("mdb header past end")
        msig = struct.unpack_from("<I", chunk, mp)[0]
        if msig != 0x0062646D:
            raise ValueError(f"mdb sig {msig:#x} at {mp - start:#x}")
        bone_off, bone_total, vb_total_i = struct.unpack_from("<IHH", chunk, mp + 4)
        flag = struct.unpack_from("<H", chunk, mp + 4 + 4 + 4 + 18)[0]
        if bone_total > 4096:
            raise ValueError(f"bone_total={bone_total}")
        bone_end = bone_off + bone_total * 16          # Bone: 3xf32 + 2 pad + s16
        if bone_total and bone_end > end - mp:
            raise ValueError(f"bones past end ({bone_end:#x} > {end - mp:#x})")
        hdr_mdb = 32 + 4 * vb_total_i
        if hdr_mdb > end - mp:
            raise ValueError("mdb vb table past end")
        vb_offs = struct.unpack_from(f"<{vb_total_i}I", chunk, mp + 32)
        for vbo in vb_offs:
            dp = mp + vbo
            if dp + 24 > end:
                raise ValueError("DataAddress past end")
            pos_o, nrm_o, uv_o, col_o, wgt_o, vtx, mat = struct.unpack_from("<5IHH", chunk, dp)
            stride = 8 if flag == 0 else 16            # room: 3xs16+u16 ; else 3xf32+i32
            need = pos_o + vtx * stride
            if need > end - dp:
                raise ValueError(f"positions past end (+{need:#x})")
            for o, rec in ((nrm_o, 4), (uv_o, 4), (col_o, 4), (wgt_o, 8)):
                if o and o + vtx * rec > end - dp:
                    raise ValueError(f"attr past end (o={o:#x} vtx={vtx})")
            # per-vertex strip flags must be in {0,1,32768} (chihuahua pkg/mdb main.go)
            for vi in range(vtx):
                if flag == 0:
                    f16 = struct.unpack_from("<H", chunk, dp + pos_o + vi * 8 + 6)[0]
                else:
                    f16 = struct.unpack_from("<I", chunk, dp + pos_o + vi * 16 + 12)[0]
                if f16 not in (0, 1, 32768):
                    raise ValueError(f"vertex[{vi}] flag {f16} not in {{0,1,32768}}")
            verts_total += vtx
            vb_total += 1
            if flag == 0:
                room_vb += 1
        bones_total += bone_total
    return dict(nodes=node_total, mdbs=len(mdb_seen), bones=bones_total,
                vbs=vb_total, verts=verts_total, room_vbs=room_vb,
                node_names="|".join(names[:6]))


def cmd_scr():
    rows = load_inventory()
    ok = fail = 0
    errors = collections.Counter()
    stats = []
    n_md = 0
    for name, data, s, e in find_chunks(rows, "MD"):
        n_md += 1
        try:
            r = parse_scr_mdb_walk(data, s, e)
            ok += 1
            stats.append((name, e - s, r["nodes"], r["mdbs"], r["bones"],
                          r["vbs"], r["verts"], r["room_vbs"]))
        except ValueError as ex:
            fail += 1
            errors[str(ex)[:70]] += 1
    print(f"MD(SCR) chunks: {n_md}  full-walk OK: {ok}  FAIL: {fail}")
    if errors:
        print("errors:", dict(errors))
    if stats:
        tot = lambda i: sum(s[i] for s in stats)
        print(f"totals: nodes={tot(2)} mdbs={tot(3)} bones={tot(4)} "
              f"vertex_buffers={tot(5)} vertices={tot(6)} room_vbs={tot(7)}")
        print("samples (file, chunk_bytes, nodes, mdbs, bones, vbs, verts, room):")
        for s in stats[:6]:
            print("   ", s)
    out = os.path.join(OUT_DIR, "scr_mdb_headers.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "chunk_bytes", "nodes", "mdbs", "bones", "vbs", "verts", "room_vbs"])
        w.writerows(stats)
    print(f"csv -> {out}")
    return ok, fail


# ---------------------------------------------------------------- MOT

def parse_mot_walk(chunk, start, end):
    """Full MOT walk per chihuahua pkg/mot (sig 'mtb3', records x12B, curve ranges)."""
    n = end - start
    if n < 8:
        raise ValueError("mot<8")
    sig = struct.unpack_from("<I", chunk, start)[0]
    if sig != 0x3362746D:
        raise ValueError(f"mot sig {sig:#x}")
    frame_total, record_total, loop = struct.unpack_from("<HBB", chunk, start + 4)
    if record_total == 0:
        raise ValueError("record_total=0")
    table_end = 8 + record_total * 12
    if table_end > n:
        raise ValueError("record table past end")
    channels = []
    curves_sum = 0
    hermite = const = sentinel = 0
    p = start + 8
    data_offsets = []
    for r in range(record_total):
        target, channel, curve_total, use_global, payload = struct.unpack_from("<BBHII", chunk, p)
        p += 12
        enc = channel & 0xF0
        channels.append(channel)
        if channel == 0xFF:
            sentinel += 1
            data_offsets.append(None)
            continue
        if enc == 0x00:
            const += 1
            data_offsets.append(None)
            continue
        if enc == 0x10:
            hermite += 1
            data_offsets.append((payload, curve_total))
            curves_sum += curve_total
            continue
        raise ValueError(f"unsupported encoding 0x{enc:02X} ch={channel}")
    for item in data_offsets:
        if item is None:
            continue
        off, ct = item
        if off is None:
            continue
        dsz = 12 + ct * 4
        if off < table_end or off + dsz > n:
            raise ValueError(f"data range {off:#x}+{dsz:#x} outside mot size {n:#x}")
        struct.unpack_from("<6H", chunk, start + off)
    return dict(frames=frame_total, records=record_total, loop=loop,
                hermite=hermite, constant=const, sentinel=sentinel,
                curves=curves_sum, channels=collections.Counter(channels))


def cmd_mot():
    rows = load_inventory()
    ok = fail = 0
    errors = collections.Counter()
    ch_hist = collections.Counter()
    stats = []
    n_found = 0
    for name, data, s, e in find_chunks(rows, "MOT"):
        n_found += 1
        try:
            m = parse_mot_walk(data, s, e)
            ok += 1
            ch_hist.update(m["channels"])
            stats.append((name, e - s, m["frames"], m["records"], m["loop"],
                          m["hermite"], m["constant"], m["sentinel"], m["curves"]))
        except ValueError as ex:
            fail += 1
            errors[str(ex)[:70]] += 1
    print(f"MOT chunks: {n_found}  full-walk OK: {ok}  FAIL: {fail}")
    if errors:
        print("errors:", dict(errors))
    if stats:
        rt = [s[3] for s in stats]
        print(f"records/chunk: min={min(rt)} max={max(rt)} mean={sum(rt)/len(rt):.1f}")
        ft = [s[2] for s in stats]
        print(f"frames/chunk : min={min(ft)} max={max(ft)} mean={sum(ft)/len(ft):.1f}")
        print("channel census (enc<<4|component):", dict(sorted(ch_hist.items())))
        print("samples (file, bytes, frames, records, loop, hermite, const, sentinel, curves):")
        for s in stats[:6]:
            print("   ", s)
    out = os.path.join(OUT_DIR, "mot_headers.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "chunk_bytes", "frames", "records", "loop",
                    "hermite", "constant", "sentinel", "curves"])
        w.writerows(stats)
    print(f"csv -> {out}")
    return ok, fail


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "inventory"
    if mode == "inventory":
        cmd_inventory()
    elif mode == "chunks":
        cmd_chunks()
    elif mode == "scr":
        cmd_scr()
    elif mode == "mot":
        cmd_mot()
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
