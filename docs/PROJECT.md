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
      Bukti: `sif_test` 36/36 PASS 5× (31 test lama + 5 test Phase 8 lanjutan: loadfile versi,
      loadfile LoadModule, iopheap alloc/free/reuse, sjx DTX); `sif_dma_test` 10/10 PASS 5×;
      `adx_test` bit-exact vs ffmpeg
      (mono 17.664 sampel, stereo 28.800, ADX asli dari ISO 707.968 sampel — maxdiff 0).
      **Lanjutan 2026-10-24 (lihat LOG #15):** boots jauh lebih dalam —
      `crt0_test 400M`: kicks=33 entries=51 ee2iop=5.808 B, sifcmd init=4 reset=1 bind=8 call=20
      sema=25, loadfile memuat **7 modul IOP nyata** dari extract/modules/irx (sio2man 5.248 ·
      padman 45.952 · libsd 30.144 · SDRDRV 9.216 · sdhd 14.720 · snddrv 20.352 ·
      cri_adxi 76.928 B), iopheap alloc 2.256 B → 0x00130000 lalu 332.864 B → 0x00130900,
      deterministik 5/5 identik.
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

**4b) Phase 7c readiness — sudah ada kick DMA ch2 dari kode GAME (bukan test)**

`crt0_test` sekarang memasang hook `eekern::set_gif_kick` yang mengalirkan packet DMA ch2
nyata ke parser produksi `gif::parse_stream` (yang unit-test 6/6) dan mencetak census.
Hasil 400M: `bus: kicks=16384 qwords=98304 chcr_polls=49152 vif_polls=0 mmio_other=49250
last{madr=0x1fffe50 qwc=6 chcr=0x0 tte=0}` dan `gif: kicks=16384 tags=16384 actions=0 bad=0`,
5/5 identik. **Tafsir jujur:** 16.384 = batas retry konstan (0x4000) dan isi packet-nya tag
NOL (nloop=0, egl=0) — jadi ini loop "kick + Deci2Call" yang gagal, bukan render nyata.
Bukti positifnya: rantai game→MMIO→sink→parser tersambung tanpa crash (bad=0) dan parser
menerima setiap kick. Render nyata menunggu wall sound di atas lewat.

**5) Test floor** (semua PASS, 5× berturut kecuali disebut): gif_test · shim_test · bus_test ·
sif_test 31/31 · sif_dma_test 10/10 · adx_test (3 varian) · godhand_slice (setarch -R, rc=0) ·
crt0_test 2 B inst deterministik (md5 644fa8b7…).

**6) Repo**: commit `0f7d1ee` (4 commit termasuk Phase 7b.9 & slice Phase 8 sebelumnya) sudah
di-push ke `origin/main` (github.com/Ryzellx/godhand-port). Ledger ini dimirror ke
`docs/PROJECT.md` + `docs/ENGINE-MAP.md` di repo supaya selamat kalau VPS di-rebuild.
Verifikasi push: `git ls-tree -r HEAD` tidak memuat ISO/7z/token (163 file, .git 6,0 MB).

### LOG 2026-10-24 #15 (Phase 8 tuntas: SIF DMA benar, loadfile/iopheap/SJX, memcard)

Sesi ini memperbaiki jalur SIF sampai boot game masuk **inisialisasi sound middleware**,
bukan lagi macet di bind-retry. Semua angka dari run nyata, 5× identik.

**1) Tiga bug model SIF yang ditemukan lewat pengukuran**

- **`cd->server` (SifRpcServerData) harus di RAM EE, `cd->buf/cbuf` di RAM IOP.** Sesi lalu
  memindahkan ketiganya ke 0x00100000/0x00110000/0x00120000 "supaya masuk 2 MB IOP" — benar
  untuk buf/cbuf, **salah untuk server**: 0x00100000 adalah alamat load ELF game (program header
  vaddr 0x00100000, filesz 0x360ce4), jadi 16 B pertama image tertimpa struct. Sekarang
  SD_STRUCT = 0x00F00000 (EE, di atas bss 0x7867b4, di bawah stack 0x1FFFF00),
  SD_BUF = 0x00110000, SD_CBUF = 0x00120000 (IOP 2 MB).
