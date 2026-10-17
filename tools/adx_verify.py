#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6c — verifikasi decode ADX SELURUH 367 inner entry (baseline ffmpeg)
# Runtime: python3 + ffmpeg (adpcm_adx). Input: extract/adx_inner_manifest.csv (adx_batch.py).
# Per entry: decode penuh -> md5 PCM s16le (stream, tanpa simpan PCM) + byte count + durasi,
#            plus md5 prefix (first PBLOCK blockset = 256*32*2*ch byte pertama) buat compare
#            dgn tools/adx_decode.py (decoder stdlib).
# Usage: adx_verify.py <out.csv>          # pass1: ffmpeg baseline (semua entry)
#        adx_verify.py --mine <in.csv> <out.csv>   # pass2: decoder stdlib, compare
# Exit: rc=0 kalau semua row OK; row gagal tetap ditulis (jujur, error mentah di kolom status).
import csv, sys, os, subprocess, hashlib, tempfile, shutil

MANIFEST = "/root/ports/godhand/extract/adx_inner_manifest.csv"
NESTED = "/root/ports/godhand/extract/afs/afs-nested"
FFMPEG = "/root/.hermes/tools/ffmpeg-9.0.1-linux-x64/bin/ffmpeg"
PBLOCK = 256  # blockset prefix untuk compare lintas decoder

sys.path.insert(0, "/root/ports/godhand/tools")
from adx_decode import decode, parse_header

def inner_slice(outer, off, size):
    with open(f"{NESTED}/entry_{outer}.bin", "rb") as f:
        f.seek(int(off))
        data = f.read(int(size))
    assert len(data) == int(size), f"short read {outer}:{off}"
    return data

def run_ffmpeg_pass(rows, outp):
    tmpd = tempfile.mkdtemp(prefix="adxv_", dir="/root/.hermes/cache/scratch")
    outrows = []
    ok = fail = 0
    try:
        for r in rows:
            key = f"{r['outer_entry']}:{r['inner_idx']}"
            data = inner_slice(r["outer_entry"], r["inner_off"], r["inner_size"])
            try:
                h = parse_header(data)
                ch, rate = h.channels, h.rate
                kbytes = PBLOCK * 32 * 2 * ch
                tf = os.path.join(tmpd, "in.adx")
                with open(tf, "wb") as fp:
                    fp.write(data)
                p = subprocess.run(
                    [FFMPEG, "-hide_banner", "-loglevel", "error", "-y", "-i", tf,
                     "-vn", "-f", "s16le", "pipe:1"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
                pcm = p.stdout
                if p.returncode != 0:
                    raise RuntimeError(f"ffmpeg rc={p.returncode} stderr={p.stderr[-300:]!r}")
                hsh = hashlib.md5(pcm).hexdigest()
                pre = hashlib.md5(pcm[:kbytes]).hexdigest() if len(pcm) >= kbytes else None
                warn = p.stderr.count(b"corrupt")
                sec = len(pcm) / 2 / ch / rate if rate else 0
                outrows.append([r["outer_entry"], r["inner_idx"], r["inner_size"],
                                rate, ch, h.samples, len(pcm), f"{sec:.3f}", hsh,
                                pre if pre else "SHORT", warn, "OK"])
                ok += 1
            except Exception as e:
                outrows.append([r["outer_entry"], r["inner_idx"], r["inner_size"],
                                "", "", "", "", "", "", "", 0, f"FAIL:{type(e).__name__}:{e}"])
                fail += 1
            if (ok + fail) % 25 == 0:
                print(f"progress {ok+fail}/{len(rows)} ok={ok} fail={fail}", flush=True)
    finally:
        shutil.rmtree(tmpd, ignore_errors=True)
    with open(outp, "w", newline="") as fp:
        w = csv.writer(fp)
        w.writerow(["outer", "inner", "size", "rate", "ch", "hdr_samples",
                    "pcm_bytes", "sec", "pcm_md5", "prefix_md5", "ff_corrupt_pkts", "status"])
        w.writerows(outrows)
    print(f"FFMPEG PASS DONE ok={ok} fail={fail} -> {outp}", flush=True)
    return fail

def run_mine_pass(inpath, outp):
    base = {}
    with open(inpath) as fp:
        for row in csv.DictReader(fp):
            base[(row["outer"], row["inner"])] = row
    outrows = []
    ok = pfx_ok = full = fail = 0
    SUBSET = {("1052", "12"), ("0700", "0"), ("0204", "0"), ("0069", "0"),  # plain: 48k mono/stereo, 24k mono/stereo
              ("1070", "0"), ("0069", "2"), ("0066", "42"), ("0066", "0"), ("0066", "20"),  # ext: kecil/sedang/besar 17MB
              ("0066", "52")}  # plain stereo terbesar 9.3MB — cakup semua kelas (variant,rate,ch)
    for (o, i), b in base.items():
        if b["status"] != "OK":
            outrows.append([o, i, "SKIP_BASE_FAIL", "", "", b["status"]]); fail += 1; continue
        try:
            data = None
            size = int(b["size"]); ch = int(b["ch"])
            data = _slice_from_manifest(o, i, size)
            kbytes = PBLOCK * 32 * 2 * ch
            h, pcm = decode(data, max_blocksets=PBLOCK)
            mine_pre = hashlib.md5(pcm).hexdigest() if len(pcm) >= kbytes else "SHORT"
            match = (mine_pre == b["prefix_md5"])
            pfx_ok += match
            full_md5 = ""
            if (o, i) in SUBSET:
                _h, pcm_full = decode(data)
                full_md5 = hashlib.md5(pcm_full).hexdigest()
                full_ok = (full_md5 == b["pcm_md5"])
                ok += full_ok
                outrows.append([o, i, "FULL", match, full_ok, full_md5, b["pcm_md5"]])
            else:
                outrows.append([o, i, "PREFIX", match, "", mine_pre, b["prefix_md5"]])
            if not match:
                fail += 1
        except Exception as e:
            outrows.append([o, i, "ERR", "", "", "", f"{type(e).__name__}:{e}"])
            fail += 1
    with open(outp, "w", newline="") as fp:
        w = csv.writer(fp)
        w.writerow(["outer", "inner", "kind", "prefix_match", "full_match",
                    "mine_md5", "ff_md5_or_note"])
        w.writerows(outrows)
    print(f"MINE PASS DONE prefix_match={pfx_ok}/{len(base)} full_match={ok}/{len(SUBSET)} "
          f"fail={fail} -> {outp}", flush=True)
    return 0 if fail == 0 and pfx_ok == len(base) and ok == len(SUBSET) else 1

_MAN = {}
def _slice_from_manifest(o, i, size):
    if not _MAN:
        with open(MANIFEST) as fp:
            for row in csv.DictReader(fp):
                _MAN[(row["outer_entry"], row["inner_idx"])] = row
    row = _MAN[(o, i)]
    return inner_slice(row["outer_entry"], row["inner_off"], row["inner_size"])

def main():
    args = sys.argv[1:]
    if args and args[0] == "--mine":
        rows = list(csv.DictReader(open(args[1])))
        sys.exit(run_mine_pass(args[1], args[2]))
    else:
        rows = list(csv.DictReader(open(MANIFEST)))
        print(f"manifest rows: {len(rows)}", flush=True)
        sys.exit(run_ffmpeg_pass(rows, args[0] if args else "/root/ports/godhand/extract/adx_verify_ffmpeg.csv"))

if __name__ == "__main__":
    main()
