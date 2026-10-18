#!/usr/bin/env python3
# Probe tail SNR2 tahap 8 — TES BASIS alamat: reloc 'loc' itu file-offset atau code-relative(0 = awal code di file 128)?
# Reloc type MIPS: 4=R_MIPS_26 (patch instr j/jal, op=2) · 5=R_MIPS_HI16 (patch lui, op=0x0F) · 6=R_MIPS_LO16 (patch imm rendah)
import csv, os, struct, collections

CUSTOM = "/root/ports/godhand/extract/afs/custom"
MAN = "/root/ports/godhand/extract/snr2_manifest.csv"

def word(data, off):
    if 0 <= off <= len(data) - 4:
        return struct.unpack_from("<I", data, off)[0]
    return None

def main():
    files = []
    for r in csv.DictReader(open(MAN)):
        d = open(os.path.join(CUSTOM, r["file"]), "rb").read()
        files.append((r["file"], d, r))

    for base in (0, 128):
        stats = collections.Counter()
        for fn, d, r in files:
            ro, nsym, so = int(r["reloc_off"]), int(r["n_reloc"]), int(r["sym_off"])
            for k in range(nsym if False else int(r["n_reloc"])):
                loc, rest = struct.unpack_from("<II", d, ro + k*12)[:2]
                typ = rest & 0xFF
                w = word(d, base + loc)
                if w is None:
                    stats[f"base{base}:loc_out_of_range"] += 1; continue
                op = w >> 26
                if typ == 4:
                    stats[f"base{base}:t4_op{op:02x}"] += 1
                elif typ == 5:
                    stats[f"base{base}:t5_op{op:02x}"] += 1
                elif typ == 6:
                    # LO16: banyak opcode sah (lw, ori, addiu, lhu, sh...)
                    stats[f"base{base}:t6_op{op:02x}"] += 1
        print(f"--- base={base}")
        for pre in (f"base{base}:t4", f"base{base}:t5", f"base{base}:t6", f"base{base}:loc"):
            sub = sorted(((k.split(':')[-1], v) for k, v in stats.items() if k.startswith(pre)),
                         key=lambda x: -x[1])
            print("   ", pre, "->", sub[:8])

    # tes sym value: apakah ada defined sym value >= code_size (bukti file-offset) ?
    a = b = 0
    examples = []
    for fn, d, r in files:
        cs, te, so, nsym = int(r["code_size"]), int(r["table_end"]), int(r["sym_off"]), int(r["n_sym"])
        for k in range(nsym):
            no, v, sid = struct.unpack_from("<III", d, so + k*12)
            if v == 0: continue
            if v >= 128 + cs:      # di luar region code kalau file-offset
                a += 1
            if v >= cs and v < 128 + cs:   # >= code_size tapi < 128+code_size -> file-offset kalo nilai code-rel mustu < cs
                b += 1
                if len(examples) < 6:
                    end = d.find(b"\x00", no, int(r["reloc_off"]))
                    examples.append((fn, d[no:end].decode('ascii','replace'), v, cs))
    print(f"sym value >= 128+code_size: {a} (harus 0 utk kedua interpretasi)")
    print(f"sym value in [code_size, 128+code_size): {b}  -> >0 membuktikan FILE-OFFSET")
    for e in examples: print("   ", e)

if __name__ == "__main__":
    main()
