#!/usr/bin/env python3
# God Hand (SLUS-21503) — ISO -> extracted ADX provenance verifier
# Runtime: python3 stdlib | Target: prove every .adx on disk came out of the ISO
#
# Independently re-walks the master AFS TOC (ISO offset 4,769,792), collects every
# CRI ADX entry (recursing into nested AFS), and compares the resulting sha1
# multiset against the extracted directory. Then scans the whole ISO for ADX
# headers that are NOT in the AFS walk and classifies them: a real ADX must sit on
# a 2048-byte CD boundary and its type-8 scale words must decrypt to a sane
# adaptive range with the game keystring. Anything else is a coincidental byte
# pattern inside packed/compressed data.
#
# Usage: adx_iso_verify.py [--iso PATH] [--dir PATH] [--key karaage] [--dump-miss]
import argparse, collections, hashlib, mmap, os, re, struct, sys

AFS_BASE = 4_769_792
PRIME_RE = re.compile(r"0x([0-9A-Fa-f]{4})")


def load_prime_table(cpp_path):
    text = open(cpp_path, "r", encoding="utf-8", errors="replace").read()
    body = text.split("kKey8Primes[0x400] = {")[1].split("};")[0]
    table = [int(x, 16) for x in PRIME_RE.findall(body)]
    if len(table) != 0x400:
        raise SystemExit("prime table: expected 1024 entries, got %d" % len(table))
    return table


def derive_key(table, keystring):
    k1, k2, k3 = table[0x100], table[0x200], table[0x300]
    for b in keystring.encode("ascii"):
        p = table[b + 0x80]
        k1 = table[(k1 * p) % 0x400]
        k2 = table[(k2 * p) % 0x400]
        k3 = table[(k3 * p) % 0x400]
    return k1, k2, k3


def walk_afs(f, iso_size, base, prefix, hits, depth=0):
    f.seek(base)
    head = f.read(8)
    if len(head) < 8 or head[:4] != b"AFS\x00":
        return
    count = struct.unpack_from("<I", head, 4)[0]
    if count > 100000:
        return
    toc = []
    for _ in range(count):
        raw = f.read(8)
        if len(raw) < 8:
            return
        toc.append(struct.unpack("<II", raw))
    for i, (off, size) in enumerate(toc):
        start = base + off
        if size <= 0 or start + size > iso_size:
            continue
        f.seek(start)
        magic = f.read(8)
        if magic[:4] == b"AFS\x00" and depth < 3:
            walk_afs(f, iso_size, start, "%s_%d" % (prefix, i), hits, depth + 1)
        elif magic[:2] == b"\x80\x00" and size > 0x40:
            f.seek(start)
            data = f.read(size)
            if len(data) < 0x28 or data[0x12] != 0x04:
                continue
            hsize = struct.unpack_from(">H", data, 2)[0]
            if data[hsize - 2:hsize + 4] != b"(c)CRI":
                continue
            hits[(prefix, i)] = (start, size, hashlib.sha1(data).hexdigest())


def scan_iso_headers(mm, iso_size):
    """Every (c)CRI in the image plus the ADX magic that must precede it."""
    out = []
    i = 0
    while True:
        j = mm.find(b"(c)CRI", i)
        if j < 0:
            return out
        i = j + 1
        for hsize in range(0x14, 0x900, 2):
            start = j - hsize + 2
            if start < 0:
                break
            if mm[start:start + 2] != b"\x80\x00":
                continue
            if struct.unpack(">H", mm[start + 2:start + 4])[0] != hsize:
                continue
            out.append(start)
            break


def scale_sanity(mm, off, hsize, channels, key, frames=200):
    state, mult, add = key
    scales = []
    for i in range(frames):
        p = off + hsize + 4 + i * 18 * channels
        if p + 2 > len(mm):
            break
        word = struct.unpack(">H", mm[p:p + 2])[0] ^ state
        scales.append(word & 0x1FFF)
        state = (state * mult + add) & 0x7FFF
    if not scales:
        return 8191.0
    return sum(scales) / float(len(scales))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="/root/ports/godhand/God_Hand_USA_SLUS21503.iso")
    ap.add_argument("--dir", default="/root/ports/godhand/extract/afs")
    ap.add_argument("--key", default="karaage")
    ap.add_argument("--cpp", default=os.path.join(os.path.dirname(__file__), "..", "src", "shim", "adx.cpp"))
    ap.add_argument("--dump-miss", action="store_true")
    args = ap.parse_args()

    iso_size = os.path.getsize(args.iso)
    table = load_prime_table(args.cpp)
    key = derive_key(table, args.key)
    print("keystring %r -> %04X/%04X/%04X" % (args.key, key[0], key[1], key[2]))

    entries = {}
    with open(args.iso, "rb") as f:
        walk_afs(f, iso_size, AFS_BASE, "m", entries)
    iso_hashes = collections.Counter(h for _, _, h in entries.values())
    print("ADX entries found by AFS walk: %d (%d unique sha1, %d bytes)"
          % (len(entries), len(iso_hashes), sum(sz for _, sz, _ in entries.values())))

    disk = {}
    for name in sorted(os.listdir(args.dir)):
        if not name.lower().endswith(".adx"):
            continue
        data = open(os.path.join(args.dir, name), "rb").read()
        disk[name] = hashlib.sha1(data).hexdigest()
    disk_hashes = collections.Counter(disk.values())
    print("ADX files on disk: %d (%d unique sha1)" % (len(disk), len(disk_hashes)))
    print("sha1 multiset identical: %s" % (iso_hashes == disk_hashes))
    print("disk files without ISO twin: %d" % sum((disk_hashes - iso_hashes).values()))
    print("ISO entries not extracted: %d" % sum((iso_hashes - disk_hashes).values()))

    with open(args.iso, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        known = {off for off, _, _ in entries.values()}
        strays = [o for o in scan_iso_headers(mm, iso_size) if o not in known]
        real, fake = [], []
        for off in strays:
            hsize = struct.unpack(">H", mm[off + 2:off + 4])[0]
            channels = mm[off + 7]
            aligned = (off % 2048) == 0
            mean = scale_sanity(mm, off, hsize, channels, key)
            (real if (aligned and mean < 600.0) else fake).append((off, hsize, channels, mean, aligned))
        print("ADX-shaped headers outside the AFS walk: %d (real=%d false-positive=%d)"
              % (len(strays), len(real), len(fake)))
        if args.dump_miss:
            for off, hsize, ch, mean, aligned in strays:
                hdr = struct.unpack(">H", mm[off + 2:off + 4])[0]
                sc = struct.unpack(">I", mm[off + 12:off + 16])[0]
                print("  off=%-11d hdr=0x%x ch=%d samples=%d align2048=%s scale_mean=%.1f -> %s"
                      % (off, hdr, ch, sc, aligned, mean,
                         "REAL (not extracted)" if (aligned and mean < 600.0) else "false positive"))
        mm.close()

    ok = (iso_hashes == disk_hashes) and not real
    print("RESULT: %s" % ("PASS — extraction complete and byte-identical" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