- **Sumber payload CALL ditentukan riwayat tulis DMA, bukan tebakan urutan.** Versi setengah
  jadi selalu membaca `cd->buf`; di `sif_dma_test` (payload menempel di packet+64 lewat entri
  sdd kedua, dst=64) hasilnya 8/10 (read gagal, `got=0`). Sekarang `iop_write()` mencatat
  (addr,len,seq) 16 entri terakhir dan `wr_seq_covering(addr,n)` memilih kandidat yang benar-benar
  ditulis DMA paling akhir; fallback pkt+64 mentah. **sif_dma_test kembali 10/10 ×5.**
- **rec_id bit0 = "masih jalan", bit1 = "selesai".** Bukti semantik dari kode game sendiri:
  `is_busy()` @0x3b3328 (dipakai wait @0x375930) mengembalikan 1 selama `pkt[16] & 1`.
  `finish_request` dulu hanya `|= 2` → spin **325.967.981** iterasi di 0x375918.
  Sekarang `(rec_id & ~1) | 2` → game lanjut.

**2) Backend baru yang membuat boot bergerak (bukan stub)**

- **loadfile SID 0x80000006** (`sif_rpc.cpp`): fno 255 → word versi "3100" (0x30303133,
  terbukti masuk buffer game @0x781ba8); fno 0 = LoadModule — nama `cdrom0:\MODULES\*.IRX;1`
  di-resolve ke `extract/modules/irx/`, ukuran dibaca nyata dari disk, dicatat loaded.
  Hasil: 7 modul, **fail=0**.
- **iopheap SID 0x80000003**: alloc/free heap RAM IOP (bump + free-list, base 0x00130000,
  64 B align). Terpetakan dari kode game: 0x3b5968 = sceSifAllocIopHeap (fno 1, kirim u32 size,
  terima u32 addr), 0x3b59d8 = sceSifFreeIopHeap (fno 2). Tanpa ini autostub balas 0 → game
  mencetak "E0100301: SJX_Init can't allocate IOP Heap" (string .rodata 0x455dc8) lalu spin mati
  di 0x33b2b0 (279.485.177 iterasi terukur).
- **SJX/CRI SID 0x90000200** (MODEL, bukan implementasi middleware): protokol suara game
  `fno = 0x400 | opcode` (terpetakan dari 0x332670). op 0x02 = create DTX (args {0, workbuf EE,
  iopbuf, size} → handle = iopbuf), op 0x22 = SJUNI_CreateRmt (args {tipe, iopbuf, size} →
  handle = iopbuf). Tanpa ini: "E0100302: SJX_Init can't create DTX" / "E0110102 ... can't
  creat SJUNI_CreaetRmt". Handle = alamat buffer yang game sendiri kirim, jadi objek tetap hidup
  di ruang IOP yang game alokasikan — bukan angka karangan.
- **MMIO 0x1000F000** (SIF DMA status EE) balas 0x4 (bit2 "transfer selesai"); kode game
  0x3b0210 memang poll `andi v0,4`. **LWR** diperbaiki (b=0 seluruh word; sebelumnya
  `lwl 3(x)+lwr 0(x)` menghasilkan 0x00000033 bukan 0x30303133).

**3) Status boot nyata (crt0_test, ELF retail)**

`400M inst`: stop=clean-cap, pc=0x00337180, kicks=33 entries=51 ee2iop=51 (5.808 B),
sifcmd init=4 reset=1 sreg=0 bind=8 call=20, rpc bind_replies=8 call_replies=20 sema_signals=25
unknown_srv=3 autostub=3 handler_fail=1 payload=3.840 B reply=76 B,
loadfile load=7 fail=0, iopheap alloc 2.256 B → 0x00130000 + 332.864 B → 0x00130900.
Deterministik **5/5 run identik** (baris stats sama byte-per-byte).

**4) Wall berikutnya (jujur, BELUM selesai)**

pc 0x00337180 = jalur fatal `ps2rna_init_psj` (string 0x455548
"E0110104: ps2rna_init_psj: can't creat SJUNI_Creaet"). Rantainya:
0x337158 `SJUNI_Create` (0x33a190) → slot alloc 0x33a1e8 (tabel 64 entri @0x3e7ff0) →
0x338a10 → 0x340ae8, mengembalikan 0 **tanpa RPC op baru** — jadi kegagalannya lokal di model
suara EE (state PS2RNA/SJUNI), bukan di SIF. Untuk lanjut perlu model objek SJUNI/PS2RNA nyata
(paling sedikit: slot voice, state machine create/attach, buffer IOP yang dipakai DMA), bukan
stub. Ini pekerjaan Phase 9, bukan Phase 8.

