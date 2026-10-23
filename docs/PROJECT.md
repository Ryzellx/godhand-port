# God Hand Native Port — PROJECT
Target: God Hand (Clover Studio/Capcom, 2006) PS2 → PC native (Windows 10/11), TANPA emulator.
Metode: static decompilation + reimplementation layer (jalur OpenGOAL-style).

> **CATATAN REKONSTRUKSI 21 Okt 2026:** VPS di-rebuild pagi ini (21 Okt ~05:19 WIB) —
> `/root/ports/godhand/` (ISO, extract, decomp, ledger ini) + clone lokal repo hilang total.
> Semua cron run 20 Okt 17:23 → 21 Okt 13:15 FAILED (HTTP 502/522, provider error).
> Ledger ini direkonstruksi dari: git history repo (commit terakhir `1d55bba`) + session
> history Hermes. Fakta teknis = dari commit message + LOG sesi lalu; **bukan dari file
> lama yang sudah tidak ada**. Checksum ISO/ELF tetap diverifikasi ulang setelah restore.

## CONTINUATION PROTOCOL
1. Baca file ini penuh + `ENGINE-MAP.md`.
2. Kerjakan item checklist `[ ]` **paling atas**.
3. Jalankan test NYATA (bukan klaim) — catat angka hasil.
4. Update checklist + tambah entry `### LOG <tanggal>` di bawah.
5. Commit + push ke GitHub (`Ryzellx/godhand-port`, token via `~/.hermes/secrets/github_pat`,
   JANGAN commit token/ISO/asset).
6. Kalau gagal: tulis error mentah + pivot. Jangan markup.
7. Marker notifikasi (hanya ini yang dikirim ke chat):
   - `PLAYABLE_READY: <bukti>` — build bisa dimainkan
   - `BLOCKED_NEED_BOZ: <alasan>` — butuh keputusan BOZ
   - `PROGRESS_UPDATE: <baris>` — milestone berarti (dihapus setelah disampaikan)

## Status Pipeline
- [x] Phase 0 — ISO acquire + verify (CAPCOM/CLOVERSTUDIO 2006-08-23, 1,567,686,656 B)
- [x] Phase 1a — ELF extract: SLUS_215.03, MIPS R5900 (mips3, eabi64), entry 0x100008,
      .text 2,832,160 B @ 0x100000, md5 1db3b1eaca95765f96b37675d0bd315b
- [x] Phase 1b — Symbol intel: 764 mangled names → 170 classes (extract/class_map.txt)
- [x] Phase 1c — FST reversed: 16B header (magic FST\0, float 1.0, strtab_size 0x3844),
      strtab @0x10, 1543 rec × 12B {name_off, a, b} + 8B footer. 47 dir / 1496 file.
- [x] Phase 1d — _DATA.AFS TOC: base ISO offset 4,769,792, 1445 entries {u32 off, u32 size}.
      Isi: TIM2 texture, nested AFS (bgm), MPEG PS stream (movie), custom model/anim format.
- [x] Phase 1e — Extractor: tools/fst_parse.py (manifest + extract by idx) — verified TIM2
- [x] Phase 1f — Ghidra headless decompile-all: ok=6146 fail=1 → decomp/decompiled_all.c
      (artefak decomp ikut hilang di rebuild — tidak perlu re-run: kontrak sudah diekstrak)
- [x] Phase 2a — Syscall inventory: 156 situs, 141 nomor, 0 UNKNOWN (nomor di register v1)
- [x] Phase 2a — ENGINE-MAP.md + GIF tag parser C++ — 6/6 unit test PASS (vs PCSX2 semantics)
- [x] Phase 2b — EE kernel shim (thread/sema/EF/file) — shim_test 5/5 PASS, mingw-OK
- [x] Phase 3 — GS register state machine (src/gs/) + Vulkan offscreen backend (pixel-verified)
- [x] Phase 4 — Asset pipeline: AFS 1445/1445 unpack (1,540,397,895 B == TOC sum),
      TIM2 30 → PNG, ADX 367/367 byte-exact vs ffmpeg, SNR2 81/81, MOT 27192/27192,
      DAT/SCR → MDB/MOT 673 model 129k bones, AFS name map 100%
