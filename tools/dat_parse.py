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
    """Full MOT walk — spec turunkan dari kode game (FUN_001578xx memcmp 'mtb3' +
    sampler 0x1595f8) + invariant empiris di SELURUH 27.192 chunk:

    header  : u32 sig 'mtb3', u16 frame_total, u8 record_total, u8 loop
    record  : u8 target, u8 channel, u16 curve_total, [u32 use_global], u32 payload
              -> stride 12B (dengan use_global) ATAU 8B (tanpa) — stride otomatis
                 dipilih: stride 12B hanya diterima kalau tabel + seluruh stream
                 valid; 4 chunk em26.dat hanya valid dengan stride 8B.
    channel : enc = channel & 0xF0 ; component = channel & 0x0F
              0x00 = constant  (payload = float32 bits)
              0x10 = hermite   (payload = offset data; block = 12B header + 4*ct)
              0x30 = varian    (payload = offset data; block = 24B + 8*ct
                                = 2x ukuran block hermite)  [baru, 165 rec]
              0xFF = sentinel  (rec terakhir; payload diabaikan)
    stream  : block data membentuk stream KONTIGU — payload rec data pertama
              == table_end, tiap payload berikutnya == akhir block sebelumnya.
              (invariant ini terverifikasi 26.919/26.919 chunk mtb3)
    """
    n = end - start
    if n < 8:
        raise ValueError("mot<8")
    sig = struct.unpack_from("<I", chunk, start)[0]
    if sig != 0x3362746D:
        raise ValueError(f"mot sig {sig:#x}")
    frame_total, record_total, loop = struct.unpack_from("<HBB", chunk, start + 4)
    if record_total == 0:
        raise ValueError("record_total=0")
    last_err = None
    for stride in (12, 8):
        table_end = 8 + record_total * stride
        if table_end > n:
            last_err = "record table past end"
            continue
        recs = []
        p = start + 8
        for _ in range(record_total):
            if stride == 12:
                target, channel, ct, _ug, payload = struct.unpack_from("<BBHII", chunk, p)
            else:
                target, channel, ct, payload = struct.unpack_from("<BBHI", chunk, p)
            recs.append((target, channel, ct, payload))
            p += stride
        cur = table_end
        channels = collections.Counter()
        hermite = const = sentinel = curves = v30 = 0
        stream_ok = True
        for i, (target, channel, ct, payload) in enumerate(recs):
            channels[channel] += 1
            if channel == 0xFF:
                sentinel += 1
                continue
            enc = channel & 0xF0
            if enc == 0x00:
                const += 1
                continue
            if enc == 0x10:
                sz = 12 + 4 * ct
                hermite += 1
                curves += ct
            elif enc == 0x30:
                sz = 24 + 8 * ct          # == 2 x block hermite (terverifikasi 165 rec)
                v30 += 1
                curves += ct
            else:
                last_err = f"unsupported encoding 0x{enc:02X} ch={channel}"
                stream_ok = False
                break
            if payload != cur:            # stream kontigu = invariant utama
                last_err = f"stream gap rec{i} payload={payload} expect={cur}"
                stream_ok = False
                break
            cur += sz
            if cur > n:
                last_err = f"data past end rec{i}"
                stream_ok = False
                break
        if stream_ok:
            return dict(frames=frame_total, records=record_total, loop=loop, stride=stride,
                        hermite=hermite, constant=const, sentinel=sentinel, v30=v30,
                        curves=curves, data_end=cur, slack=n - cur, channels=channels)
    raise ValueError(last_err or "walk failed")