**5) Test floor** (semua PASS, 5× berturut, 2026-10-24): gif_test (6/6) · shim_test (ALL PASS) ·
bus_test (ALL PASS) · census GIF ch2 di crt0 (16.384 kick, 0 bad parse) · sif_test 36/36 (naik dari 31: +5 test backend baru) · sif_dma_test 10/10 · crt0_test 400M (deterministik 5/5) ·
adx_test 3 varian (2ch/48 kHz dari ISO: 96.224 sampel/ch, 2,005 s) · godhand_slice
`setarch -R` rc=0.

**6) Repo**: commit menyusul di sesi ini (lihat `git log`), mirror ledger ke `docs/`.

### LOG 2026-10-24 #16 (Phase 9 buka: bug CPU MMI — dinding SJUNI/SJX jebol, boot masuk frame loop)

Ringkas: dinding `ps2rna_init_psj` **bukan** kurang model SJUNI. Akarnya bug interpreter:
opcode MMI (0x1C) di-handle stub yang menulis "rs ke rd" untuk hampir semua instruksi.
Sesudah MMI diimplementasi benar, `SJUNI_Create` mengembalikan handle nyata dan boot lewat
sound-init sampai frame loop (panggilan pad per frame). Semua angka di bawah dari run nyata 5×.

**1) Bukti bug (bukan dugaan)**

- Census statis `.text`: **2.256 situs MMI**, 312 word unik, **34 mnemonic** — yang terbesar
  `pcpyld` 426, `paddsh` 384, `pmaxh` 272, `pminh` 272, `psrah` 260, `ppacb` 136, `paddh` 80.
  Handler lama hanya menangani 7 nilai `fn` dan sisanya `default: w32(rd, rs)`.
- Watchpoint tulis (instrument baru `GH_WATCH`) menunjuk **pc=0x003a532c**: itu `sq t0,0(a3)`
  di dalam **memset libkernel (0x3a52f0)**. Nilai yang masuk ke tabel SJUNI 0x3E7FF0:
  `00000000 01010101 00000000 00000000` per 16 B — pola rusak, karena `pcpyh`/`pcpyld`
  (bagian dari idiom memset: `dsll 8; or; pcpyh; pcpyld; sq`) menghasilkan lane salah.
- Akibatnya fungsi tabel SJUNI (0x33a1e8) membaca byte "+4 aktif" = 1 untuk **semua 64 slot** →
  `SJUNI_Create` balik **0** → game masuk jalur fatal 0x337174 (`E0110104: can't creat
  SJUNI_Creaet`) dan spin di 0x337180: **50.912.112** backward-branch di 400 M instruksi.

**2) Fix: `src/shim/ee_mmi.h` — MMI R5900 lengkap**

- Decode benar: `funct = inst & 0x3F` memilih kelas (MMI0 0x08 / MMI1 0x28 / MMI2 0x09 /
  MMI3 0x29) atau op langsung (MADD/MADDU/PLZCW/MFHI1..MTLO1/MULT1/MULTU1/DIV1/DIVU1/
  MADD1/MADDU1/PMFHL/PMTHL/PSLLH/PSRLH/PSRAH/PSLLW/PSRLW/PSRAW); sub-op = field `sa`.
  Tabel & semantik mengikuti PCSX2 `R5900OpcodeTables.cpp` + `MMI.c` (lane LE 32-bit).
- 128 sub-op MMI0/1/2/3 + 23 op langsung: PADDW/PSUBW/PCGTW/PMAXW/PADDH/…/PPAC5,
  PABSW/PCEQW/PMINW/PADSBH/…/QFSRV, PMADDW/PSLLVW/PMRLVW/PMFHI/PMFLO/PINTH/PDIVW/PCPYLD/
  PMADDH/PHMADH/PAND/PXOR/PHMSBH/PEXEH/PREVH/PMULTH/PDIVBW/PEXEW/PROT3W, PMADDUW/PSRAVW/
  PMTHI/PMTLO/PINTEH/PMULTUW/PDIVUW/PCPYUD/POR/PNOR/PEXCH/PCPYH/PEXCW.