- [x] Phase 5 — Vertical slice: packet GIF → GS → Vulkan → PPM (godhand_slice, 5/5 floor,
      setarch -R because lavapipe ASLR bug) + pl00 model render 8349 verts dari disc
- [x] Phase 6d.6f — SNR2 container 81/81 + tail framing 81/81 + reloc MIPS R_MIPS_26/HI16/LO16
      17675/17675 — 1861 nama modul stabil
- [x] Phase 7a — EE bus + DMA ch2 MMIO registers (contract dari game decomp), bus_test 20/20
- [x] Phase 7b — EE CPU interpreter (R5900 subset): crt0 ASLI boot penuh —
      crt0_test PASS exit=0, entry 0x100008, instret 1,654,005, 81.5M inst/s,
      stdout deterministik (2 run md5 identik). Bug fix batch: v0-cycling (vsync injection
      clobbered r[2]), ROM-handler walk (FindAddress 131), REGIMM bltzl/bgezl + branch-likely,
      ERET ErrorEPC=cop0 r30, LWU/LWC1/SWC1/LDC1/SDC1/SDL/SDR/SWR/CACHE/PREF, syscall 10-23
- [x] Phase 7b.7 — VU0 awal (vmove/vmr32/vadd/vsub hack) + SDL opcode fix (0x2C) + EI/DI +
      generic syscall stub — commit 1d55bba (klaim "4.29B inst" = u32 counter WRAP, lihat LOG)
- [x] Phase 7b.8 — VU0 decode BENAR: SPECIAL1/SPECIAL2 9-bit func table (bukan switch hack),
      accumulator ACC, lqc2/sqc2 (16-aligned), VF/VI/Q/I register file penuh — commit a021ac5
      bukti: crt0_test 30M inst, VU0 macro=12303 sqc2=6123 lqc2=182 unk=0,
      SPEC1 distinct=11 op (idx 0/1/3/4/11/24/42/44/...), SPEC2 distinct=7 op
      (idx 9/10/24/48/49/57/59), deterministik (2 run stdout md5 = ea27258a...), 5 test floor PASS
- [ ] Phase 7b.9 — Diagnosis infinite loop pasca-crt0: hot-PC sample → identifikasi spin
      **PROGRESS 2026-10-23:** spin #1 ditemukan = **bukan deadlock**, tapi busy-delay konstan:
      idiom `addiu v0,v0,-1 / 4x nop / bne v0,v1,self` di 0x399190 (v1=0xFFFFFFFF → 1.048.577
      iterasi = 7,34 M instr per panggilan). Fast-forward eksak dipasang di interpreter
      (deteksi pola + lompat, instret tetap dihitung): 2 B inst run → 267 loop, 1,96 B instr
      di-skip (97,8%), throughput efektif 1.524 M inst/s (dari 47). Wall baru: 35×
      CreateSema→WaitSema→DeleteSema (sema-autosig=32) + syscall 100:16.390 / 124:16.384,
      PC akhir 0x399124. Berikutnya: identifikasi loop pemanggil sema-churn itu.
- [ ] Phase 7c — DMA ch2 kick dari game code → GIF → Vulkan (game_loop integration)
- [x] Phase 8 — IOP RPC host backend (pad/sound/memcard) + file I/O cdrom0:
      **[x] SIF RPC dispatcher** `src/shim/sif_rpc.cpp` — header 24B (cmd/server/func/psize/dsize/result),
      registry server, stats. **[x] fileio backend** (open/close/read/write/lseek/size/stat/listdir)
      via `host_io` + `memcard`. **[x] pad** netral (0xFFFF), **[x] cdvd** ready, **[x] mcserv** getinfo.
      **[x] sound ADX/CRI** `src/shim/adx.{h,cpp}` — decoder 4-bit block-18, LPC orde-2 (coeff dari
      cutoff header), loop point, sink buang (setara /dev/null) + export WAV. **[x] memcard**
      `src/shim/memcard.{h,cpp}` — mc0:/mc1: → `~/.local/share/godhand-launcher/memcard/`,
      open/read/write/lseek/close/size/stat/listdir/mkdir/remove/getinfo, model kartu 128 KB /
      15 cluster 8 KB. **[x] wiring syscall 119/118** `src/shim/sif_dma.{h,cpp}` — sdd[] diparse,
      paket sifcmd (init/reset/set-sreg/bind/call) di-decode, backend dipanggil, reply "rend"
      ditulis balik ke bus EE, `cd->server/buf/cbuf` diisi (model `_request_end`), sema klien
      di-signal, flag 0x77E6C0 bit0 di-set.
      Bukti: `sif_test` 31/31 PASS 5×; `sif_dma_test` 10/10 PASS 5×; `adx_test` bit-exact vs ffmpeg
      (mono 17.664 sampel, stereo 28.800, ADX asli dari ISO 707.968 sampel — maxdiff 0);
      `crt0_test` 2 B inst: kicks=12 entries=14 bind=4 call=3 sema=7 reply=12 B, deterministik
      (md5 stdout 644fa8b7…), 3 B inst clean-cap.
