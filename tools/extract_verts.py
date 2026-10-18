#!/usr/bin/env python3
# Extract real God Hand model vertex positions (pl00 = Gene) -> raw float binary
# Format output: u32 vert_count, then vert_count * 3 * f32 (x,y,z)
import struct, sys, os

DAT_PATH = "/root/ports/godhand/extract/afs/custom/entry_1053.bin"
OUT_PATH = "/root/projects/godhand-port/out/pl00_verts.bin"

def read_file(path):
    with open(path, "rb") as f:
        return f.read()

def parse_dat(data):
    total = struct.unpack_from("<I", data, 0)[0]
    offsets = list(struct.unpack_from(f"<{total}I", data, 4))
    types = []
    base = 4 + 4 * total
    for i in range(total):
        t = data[base + i*4: base + i*4 + 4]
        core = t.rstrip(b"\x00")
        if offsets[i] == 0:
            types.append("NULL"); continue
        if len(core) < 2 or not all(0x20 <= b < 0x7F for b in core):
            types.append("UNKNOWN"); continue
        types.append(core.decode("ascii"))
    return dict(total=total, offsets=offsets, types=types)

def iter_chunks(data, dat):
    for i in range(dat["total"]):
        off = dat["offsets"][i]
        if off == 0: continue
        nxt = None
        for j in range(i+1, dat["total"]):
            if dat["offsets"][j] != 0:
                nxt = dat["offsets"][j]; break
        end = nxt if nxt else len(data)
        yield dat["types"][i], off, end

def parse_mdb_verts(chunk, mp, end):
    """Extract vertex positions from MDB at mp. Returns list of (x,y,z) floats."""
    if mp + 32 > end: return []
    msig = struct.unpack_from("<I", chunk, mp)[0]
    if msig != 0x0062646D: return []
    bone_off, bone_total, vb_total_i = struct.unpack_from("<IHH", chunk, mp + 4)
    flag = struct.unpack_from("<H", chunk, mp + 4 + 4 + 4 + 18)[0]
    hdr_mdb = 32 + 4 * vb_total_i
    if hdr_mdb > end - mp: return []
    vb_offs = struct.unpack_from(f"<{vb_total_i}I", chunk, mp + 32)
    verts = []
    for vbo in vb_offs:
        dp = mp + vbo
        if dp + 24 > end: continue
        pos_o, nrm_o, uv_o, col_o, wgt_o, vtx, mat = struct.unpack_from("<5IHH", chunk, dp)
        stride = 8 if flag == 0 else 16
        for vi in range(vtx):
            vp = dp + pos_o + vi * stride
            if flag == 0:
                x, y, z = struct.unpack_from("<3h", chunk, vp)
                verts.append((float(x)/16.0, float(y)/16.0, float(z)/16.0))
            else:
                x, y, z = struct.unpack_from("<3f", chunk, vp)
                verts.append((x, y, z))
    return verts

def parse_scr_mdb_verts(chunk, start, end):
    """Walk SCR -> nodes -> MDB, return all verts from all MDB models."""
    n = end - start
    if n < 16: return []
    sig = struct.unpack_from("<I", chunk, start)[0]
    if sig != 0x00726373: return []
    node_total = struct.unpack_from("<I", chunk, start + 8)[0]
    if node_total == 0 or node_total > 4096: return []
    hdr = 16 + 4 * node_total
    if hdr > n: return []
    node_offs = struct.unpack_from(f"<{node_total}I", chunk, start + 16)
    all_verts = []
    mdb_seen = set()
    for no in node_offs:
        if no + 112 > n: continue
        np_ = start + no
        mdb_rel = struct.unpack_from("<i", chunk, np_)[0]
        mp = np_ + mdb_rel
        if mp < start or mp >= end: continue
        if mp in mdb_seen: continue
        mdb_seen.add(mp)
        verts = parse_mdb_verts(chunk, mp, end)
        all_verts.extend(verts)
    return all_verts

def main():
    data = read_file(DAT_PATH)
    dat = parse_dat(data)
    all_verts = []
    for typ, off, end in iter_chunks(data, dat):
        if typ == "MD":  # SCR container
            verts = parse_scr_mdb_verts(data, off, end)
            all_verts.extend(verts)
            print(f"SCR chunk @{off:#x}: {len(verts)} verts")
    print(f"total verts: {len(all_verts)}")
    if not all_verts:
        print("FAIL: no verts"); sys.exit(1)
    # normalize to roughly -1..1
    xs = [v[0] for v in all_verts]; ys = [v[1] for v in all_verts]; zs = [v[2] for v in all_verts]
    cx = (max(xs)+min(xs))/2; cy = (max(ys)+min(ys))/2; cz = (max(zs)+min(zs))/2
    scale = 1.0 / max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs)) * 1.8
    norm = [((x-cx)*scale, (y-cy)*scale, (z-cz)*scale) for x,y,z in all_verts]
    with open(OUT_PATH, "wb") as f:
        f.write(struct.pack("<I", len(norm)))
        for x,y,z in norm:
            f.write(struct.pack("<3f", x, y, z))
    print(f"wrote {OUT_PATH}: {len(norm)} verts normalized")

if __name__ == "__main__":
    main()