- **Slot yang tak terdefinisi di tabel → `exec()` balik false → CPU fault "MMI unknown".**
  Tidak ada lagi tulisan karangan: instruksi ilegal harus terlihat, bukan disembunyikan.
- `pc > rd` write dilakukan lewat salinan `s[]/t[]/d[]` supaya aliasing rd==rs/rt aman.

**3) Bukti fix (berurutan, tiap langkah dari run nyata)**

| Langkah | Sebelum | Sesudah |
|---|---|---|
| tulis memset ke 0x3E7FF0 | `00000000 01010101 00000000 00000000` | `00000000 00000000 00000000 00000000` (zero-fill benar) |
| `SJUNI_Create` (0x33a190) | `v0 = 0` → spin 0x337180 | `v0 = 0x003E7FF0`, slot terisi `003e7fb8 / 00000101 / 00455ba8 / 003e0a68` |
| dinding berikut | 0x337180 (SJUNI) | 0x3371E0 → `E0100401: can't create SJX of IOP` |
| setelah model SJX_Create | — | lewat; **9 modul IOP** dimuat (termasuk mcman 103.680 B + mcserv 7.744 B) |
| dinding berikut | — | pad read: **49.307** call RPC di 400 M, nol render |

**4) Dua model baru (ditandai MODEL, bukan klaim implementasi asli)**

- **SJX op 0x00 = SJX_Create** (payload `{sjunihandle, iopbuf, 1, tabelptr}`, terukur lewat
  `GH_DUMP_SID`). Model: alokasi **nyata 64 B** dari alokator heap IOP (0x130000..0x200000) →
  handle berbeda tiap panggilan (0x182140, 0x182180, …). Tanpa ini game berhenti di 0x33b4e4.
- **pad fno 1 = scePadRead**: request 128 B word0=0x12, reply dibaca game di `word[3]` lalu
  dicek `(ret >> 8) == 4` di **0x39770C** (= `PAD_STATE_STABLE`). Reply lama 12 B tanpa word[3]
  → ret 0 → game ulang terus. Reply baru: `word[3] = (4<<8) | port`. Efek terukur:
  call RPC **49.307 → 69**, bind 98.492 → 10, dan `sema-autosig` 147.551 → 0.

**5) Frontir sekarang (jujur, belum selesai)**

400 M instruksi berhenti di `pc=0x003A5938` — di dalam libc game: `strlen` (0x3A5A00,
`lb/bnezl`) dan zero-fill byte-per-byte (0x3A5F48, `sb zero`). 30% sampel hot-PC di
0x3A5F48..0x3A5F5C. **Render belum tercapai**: `gif kicks` tetap 16.384 (census Phase 7c) di
100 M maupun 400 M, `actions=0`. Jadi yang terbukti baru: rantai game→MMIO→GIF parser bersih,
bukan frame tergambar.

**6) Instrumentasi baru di `crt0_test` (semua ke stderr, stdout tetap deterministik)**

`GH_HALT_PC` (stop tepat sebelum pc) · `GH_TRACE_WINDOW=lo-hi` (trace per instruksi + GPR kunci) ·
`GH_WATCH=lo-hi` (+`GH_WATCH_CAP`) watchpoint tulis dengan pc penulis · `GH_DUMP_MEM=addr:len,…` ·
`GH_DUMP_SID=0x…` dump payload RPC per server · `MMI_DEBUG` trace decode MMI.

**7) Test floor (5× berturut, md5 stdout identik)**

`mmi_test 25/25` (baru) · `sif_test 36/36` · `sif_dma_test 10/10` · `shim_test ALL` ·
`bus_test ALL` · `gif_test 6/6` · `adx_test` 1 file ISO 2ch/48 kHz rc=0 ·
`crt0_test 30M` & `400M` deterministik (md5 `ac61eb8f4d8b`), stop `clean-cap` tanpa fault.

## PROTOTYPE INTEL — God Hand Preview (Jun 20, 2006)
Sumber: hiddenpalace.org, arsip 421.407.373 B (7z) berisi bin 1.261.928.448 B (MODE1/2048).
ELF: `SLPM_665.50` (JP preview), 3.450.144 B, md5 `b203e5aec76b009a1f4d594f869ca157`.
Ekstraksi tanpa menulis ISO: `7z x -so ... | tools/stream_iso_extract.py` (streaming ISO9660;
tervalidasi dulu di retail → md5 1db3b1ea... cocok).