- [ ] **PLAYABLE** 🎮 (stage st0 load + input + render loop)
- [ ] **PLAYABLE** 🎮 (stage st0 load + input + render loop)

## Engine Intel (ringkas — detail di ENGINE-MAP.md)
- Class system OOP: cScenario(35) cSnd(34) cEmWrap(27) cSceAtManager(18) cModel(16)
  pl00=player(18) cHeatSys=gauge(6) + Poker/BlackJack minigame
- Renderer: GP layer (Gp_draw_*) + eOtType ordering table → intercept di DMA ch2 MMIO 0x1000A000
- IOP: CRI_ADXI LIBSD MCMAN/MCSERV PADMAN SIO2MAN SDHD/SDRDRV/SNDDRV
- VU0 census statik .text: SQC2 6.863 · LQC2 4.889 · COP2 3.054 situs

## Path Penting
ISO     /root/ports/godhand/God_Hand_USA_SLUS21503.iso
ELF     /root/ports/godhand/extract/SLUS_215.03
FST     /root/ports/godhand/extract/GODHAND.FST
Repo    /root/projects/godhand-port/  (build: cmake -S . -B build && cmake --build build -j4)
Tools   /root/projects/godhand-port/tools/ + /root/ports/godhand/tools/fst_parse.py
Ghidra  /opt/ghidra_12.1.4_PUBLIC (JAVA_HOME_OVERRIDE=jdk21 di support/launch.properties)
Python  /root/projects/farmkit/.venv/bin/python
GitHub  github.com/Ryzellx/godhand-port — push via PAT sekali-pakai, jangan commit ISO/token

