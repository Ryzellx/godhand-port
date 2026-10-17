#!/usr/bin/env python3
# God Hand (SLUS-215.03) Phase 6c — CRI ADX decoder murni stdlib (referensi port native)
# Runtime: python3 (tanpa ffmpeg/dep). Spec: 1:1 dari ffmpeg libavcodec/adxdec.c (release/9.0)
#          + empiris hexdump asli ISO God Hand (header u8@7=ch, u32BE@8=rate, cutoff u16BE@0x10).
# Layout header (empiris + ffmpeg):
#   u16BE@0x00 = 0x8000 (magic) · u16BE@0x02 = header_size → data mulai @hs+4
#   u8@0x04=3 (encoding) · u8@0x05=18 (block size B) · u8@0x06=4 (bits/sample)
#   u8@0x07=channels · u32BE@0x08=sample_rate · u16BE@0x10=cutoff (Hz)
#   "(c)CRI" @ (data_offset-6)
# Codec: per blok 18 B per kanal = u16BE scale + 32 nibble signed-4bit (MSB-first):
#   s0 = d*scale + ((coeff0*s1 + coeff1*s2) >> 12); clip int16. scale&0x8000 = EOF marker.
#   LPC: a=√2-cos(2π·cutoff/rate); b=√2-1; c=(a-√((a+b)(a-b)))/b;
#   coeff0=lrintf_f32(c·2·4096), coeff1=lrintf_f32(-c²·4096)  (lrintf=float32 lalu round-half-even)
# Usage: adx_decode.py in.adx [out.wav]
import sys, math, struct

def _lrintf(x):
    # C lrintf: konversi double→float32 dulu (IEEE-754 round-to-nearest-even), lalu round-half-even
    f32 = struct.unpack("f", struct.pack("f", x))[0]
    return round(f32)

def calc_coeffs(cutoff, rate):
    a = math.sqrt(2.0) - math.cos(2.0 * math.pi * cutoff / rate)
    b = math.sqrt(2.0) - 1.0
    c = (a - math.sqrt((a + b) * (a - b))) / b
    return _lrintf(c * 2.0 * 4096), _lrintf(-(c * c) * 4096)

class Header:
    __slots__ = ("data_off", "channels", "rate", "samples", "cutoff", "coeff")

def parse_header(data):
    if len(data) < 24 or struct.unpack_from(">H", data, 0)[0] != 0x8000:
        raise ValueError("bukan ADX (magic)")
    h = Header()
    h.data_off = struct.unpack_from(">H", data, 2)[0] + 4
    if len(data) >= h.data_off and data[h.data_off - 6:h.data_off] != b"(c)CRI":
        raise ValueError("(c)CRI tidak cocok di offset-6")
    if not (data[4] == 3 and data[5] == 18 and data[6] == 4):
        raise ValueError(f"varian tak didukung enc={data[4]} blk={data[5]} bits={data[6]}")
    h.channels = data[7]
    if not (0 < h.channels <= 6):
        raise ValueError(f"channels tak valid: {h.channels}")
    h.rate = struct.unpack_from(">I", data, 8)[0]
    if not (0 < h.rate < 2**31):
        raise ValueError(f"rate tak valid: {h.rate}")
    h.cutoff = struct.unpack_from(">H", data, 16)[0]
    h.coeff = calc_coeffs(h.cutoff, h.rate)
    h.samples = struct.unpack_from(">I", data, 12)[0]
    return h

def decode(data, max_blocksets=None, want_header=None):
    """-> (header, pcm interleaved s16LE bytes). Berhenti di blok EOF (scale&0x8000)
    atau akhir data lengkap; blok sisa < 18*ch dibuang (identik ffmpeg)."""
    h = want_header or parse_header(data)
    ch, bs = h.channels, 18
    c0, c1 = h.coeff
    pos = h.data_off
    end = len(data)
    s1 = [0] * ch
    s2 = [0] * ch
    out = [bytearray() for _ in range(ch)]  # planar, diinterleave di akhir
    nsets = (end - pos) // (bs * ch)
    if max_blocksets is not None:
        nsets = min(nsets, max_blocksets)
    for _ in range(nsets):
        done = False
        for c in range(ch):
            scale = struct.unpack_from(">H", data, pos)[0]
            if scale & 0x8000:
                done = True  # blok EOF: buang set yang belum lengkap, berhenti total
                break
            a1, a2 = s1[c], s2[c]
            buf = out[c]
            p = pos + 2
            for i in range(0, 16, 2):
                b0 = data[p]; b1 = data[p + 1]
                for nib in ((b0 >> 4), (b0 & 0xF), (b1 >> 4), (b1 & 0xF)):
                    d = nib - 16 if nib >= 8 else nib
                    v = d * scale + ((c0 * a1 + c1 * a2) >> 12)
                    a2 = a1
                    a1 = -32768 if v < -32768 else (32767 if v > 32767 else v)
                    buf += struct.pack("<h", a1)
                p += 2
            s1[c], s2[c] = a1, a2
            pos += bs
        if done:
            break
    total = min(len(o) for o in out) if ch else 0
    inter = bytearray()
    frames = total // 2
    for i in range(frames):
        for c in range(ch):
            inter += out[c][i * 2:i * 2 + 2]
    return h, bytes(inter)

def write_wav(path, h, pcm):
    n = len(pcm) // 2
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, h.channels, h.rate,
                            h.rate * h.channels * 2, h.channels * 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)

def main():
    if len(sys.argv) < 2:
        print(__doc__ or "usage: adx_decode.py in.adx [out.wav]"); sys.exit(2)
    data = open(sys.argv[1], "rb").read()
    h, pcm = decode(data)
    print(f"channels={h.channels} rate={h.rate} cutoff={h.cutoff} coeff={h.coeff} "
          f"header_samples={h.samples} pcm_bytes={len(pcm)} "
          f"pcm_samples={len(pcm)//2//h.channels} "
          f"sec={len(pcm)/2/h.channels/h.rate:.3f} md5pcm={__import__('hashlib').md5(pcm).hexdigest()}")
    if len(sys.argv) > 2:
        write_wav(sys.argv[2], h, pcm)
        print(f"wav -> {sys.argv[2]}")

if __name__ == "__main__":
    main()