Fakta berguna:
- Boot langsung ke debug menu saat New Game; menu debug bisa dipanggil kapan saja (L2+R3).
- Retail `SLUS_215.03` **tidak** punya label menu (`ROOM JUMP`, `FLAG EDIT`, dst = 0 hit),
  tapi **masih menyimpan** string path `etc/debugres/RoomJmpTbl.dat` dan FST retail memuat
  record `debugres/roomjmptbl.dat` → data room-jump ikut terkirim di versi final.
- Proto punya 73 string debug, termasuk **51 flag `DBG_*` unik** di .rodata, slot nama 24 byte:
  DBG_FPS_CAMERA_ON, DBG_PL_NO_DEATH, DBG_EM_SET_OFF, DBG_AREA_CAM, DBG_LOW_RESOLUTION,
  DBG_SHOOTING_MODE, DBG_MENU, DBG_TIMER_DISP, dll.
- Belum: ID numerik tiap flag (tabel nama inline di .rodata, bukan pointer table) → perlu
  xref dari kode menu Flag Edit.

Rencana pakai proto:
1. Target render Phase 7c yang lebih murah: menu debug = mayoritas 2D/text packet, tak butuh VU1.
2. Anchor reversing: string + struktur menu → temukan tabel flag → kalau ID sama di retail,
   FPS camera/no-death bisa diaktifkan buat tes tanpa cheat engine.
3. Diff dua ELF (US retail vs JP preview) buat prioritas fungsi engine vs konten.

### LOG 24 Okt 2026 — Phase 9a: CRI ADX decoder (branch `feat/adx-audio`)

Target: backend audio native — decoder CRI ADX 4-bit ADPCM untuk 367 stream ADX di ISO
`SLUS_21503`. File: `src/shim/adx.h`, `src/shim/adx.cpp`, `src/shim/adx_test.cpp`,
`tools/adx_iso_verify.py`. Build: `cmake --build build --target adx_test`.

**1) Sumber data — ADX dari ISO (bukan file tebakan)**
- Master AFS TOC di ISO offset 4.769.792 (`AFS\0`, count=0x5A5). Walk rekursif AFS bersarang
  → **367 entry ADX** (263 sha1 unik; 104 stream duplikat antar arsip), **637.611.778 byte**.
- Provenance diverifikasi ulang independen (`tools/adx_iso_verify.py`): multiset sha1 hasil
  walk ISO **identik** dengan 367 file di `extract/afs/` — 0 file tanpa kembaran ISO, 0 entry
  ISO tidak ter-ekstrak.
- Scan seluruh ISO menemukan 14 header ber-bentuk ADX di luar walk AFS. Semua **false
  positive**: offset-nya tidak jatuh di boundary 2048 dan scale word-nya jadi derau setelah
  didekripsi (`scale_mean` 2259–4209 vs ~131 untuk stream asli). Jadi cakupan ekstraksi
  lengkap, bukan "kira-kira".
- Census header 367 file: encoding=3 (ADX fixed-scale) semua, frame 18 B, 4 bit, cutoff 500 Hz,
  version word **0x0408** (= enkripsi CRI type 8) di semua file; 227 stereo / 140 mono;
  359 @ 48 kHz, 8 @ 24 kHz. 45 file punya blok loop aktif.

**2) Format yang diimplementasi**
- Frame 18 B: 2 B scale word + 16 B nibble → **32 sampel**; `s = clamp16(nib*scale +
  ((c1*s1 + c2*s2) >> 12))`, prediktor orde-2, koefisien dari cutoff (`cos`, Q12).
- Dekripsi **type 8**: LCG 15-bit (`state = (state*mult + add) & 0x7fff`) hanya pada scale word
  tiap frame; nibble tidak dienkripsi. Keystream diturunkan dari keystring lewat tabel 1024
  prima — `"karaage"` → start=0x49E1 mult=0x4A57 add=0x553D. String `karaage` memang ada
  literal di `SLUS_215.03` @ 3.464.736.