def parse_mq_walk(chunk, start, end):
    """Varian 'mq' (273 chunk, sig 'mq'+u16 ct) — layout turunkan empiris 273/273:

      header : u16 'mq', u16 curve_total(ct), u16 6919 (konstan lintas chunk = tag
               build alat), u8, u8(loop-ish)
      table  : 15 x record 8B {u8 target, u8 channel, u16 ct, u32 payload}
               (6 hermite + 1 sentinel + 8 constant pada sampel; channel pakai
               aturan yang sama dgn mtb3)
      entries: 26 x 64B {u8 idx(1..25), u8 0, u16 ct, u32 offs[4] (0 = null),
               u8 flags[4] (0xFF = channel absen), 10 x f32}
               entry terakhir = sentinel {idx=127, ct=0xFFFF}
      data   : stream block hermite kontigu mulai tepat setelah entry table;
               sisa ekor = blok channel terkompres (per target, ~ct byte/offset,
               BELUM didecode — dihitung sbg slack, bukan error)
    """
    n = end - start
    if n < 8:
        raise ValueError("mq<8")
    sig_lo, ct_hdr, tag, b6, b7 = struct.unpack_from("<5H", chunk, start)
    if (sig_lo & 0xFF) != 0x6D or (sig_lo >> 8) != 0x71:
        raise ValueError(f"mq sig {sig_lo:#x}")
    N = M = None
    tbl8 = 8 + 8 * 15
    # N=15 record 8B terverifikasi seragam; M diturunkan dari pola entry table
    recs = [struct.unpack_from("<BBHI", chunk, start + 8 + i * 8) for i in range(15)]
    dstart = None
    for tgt, ch, ct, pl in recs:
        if ch == 0xFF:
            continue
        if (ch & 0xF0) == 0x10:
            dstart = pl
            break
    if dstart is None or dstart < tbl8 or (dstart - tbl8) % 64:
        raise ValueError("mq: data start tidak setelah entry table")
    M = (dstart - tbl8) // 64
    if M < 2 or M > 200:
        raise ValueError(f"mq: M={M} di luar rentang")
    for k in range(M):
        idx, unk, ctt = struct.unpack_from("<BBH", chunk, start + tbl8 + k * 64)
        if k < M - 1:
            if idx != k + 1 or ctt != ct_hdr:
                raise ValueError(f"mq entry[{k}] pattern idx={idx} ct={ctt}")
        elif idx != 127 or ctt != 0xFFFF:
            raise ValueError("mq: entry sentinel hilang")
    cur = dstart
    hermite = const = sentinel = 0
    channels = collections.Counter()
    for i, (tgt, ch, ct, pl) in enumerate(recs):
        channels[ch] += 1
        if ch == 0xFF:
            sentinel += 1
            continue
        enc = ch & 0xF0
        if enc == 0x00:
            const += 1
            continue
        if enc != 0x10:
            raise ValueError(f"mq: encoding 0x{enc:02X}")
        if pl != cur:
            raise ValueError(f"mq: stream gap rec{i} pl={pl} expect={cur}")
        cur = pl + 12 + 4 * ct
        if cur > n:
            raise ValueError("mq: data past end")
        hermite += 1
    N = 15
    return dict(frames=tag, records=N, loop=b6, stride=8, hermite=hermite,
                constant=const, sentinel=sentinel, v30=0, curves=0,
                data_end=cur, slack=n - cur, channels=channels,
                mq=M, mq_tag=tag, mq_ct=ct_hdr, mq_b7=b7)


def cmd_mot():
    rows = load_inventory()
    ok = fail = 0
    errors = collections.Counter()
    ch_hist = collections.Counter()
    stats = []
    n_found = 0
    n_mtb3 = n_mq = 0
    stride_hist = collections.Counter()
    v30_total = 0
    slack_total = 0
    for name, data, s, e in find_chunks(rows, "MOT"):
        n_found += 1
        is_mq = data[s:s + 2] == b"mq"
        try:
            m = (parse_mq_walk if is_mq else parse_mot_walk)(data, s, e)
            ok += 1
            n_mq += is_mq
            n_mtb3 += (not is_mq)
            stride_hist[m["stride"]] += 1
            v30_total += m["v30"]
            slack_total += m["slack"]
            ch_hist.update(m["channels"])
            stats.append((name, s, e - s, "mq" if is_mq else "mtb3", m["stride"],
                          m["frames"], m["records"], m["loop"],
                          m["hermite"], m["constant"], m["sentinel"], m["v30"],
                          m["curves"], m["data_end"], m["slack"]))
        except ValueError as ex:
            fail += 1
            errors[str(ex)[:70]] += 1
    print(f"MOT chunks: {n_found}  full-walk OK: {ok}  FAIL: {fail}")
    print(f"  variant: mtb3={n_mtb3}  mq={n_mq} | stride 12B={stride_hist[12]} 8B={stride_hist[8]}")
    print(f"  encoding 0x30 records: {v30_total} | tail slack bytes total: {slack_total}")
    if errors:
        print("errors:", dict(errors))
    if stats:
        rt = [s[6] for s in stats]
        print(f"records/chunk: min={min(rt)} max={max(rt)} mean={sum(rt)/len(rt):.1f}")
        ft = [s[5] for s in stats if s[3] == "mtb3"]
        print(f"frames/chunk (mtb3): min={min(ft)} max={max(ft)} mean={sum(ft)/len(ft):.1f}")
        print("channel census (channel byte):", dict(sorted(ch_hist.items())))
        print("samples (file, off, bytes, var, stride, frames, records, loop, herm, const, sent, v30, curves, data_end, slack):")
        for s in stats[:6]:
            print("   ", s)
    out = os.path.join(OUT_DIR, "mot_headers.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "chunk_off", "chunk_bytes", "variant", "stride",
                    "frames", "records", "loop", "hermite", "constant",
                    "sentinel", "v30", "curves", "data_end", "slack"])
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