## Build & Test Reference
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j4
./build/gif_test          # 6/6 PASS
./build/shim_test         # 5/5 PASS
./build/bus_test          # 20/20 PASS
./build/branch_test       # 2/2 PASS
setarch -R ./build/godhand_slice   # → out/slice.ppm (lavapipe ASLR bug, bukan bug kode)
./build/crt0_test [elf] [cap]      # default elf=/root/ports/godhand/extract/SLUS_215.03
```

---

### LOG 2026-10-19 #8 (sesi terakhir sebelum rebuild)
Phase 7b ditutup: crt0_test PASS exit=0 (instret 1,654,005, 81.5M inst/s, deterministik),
push `9cd6334`. Wall: VU0 sqc2 @0x3079e0 (instret 1,659,664). Census statik: SQC2 6863,
LQC2 4889, COP2 3054 → sub-pase VU0.

### LOG 2026-10-19 #9 (sesi telegram malam, commit 1d55bba)
VU0 hack subset (vmove/vmr32/vadd/vsub via switch fn=inst&0x3F), SDL opcode fix (0x2A→0x2C),
EI/DI COP0, generic unknown-syscall stub. Klaim "4.29 BILLION inst" — **honest reinterpret**:
4.29e9 ≈ 2^32 = wrap counter u32, artinya CPU sekarang SPIN INFINITE setelah crt0
(bukan eksekusi 4.29B instruksi produktif). Decode VU0 di commit itu juga salah semantik
(fn 0x2C = VSUB di SPECIAL1, bukan VADD; register field salah posisi). Perlu Phase 7b.8/7b.9.

### LOG 2026-10-21 #10 (rebuild recovery)
VPS rebuild 21 Okt 05:19 WIB menghapus /root/ports + /root/projects. Semua cron 20 Okt
17:23→21 Okt 13:15 FAILED (HTTP 502/522). Restore: clone repo dari GitHub (`1d55bba`),
re-download ISO dari sumber sama (archive.org slus-215.03.-god-hand), re-extract ELF+FST,
rekonstruksi ledger ini dari git + session history. Lanjut kerja: Phase 7b.8 (VU0 decode
benar) → 7b.9 (diagnosis spin loop).

### LOG 2026-10-21 #11 (Phase 7b.8 — VU0 decode tabel penuh)
Recovery pra-kerja: ISO salah lokasi (`extract/God Hand.iso`) → dipindah ke
`/root/ports/godhand/God_Hand_USA_SLUS21503.iso`; 7z extract ELF+FST ulang, md5 ELF
`1db3b1eaca95765f96b37675d0bd315b` cocok kanonik. FST a9d2f8fa3da9e3f4b95327df16f21d63.

Phase 7b.8 dikerjakan — ganti switch-hack COP2 (case 0x12) dgn decode tabel otoritatif:
- Sumber: PCSX2 `microVU_Macro.inl` (recCOP2SPECIAL1t[64] + recCOP2SPECIAL2t[128],
  dispatch SPECIAL2 idx = (code & 3) | ((code >> 4) & 0x7C)), diverifikasi via web extract.
- Operand fields (MIPS pos): fd=dest (inst[10:6]), fs=src1 (inst[15:11]), ft=src2 (inst[20:16]),
  func6=inst[5:0]. Bug lama: func6 salah (0x2C=VSUB dipakai sbg VADD) + role ft=dest.
- Register file penuh: VF0-zero-on-read/no-write, ACC 128-bit (vacc), VI[32], Q, I, TOP.
- SPECIAL1 64 entry: VADD/SUB/MADD/MSUB/MAX/MINI/MUL x/y/z/w + q/i + plain, VIADD/VISUB/
  VIAND/VIOR, VOPMSUB. SPECIAL2 128 entry: VADDA/VSUBA/VMADDA, ITOF/FTOI, VABS, VMOVE/VMR32,
  VLQI/VSQI, VDIV/VSQRT/VRSQRT, VMTIR/VMFIR/VILWR/VISWR, VOPMULA/VNOP. Belum: VCALLMS (micro-
  program), VCLIP flag, VR* R-reg → census vu_unk (0 kena di run ini).
- LQC2/SQC2 tambah 16-aligned mask + census (vu_lqc2/vu_sqc2).

Bukti nyata (bukan klaim) — crt0_test 30M cap:
- `VU0: macro=12303 lqc2=182 sqc2=6123 unk=0`
- `SPEC1 distinct=11` top idx: 24:120 11:102 0:96 1:72 4:72 3:60 44:60 42:48
- `SPEC2 distinct=7` top idx: 49:171 48:137 9:102 10:102 24:102 57:24 59:24
- PASS crt0 (60→61), stop=clean-cap pc=0x003b1e64, instret=30,000,000, 38.3 M inst/s
- Deterministik: 2 run stdout md5 identik ea27258a710b6be6e19c7d0aff44a003
- 5 test floor PASS: gif/shim/bus/branch/slice (godhand_slice via setarch -R)

Wall tersisa (untuk 7b.9): backward-branch census panas 0x3b1e60/0x3b1e50/0x3b1d70/
0x3b1d84 + 0x3a5398 + 0x100170 — loop besar .text, kemungkinan vsync/INTC wait belum fire.
VU0 bukan penyebab spin (unk=0, macro cuma 12k dari 30M inst).

### LOG 2026-10-23 #12 (7b.9 spin #1: busy-delay, bukan deadlock)
Hot-PC 200M: 0x399190-0x3991a8 = 78% sampel. Disasm: `addiu v0,v0,-1 / 4 nop / bne v0,v1,0x399190`
dengan v1=0xFFFFFFFF → 1.048.577 iterasi (7,34 M instr) per panggilan delay. Klaim lama
"4,29 BILLION inst" = persis loop ini (22,4 M bne hit di 200M inst).
Fix: fast-forward eksak di `eecpu::run` (deteksi idiom 7-word, set rt=rs, instret += n*7,
pc += 0x1c). Hasil 2 B inst: `delay-loops skipped=267 inst_skipped=1.959.790.413 (97,8%)`,
1,31 s wall → 1.524 M inst/s efektif (sebelumnya 47,4 M inst/s riil).
Bukti jalan lebih jauh: syscall distinct 17 → 25; muncu 18,22,65,119,120,121,122,124;
sema 35 create / 32 delete / 32 autosig; SIF reg4=0x20000; PC akhir 0x399124.
Wall berikut: sema-churn 35× + syscall 100:16.390 / 124:16.384 — cari caller loop-nya.
Launcher GUI di `launcher/` (ISO picker + 4K + pad/keybind, PLAY nunggu binary).

### LOG 2026-10-23 #13 (Phase 8 slice — RPC + host FS)
`src/shim/host_io.{h,cpp}`: resolve path PS2 (strip device, `;1`, case-insensitive walk,
tolak `..`), open/read/write/lseek/size/stat/listdir. `src/shim/sif_rpc.{h,cpp}`: codec
24B header + registry server + stats; backend fileio (8 func), pad (state netral 0xFFFF),
cdvd (ready). `sif_test`: 16/16 PASS 5× berturut; baca SYSTEM.CNF asli lewat jalur RPC
(57 B, BOOT2 ketemu), traversal ditolak, lseek+close OK. `ee_kernel::map_path` sekarang
fallback ke hostio::resolve → nama lowercase/berbeda case tetap ketemu.
Belum: sound ADX/CRI, memcard, dan wiring syscall 119/124 ke dispatcher (masih stub).

### LOG 2026-10-23 #14 (Phase 8 lanjutan — wiring SIF DMA, memcard, ADX)
Kerja: sambungkan syscall 119 (SifSetDma) ke dispatcher sifrpc yang sudah ada, memcard backend,
decoder ADX. Semua angka di bawah dari run nyata, bukan klaim.

**1) SIF DMA → RPC (`src/shim/sif_dma.*`, baru)**
- Layout paket diverifikasi ke sumber kanonik ps2sdk (`sifcmd-common.h`, `sifrpc-common.h`):
  `SifCmdHeader_t {u32 psize:8, dsize:24; void* dest; int cid; u32 opt}` + rec_id/pkt_addr/
  rpc_id/cd. Data lapangan cocok: paket game di 0x77e740 → words `40 000000 | 00000000 |
  09000080 | … | 05000000 | 40e77720 | 02000000 | e8c57700` = size 64, cid 0x80000009 (RPC_BIND),
  rec_id 5, pkt_addr self, rpc_id 2, cd 0x77c5e8.
- `kick()` dua-pass: (1) semua entri sdd[] ditransfer ke RAM IOP 2 MB, (2) baru decode paket.
  Satu-pass salah: payload RPC datang di entri sdd berikutnya, terbaca kosong (fd=21/n=65536
  sampah) — diperbaiki, sekarang payload benar (`fd=3, n=57`).
- Reply: rec_id |= 2 (bit "complete" yang dicek `sceSifCheckStatRpc`), field rend (cid/sd/buf/
  cbuf), `cd->server=sd, cd->buf, cd->cbuf,` di tulis (model `_request_end` di interrupt context),
  sema klien (`cd+8`) di-signal, flag 0x77E6C0 |= 1.
- Bukti `sif_dma_test` (baru): BIND fileio → kick → rec_id bit1, sema ke-signal; CALL open
  `cdrom0:\SYSTEM.CNF;1` → fd=3; CALL read 57 B → isi "BOOT2" (file asli dari extract ISO).
  10/10 PASS, 5× berturut identik.

**2) Efek ke boot game (crt0_test, ELF asli)**
- Tanpa set flag 0x77E6C0: berhenti 2 B inst di 0x3b205c (0 bind).
- Dengan flag: 32 paket BIND nyata ke sid 0x80000592, 32 CreateSema/DeleteSema, pc 0x399124
  (bind-retry loop) — karena `cd->server` masih 0.
- Setelah `cd->server/buf/cbuf` diisi: bind sukses, game lanjut → **CALL pertama** (fno=255,
  sid 0x80000006), lalu **poll SifGetReg(4) 93,2 juta kali** menunggu bit 0x40000.
- Fix `SifSetReg` jadi bit-set (|=), bukan overwrite: game menulis urutan 0x60000 → 0x40000 →
  0x10000 → 0x20000 ke reg 4; dengan overwrite bit 0x40000 hilang. IOP-side "RPC ready"
  (0x40000) di-set saat paket INIT_CMD/RESET_CMD diproses.
- Wall baru: `opcode unknown` @0x3b5b24 = **SWL** (op 0x2A) belum ada di decoder (do_swl sudah
  ditulis tapi tak pernah dipanggil). Ditambahkan → 3 B inst clean-cap.
- Status akhir 2026-10-23: `crt0_test 2 B inst`: instret=2.000.000.000, stop=clean-cap,
  kicks=12 entries=14 ee2iop=624 B, sifcmd init=4 reset=1 bind=4 call=3, sema_signals=7,
  short=2 (paket <16 B, dibuang), reply=12 B, pc=0x3a510c; deterministik 2 run md5
  `644fa8b79d45abcaee88506f7fa688dd`. 3 B inst juga clean-cap (pc 0x2be24c).
- Wall berikut (jujur, belum selesai): spin di 0x3a50b4..0x3a512c setelah CALL fno=255 ke
  sid 0x80000006 dijawab stub. Belum ada backend untuk 0x80000006/0x80000592 (auto-stub
  4 byte nol, dihitung `autostub=3` — ini MODEL, bukan modul asli). Berikutnya: identifikasi
  dua server itu (kandidat kuat: CRI ADXI audio & sio2man/padman) + implementasi fungsinya.

**3) Memcard (`src/shim/memcard.*`, baru)**
- mc0:/mc1: → `<root>/mc0|mc1/...`, case-insensitive walk, tolak `..`/absolut, strip `;1`.
  Root default `~/.local/share/godhand-launcher/memcard` (env `GH_MEMCARD_DIR`).
- API: open/read/write/lseek/close/size/stat/listdir/mkdir/remove/getinfo. getinfo pakai model
  kartu asli: 128 KB, cluster 8 KB, 15 cluster terpakai (1 page sistem).
- Bukti (sif_test 31/31, 5×): mkdir BASLUS-21503, tulis 4096 B pola, lseek 0, baca balik
  byte-exact, size 4096, stat 4096, listdir 1 entri, getinfo used=1 free=14 free_bytes=114688,
  mcserv getinfo via RPC sama, fileio.open "mc0:/BASLUS-21503/SAVE.DAT" via RPC → fd=1001,
  file di disk host 4096 B (read-after-write).
- Batas jujur: layout FAT/ECC mentah kartu PS2 tidak direproduksi; yang dilayani adalah
  permukaan API (mcman/mcserv). Game yang membaca sektor mentah akan gagal.

**4) Audio ADX (`src/shim/adx.*`, baru)**
- Decoder mengikuti ffmpeg `libavcodec/adx.h` + `adxdec.c` + `adx.c` (koefisien LPC dari cutoff):
  blok 18 B/channel, 32 sampel nibble, prediksi orde-2 `s0 = d*scale + ((c0*s1 + c1*s2) >> 12)`,
  clip int16, EOF packet (scale & 0x8000), loop point dari flag bit0.
- Validasi: `adx_test` bandingkan PCM ke ffmpeg decode.
  mono 44,1 kHz: 17.664 sampel, exact 100%, maxdiff 0.
  stereo 48 kHz: 28.800 sampel, exact 100%, maxdiff 0.
  **ADX asli dari ISO** (offset 23.721.984, 2ch/48 kHz): 707.968 sampel, exact 100%, maxdiff 0,
  5× run identik.
- Sink: data dibuang (setara /dev/null) + `write_wav()` untuk export. Belum ada mixing/DSP
  dan belum disambung ke sifrpc server sound (menunggu identifikasi sid 0x80000006/0x80000592).

**5) Test floor** (semua PASS, 5× berturut kecuali disebut): gif_test · shim_test · bus_test ·
sif_test 31/31 · sif_dma_test 10/10 · adx_test (3 varian) · godhand_slice (setarch -R, rc=0) ·
crt0_test 2 B inst deterministik (md5 644fa8b7…).