- Validasi keystream tanpa percaya kode sendiri: scale word stream asli hasil dekripsi punya
  `mean=131, p99=359, max=2488` (pola encoder adaptif), sedangkan kunci salah memberi
  `mean≈3967–4255, p99≈8069` (derau). Ini bukti kunci benar secara statistik, bukan asumsi.
- Dua flavour aritmetika, keduanya diuji: `Cri` (scale+1, history seed dari header 0x18/0x1c —
  yang dipakai hardware) dan `Ffmpeg` (scale mentah, history nol, koefisien `lrintf`).
- Loop: offset **fixed** untuk v4 — flag `u32@0x24`, start `u32@0x28`, start_byte `u32@0x2c`,
  end `u32@0x30`, end_byte `u32@0x34`, blank samples `u16@0x20`. Offset ini ditentukan
  eksperimen (file sintetis mono/stereo + `vgmstream -m` sebagai hakim), bukan dari doc:
  layout "setelah history" yang dipakai decoder lain salah untuk mono.
- `decode()` mengembalikan frame penuh (paritas libavcodec), `decode_trimmed()` memotong ke
  `sample_count` header — itu panjang yang benar-benar diputar PS2/vgmstream. Selisihnya nyata:
  file terbesar 6.369.984 → 6.369.200 sampel (784 sampel padding encoder).

**3) Hasil verifikasi (5x berturut, semua PASS)**

Referensi ganda: `ffmpeg -i <plain.adx> -f s16le -acodec pcm_s16le ref.raw` untuk flavour
libavcodec, dan `vgmstream-cli` (r2117) langsung pada file **asli terenkripsi** untuk flavour
CRI + metadata loop.

| run | exit | pass/files | ffmpeg byte-exact | vgmstream byte-exact | SNR min (dB) | wall (s) |
|---|---|---|---|---|---|---|
| 1 | 0 | 367/367 | 367/367 | 367/367 | 999.00 | 858.8 |
| 2 | 0 | 367/367 | 367/367 | 367/367 | 999.00 | 898.7 |
| 3 | 0 | 367/367 | 367/367 | 367/367 | 999.00 | 945.1 |
| 4 | 0 | 367/367 | 367/367 | 367/367 | 999.00 | 1040.0 |
| 5 | 0 | 367/367 | 367/367 | 367/367 | 999.00 | 1154.2 |

- Body log 5 run **identik** (sha1 `4b523a8b18b2`) → deterministik, bukan kebetulan.
- ffmpeg: **367/367 byte-exact**, SNR 999 dB (= identik, 0 sampel beda) di setiap file.
- vgmstream: **367/367 byte-exact** pada file terenkripsi asli → keystream + aritmetika CRI +
  trim `sample_count` semuanya benar.
- Total sampel ter-dekode (setelah trim): **572.592.917 sampel** per run.
- Loop: **45 file** ber-loop (gate otomatis: `loop_files=45 loop_verified=45`); start/end/flag dibandingkan otomatis dengan
  metadata vgmstream, semua cocok (contoh: `adx_m66_0000` 471.744–6.163.980;
  `adx_m1070_0000` 13.792–128.480).
- Tabel filter encoding type 2 (filter 0..3) diuji dengan vektor sintetis vs vgmstream:
  4/4 PASS, 12.800 sampel, mismatch 0. (Tidak ada stream type 2 di title ini, jadi jalur ini
  hanya terbukti di vektor sintetis.)
- Reproduksi: `./build/adx_test --tmp /dev/shm/adxtest --vgmstream
  /root/.hermes/tools/vgmstream/vgmstream-cli --synth`
  (default `--tmp` = `/dev/shm`, karena root fs VPS ini cuma sisa ~1 GB dan run pertama mati
  kehabisan disk saat menulis raw reference).

**4) Yang belum (jujur)**
- Belum ada mixing/DSP, belum tersambung ke sound server RPC (`sid` 0x80000006/0x80000592
  belum teridentifikasi) dan belum ada output SPU2 — decoder berdiri sendiri + sink buang/WAV.
- Loop baru **dilaporkan**, belum **diputar**: belum ada carry predictor state saat lompat dari
  `loop_end` ke `loop_start` (perlu restore history per channel di titik loop).
  → **ditutup di Phase 9b** (log di bawah): tiga hipotesis state diuji, satu menang byte-exact.
