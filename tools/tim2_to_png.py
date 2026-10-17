#!/usr/bin/env python3
"""TIM2 -> PNG (stdlib only). Format diverifikasi: OpenKh tm2.md + empiris AFS God Hand.
Layout: filehdr(16) {TIM2,ver,fmt,u16 pics,pad8} -> pic {totalSize,clutSize,imageSize,
headerSize=48,clutColors,pictFmt,mip,clutType(3=linear32),imageType(1=rgba16,3=rgba32,
4=pal4,5=pal8),w,h,GsTex...} -> image data -> clut(akhir utk indexed).
Usage:
  tim2_to_png.py <in.tm2> <out.png>
  tim2_to_png.py --afs <iso> <idx> <out.png>   (baca langsung dari ISO tanpa extract)
"""
import struct, sys, zlib, io

def _png(w, h, rgba: bytes) -> bytes:
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgba[y*w*4:(y+1)*w*4] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))

def _clut32(b: bytes):
    out = []
    for i in range(0, len(b) - 3, 4):
        r, g, bl, a = b[i], b[i+1], b[i+2], b[i+3]
        out.append((r, g, bl, a))
    return out

def tim2_decode(blob: bytes):
    """-> list[(w,h,rgba_bytes)]"""
    assert blob[:4] == b"TIM2", "bukan TIM2"
    pics = struct.unpack_from("<H", blob, 6)[0]
    out = []
    off = 16
    for _ in range(pics):
        total, clutsz, imgsz, hdrsz, clutcolors, _pf, mip, cluttype, imgtype, w, h = \
            struct.unpack_from("<IIIHHBBBBHH", blob, off)
        img_off = off + hdrsz + (48 if False else 0)
        # mipmap struct muncul HANYA jika mip>1 (44B ekstra) — God Hand pakai mip=1
        if mip > 1:
            img_off = off + hdrsz + 44
        data = blob[img_off:img_off + imgsz]
        clut = blob[img_off + imgsz: img_off + imgsz + clutsz]
        pal = _clut32(clut) if cluttype == 3 else []
        rgba = bytearray(w * h * 4)
        if imgtype == 3:                       # rgba32: R,G,B,A langsung
            rgba[:len(data) * 4 // 4] = data[:w*h*4]
        elif imgtype == 1:                     # rgba16: A1B5G5R5
            for i in range(w * h):
                v = struct.unpack_from("<H", data, i*2)[0]
                r = (v & 31) << 3; g = ((v >> 5) & 31) << 3
                b2 = ((v >> 10) & 31) << 3; a = 255 if (v >> 15) else 0
                rgba[i*4:i*4+4] = bytes((r, g, b2, a))
        elif imgtype == 4:                     # pal4: 2 pixel/byte, low nibble = x genap
            for i in range(w * h // 2):
                byte = data[i]
                for k, idx in enumerate((byte & 0xF, byte >> 4)):
                    p = i*2 + k
                    if p >= w*h: break
                    if idx < len(pal):
                        r, g, b2, a = pal[idx]
                        rgba[p*4:p*4+4] = bytes((r, g, b2, a))
        elif imgtype == 5:                     # pal8
            for i in range(w * h):
                idx = data[i]
                if idx < len(pal):
                    r, g, b2, a = pal[idx]
                    rgba[i*4:i*4+4] = bytes((r, g, b2, a))
        else:
            raise ValueError(f"imageType tak dikenal: {imgtype}")
        out.append((w, h, bytes(rgba), imgtype, clutcolors))
        off += total
    return out

def afs_extract(iso_path: str, idx: int) -> bytes:
    AFS = 4_769_792
    with open(iso_path, "rb") as f:
        f.seek(AFS)
        _, cnt = struct.unpack("<4sI", f.read(8))
        if not (0 <= idx < cnt): raise IndexError(idx)
        f.seek(AFS + 8 + idx * 8)
        off, sz = struct.unpack("<II", f.read(8))
        f.seek(AFS + off)
        return f.read(sz)

def main():
    if sys.argv[1] == "--afs":
        _, iso, idx, outp = sys.argv[1:5]
        blob = afs_extract(iso, int(idx))
    else:
        blob = open(sys.argv[1], "rb").read()
        outp = sys.argv[2]
    pics = tim2_decode(blob)
    for i, (w, h, rgba, it, nc) in enumerate(pics):
        path = outp if i == 0 else outp.replace(".png", f"_{i}.png")
        open(path, "wb").write(_png(w, h, rgba))
        nz = sum(1 for j in range(0, len(rgba), 4) if rgba[j+3])
        print(f"pic{i}: {w}x{h} type={it} clutcolors={nc} alpha_nonzero={nz} -> {path}")

if __name__ == "__main__":
    main()