- Resampling 24 kHz → laju output game belum ada (8 stream 24 kHz).
- Audio non-ADX belum disentuh: stream MPEG/SND dan audio di dalam SFD/PSS.
- Encoding type 2/4 hanya terbukti di vektor sintetis (title ini 100% type 3).
- `--tmp` di `/dev/shm` = data sementara hilang saat reboot; bukan masalah untuk test, tapi
  jangan dipakai untuk cache aset.

### LOG 24 Okt 2026 — Phase 9b: loop benar-benar DIPUTAR (timeline splice) + sink WAV

**1) Yang diserang**

Phase 9a berhenti di "loop dilaporkan": flag/start/end dari header sudah benar dan cocok dengan
metadata vgmstream, tapi belum ada jalur yang benar-benar menyambung `loop_end → loop_start`.
Untuk port, artinya musik 2 menit mati di titik loop. Sekarang `AdxDecoder::render_loop()`
membangun timeline putar: intro `[0, loop_end)` + N body `[loop_start, loop_end)` + tail
`[loop_end, sample_count)`.

**2) Semantik vgmstream dikalibrasi dulu (bukan diasumsikan)**

`vgmstream-cli -l N.0 -F` ternyata **bukan** "N kali loop". Diukur pada `adx_m66_0000`
(48 kHz, 6.369.200 sampel, loop 471.744..6.163.980, body 5.692.236, tail 205.220):

| `-l` | sampel output | arti |
|---|---|---|
| 1.0 | 6.369.200 | satu pass murni (tanpa body tambahan) |
| 2.0 | 12.061.436 | 6.369.200 + 5.692.236 → intro + **1** body + tail |
| 3.0 | 17.753.672 | 6.369.200 + 2 x 5.692.236 → intro + **2** body + tail |

Jadi `-l N` = `N-1` body tambahan; gate kita memakai `loops` (body tambahan) dan memanggil
vgmstream dengan `-l loops+1`. Tanpa `-F`, vgmstream menambah fade 10 s dan perbandingan
byte-exact mustahil.

**3) Tiga hipotesis state prediktor diadu, bukan dipilih selera**

Saat lompat, decoder harus tahu isi history 2-tap per kanal. Tiga kemungkinan diimplementasi
terpisah (`AdxLoopState`): `Carry` (history dari sampel sebelum `loop_end`), `ResetHeader`
(reseed dari header 0x18/0x1c), `Linear` (body diambil apa adanya dari decode linear, yaitu
history "seolah tidak ada lompat"). Diuji otomatis di 45 file ber-loop, `--loop-state auto`:

| state | byte-exact vs vgmstream | catatan |
|---|---|---|
| Linear | **45/45** | 0 sampel beda di 1.013.813.378 sampel per run |
| Carry | 0/45 | SNR 44–81 dB; selisih hanya di beberapa frame setelah lompat |
| ResetHeader | 0/45 | SNR ~45–53 dB |

- Carry vs Linear: **93.798 sampel** beda dari 1.013.813.378 (**0,0093%**), |Δ| maks **34.843**
  LSB di `adx_m66_0020` (34.933.606 sampel ter-render). Selisih itu transient yang meluruh,
  bukan offset permanen.
- Seam `|x[loop_end] - x[loop_end-1]|` di titik sambung: Carry lebih kecil di **36/45** file
  (median 856 vs 1.803 LSB), sedangkan p99.9 step normal di dalam body = 25.913 LSB. Artinya
  **kedua** state jauh di bawah transient normal → tidak ada klik; Carry cuma sedikit lebih
  mulus. Linear di bawah p99.9 di 44/45, Carry di 45/45.

**4) Keputusan default (dan batasnya)**

Default port = **Linear**, karena satu-satunya yang byte-exact dengan decoder referensi
independen dan membuat body loop identik dengan pass pertama (persis yang dikodekan encoder).
`--loop-state carry` tersedia untuk eksperimen. Ini **pilihan referensi, bukan klaim hardware**:
library ADX asli yang dipakai game (`adxps2`/ADXPLAY di IOP) belum di-RE, jadi belum terbukti
apakah hardware membawa history atau re-seek. Selisih terukur 0,0093% sampel, jadi risiko
audible-nya kecil, tapi statusnya tetap "belum diputuskan oleh hardware".

**5) Detail implementasi yang tidak sepele**

- `loop_end` **tidak** frame-aligned di **28/45** file (contoh 6.163.980 = 192.624,375 frame),
  sementara `loop_start` aligned di **45/45**. Decoder yang maju per frame akan overshoot melewati
  titik lompat, jadi state tepat di `loop_end` direkonstruksi: `decode_frame()` sekarang bisa
  menangkap history setelah `hist_at` sampel, dipanggil ulang dari **snapshot state awal frame**.
- `render_loop()` memakai cache frame + snapshot entry state, jadi loop ke-2 tidak men-decode
  ulang dari sampel 0 (biaya render 45 file x 2 loop = 1,01 miliar sampel tetap ~40% dari wall run).
- Stream non-loop jatuh ke `decode_trimmed()`; `loops=0, tail=false` dipakai untuk prelisten.
- Sink WAV baru `adx_write_wav()` (RIFF, fmt 16, data s16le). Verifikasi: `ffprobe` pada hasil
  `adx_m1070_0000.wav` → `pcm_s16le 48000 Hz 2 ch`, durasi 2,770646 s = 132.991 sampel; decode
  balik lewat ffmpeg menghasilkan 531.964 B = 132.991 x 2 x 2 (cocok byte).

**6) Hasil run (angka nyata)**

5x berturut, 367 file, gate vgmstream + loop render (`--no-ffmpeg`, karena parity ffmpeg sudah
di-gate terpisah di bawah):

| run | exit | pass | vgmstream exact | loop render exact | sampel loop | wall |
|---|---|---|---|---|---|---|
| 1 | 0 | 367/367 | 367/367 | 45/45 | 1.013.813.378 | 5:22,97 |
| 2 | 0 | 367/367 | 367/367 | 45/45 | 1.013.813.378 | 5:13,84 |
| 3 | 0 | 367/367 | 367/367 | 45/45 | 1.013.813.378 | 9:19,86 |
| 4 | 0 | 367/367 | 367/367 | 45/45 | 1.013.813.378 | 10:31,93 |
| 5 | 0 | 367/367 | 367/367 | 45/45 | 1.013.813.378 | 4:58,89 |

- Body log 5 run **identik**: sha1 `1343b6bbf7b10659d808e56ee0ae0ab2e57fe384` (685 baris),
  baris SUMMARY sha1 `d5c79759fe3ad48c1c37f7aed7128f4a7024ba00`.
- VPS ini **1 vCPU / 2 GB**; wall run 3-4 hampir 2x karena verifikasi AdxStream berjalan paralel
  di core yang sama. Angka determinisme diambil dari isi log, bukan dari wall time.
- 1x run penuh (ffmpeg + vgmstream + synth), wall 929,23 s (sesi sebelumnya; log-nya tidak ikut
  di-commit, angka `trimmed_samples_total` di bawah ini yang mengonfirmasi): `pass=367 fail=0`,
  `ffmpeg_exact=367/367`, `vgmstream_exact=367/367`, `looprender_exact=45/45`,
  `synth_failures=0`, `trimmed_samples_total=572.592.917`, body sha1 `0765447d3b166fca`.
  → tidak ada regresi di jalur lama; `decode_frame` default behavior tidak berubah (parameter
  history capture opsional, default off).
- Sampel rate title: 359 stream 48 kHz, 8 stream 24 kHz (belum di-resample; lihat "belum").
- Reproduksi:
  `./build/adx_test --loop-render --loop-state linear --loops 2 --wav /dev/shm/ghwav --synth
  --vgmstream /root/.hermes/tools/vgmstream/vgmstream-cli`

**7) Yang belum (jujur)**

- Sumber kebenaran loop di **hardware**: `adxps2`/ADXPLAY di IOP God Hand belum dibedah, jadi
  Carry vs Linear belum diputuskan oleh mesin aslinya (default sekarang = referensi).
- Mixing/DSP, sound RPC (`sid` 0x80000006/0x80000592), dan output SPU2 belum ada.
- Resampling 24 kHz → laju output game belum ada (8 stream).
- Audio non-ADX belum disentuh: MPEG/SND di AFS dan audio dalam SFD/PSS.
- Encoding type 2/4 hanya terbukti di vektor sintetis (title ini 100% type 3).
- `render_loop` mengalokasikan output penuh (loop 2x file terbesar = ~140 MB); untuk streaming
  runtime perlu versi ring-buffer per frame, bukan vektor penuh.
