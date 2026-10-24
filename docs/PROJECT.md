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
- [~] Phase 9 — boot native sampai render loop (LOG #18 + #19): end_func SIF0 aktif, CDVFS open
      4/4 dari ISO9660 mentah, **Read_EE (SID 0x80000595 fno=1) hidup** — 2 baca nyata dari ISO
      (IRX.IMS lsn 2175×154, 0FLISTDV.DIR lsn 755203×2, 319.488 B) + `SifDmaStat` model antrean.
      Dinding sekarang: ADXF state byte (handle+1) tetap **2 "read issued"**, belum 3 "read end";
      transisi 3 ada di 0x323C58 (kondisi v0==0) dan tidak tercapai — berikutnya: jalur completion
      ADXF (callback 0x3A8580 + ctx 144 B @0x3F9740) sebelum VU1/render disentuh.
- [x] Phase 10 — scheduler thread EE (LOG #20): 5 thread ADXF hidup, RPC 9.118 call, nol fault.
- [~] Phase 11 — varian syscall `i` (LOG #21): `iSignalSema` = `-67` (wrapper ELF `0x3afcd0`),
      dulu hilang di stub → `fd+1` tak pernah balik. Sekarang **status fd mencapai 3**
      (`0x3285E4` @inst 44,80 M), ADXF `0x323A00`/`0x323AA0` **jalan pertama kali**, dan Read_EE
      maju berurutan (LSN 2175×154, 755203×2, 2329..2334, 755204×17, 8120×2 = 10 read / 370.688 B;
      dulu 2.255× retry LSN 2329). Dinding baru: loop RPC `0x80000593` fno 12 (4.330×) +
      `0x19720317` fno 2 (3.304×); `0x323F18`/`0x324008` (service main loop) masih 0 instruksi.
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

### LOG 2026-10-24 #17 (Phase 9 lanjut: ABI sema EE dibetulkan — dinding CRI/ADX dipetakan, frontir baru = CDVD)

**1) Dinding lama dijelaskan sampai akar (semua terukur, bukan dugaan)**

- Loop 400 M–2 B instruksi **bukan hang acak**: 2,3 M panggilan `strlen` + 71 printf/iterasi
  adalah **log error CRI yang dicetak terus-menerus**, bukan kerja render. Trace ring 60 K PC:
  siklus `0x1FFCC8 → 0x395398 (vsync) → 0x3244C8 (adxf_GetStat) → bne status,3 → ulang`.
- Register saat halt: `s1=0` → handle ADXF **NULL**; status handle (byte+1) = 4 ditulis di
  **0x3289C4** setelah open-callback **0x330780** (cvFsOpen) balik 0.
- Pesan aslinya ada di buffer log game 0x3EADC0 (dump `GH_DUMP_MEM`):
  `E02110501 adxstmf_stat_exec: can't open GodHand.fst` — nama file di .rodata **0x42C6C8**.
- Jadi wall = **stream CRI tidak bisa buka file**, bukan masalah MMI/SJUNI (yang sudah lewat).

**2) BUG ABI NYATA: sema EE mengembalikan 0, seharusnya id**

- Bukti di libcdvd: `0x3988D0 lw v1,[0x3F8668]` (id sema) → `jal PollSema` → `0x3988E0
  bne v1,v0` = error kalau beda. Dengan return 0, cabang error **selalu** diambil.
- Fix `src/shim/ee_kernel.cpp`: WaitSema/SignalSema/PollSema `return id`.
- Efek terukur: boot **masuk protokol CDVD** (sebelumnya cuma "lewat" karena sema bohong).

**3) SID server salah (dibetulkan, ada bukti di modul)**

- `CDVD = 0x80000592` (konstanta lama 0x80000005 tak pernah dipakai game). Bukti klien:
  bind di **0x3990C8**, call fno 0 (4 B → 16 B) di **0x399168**; balasan 16 B sekarang ditulis
  (dulu 4 B). fno 0x14 (SCECdPS2DVD) dicoba → boot **malah mundur**, jadi word[3]=0 dipertahankan.
- `0x80000001` = **libsd.irx** (bukan FILEIO) — ditemukan dari byte-scan modul; handler fileio
  selama ini melayani panggilan libsd (fno 255 → -1).

**4) Infrastruktur baru: end-func RPC (default MATI)**

- Di HW, `_request_end()` memanggil `end_func(end_param,result)`. Ditambah
  `eecpu::request_callback` + antrean + save/restore konteks (`GH_RPC_ENDFUNC=1` untuk hidup).
- Percobaan pertama: `end_func=0x00398580` dipanggil, tapi `sp` jadi `0x023F86F0` (di luar RAM
  32 MB) → model interrupt belum benar. **Dimatikan default**, jangan dipakai sampai tervalidasi.

**5) Frontir sekarang (jujur, belum selesai)**

400 M berhenti di `pc=0x003B0AF8` lewat `0x398DD8`: `while ([0x3F8674] != 0) delay(4000)` —
flag diset di **0x39987C** sebelum RPC async ke sid **0x80000595**, dan hanya dibersihkan oleh
jalur completion yang belum kita emulasi. Konsekuensi jujur: `loadfile load=0` — modul IOP
**belum** dimuat lagi di jalur yang benar (jalur "9 modul" sebelumnya lolos karena sema salah).
`gif kicks` tetap 16.384, `actions=0`. Berikutnya: layanan **CDVD** (0x80000592/593/595/597).

**6) Test floor 5× berturut (md5 stdout identik)**

`sif_test 36/36` · `sif_dma_test 10/10` · `shim_test ALL` · `bus_test ALL` · `mmi_test 25/25` ·
`gif_test ALL` · `adx_test` (afsdump ISO) rc=0 · `crt0_test 30M` md5 `58507a7c…` ·
`crt0_test 400M` md5 `f5e595a9…`, stop `clean-cap`, nol fault.

**7) Tool baru:** `tools/peek_vma.py` (dump VMA→offset via PT_LOAD) untuk baca string/tabel ELF.

### LOG 2026-10-24 #18 (Phase 9: end_func SIF0 = kunci dinding CDVD; CDVFS open dari ISO nyata)

**1) Dinding lama (0x3F8674) = end_func RPC yang tidak pernah dipanggil**

- Rutin libcdvd 0x398DD8: `lw v0,[0x3F8674] / bnez -> delay(4000)`. Flag di-set 0x39987C tepat
  sebelum `sceSifCallRpc(cd=0x3F9810, fno=1, mode=1 async, ..., end_func=0x398580, end_param=0x3F9740)`.
- Path pembersihnya ada di game sendiri: end_func 0x398580 (copy 2 blok dari paket) → `j 0x3984D8`
  → `sw zero,[0x3F8674]`. Yaitu **jalur _request_end/SIF0**, bukan pekerjaan IOP.
- Infra callback sudah ada di ee_cpu, tapi mati karena fault: `fault=pc_oor @0x398580 op=jr ra`.
  Sebabnya cek akhir-loop `if (c.pc >= 0x02000000 || c.next_pc >= 0x02000000)` menabrak sentinel
  kembali 0xFFFFFFFC. Fix: kecualikan sentinel saat callback aktif (dua bentuk: pc == sentinel
  sesudah delay-slot, next_pc == sentinel saat `jr`), plus `GH_HALT_SKIP`.
- end_func sekarang HIDUP default (`GH_RPC_ENDFUNC=0` mematikan).

Bukti (crt0_test, angkanya dari run nyata):

| Metrik | Sebelum | Sesudah |
|---|---|---|
| Flag 0x3F8674 | 1 (spin 7,1 M iterasi) | 0 |
| loadfile modul IOP | 0 | **9** (sio2man 5248 · padman 45952 · libsd 30144 · SDRDRV 9216 · sdhd 14720 · snddrv 20352 · cri_adxi 76928 · mcman 103680 · mcserv 7744) |
| bind / call RPC | 6 / 6 | 13 / 96 |
| pc akhir 200 M | 0x3B0AF8 (spin) | clean-cap, tanpa fault |

**2) ISO9660 mentah + backend CDVFS (bukan hasil ekstraksi)**

- `src/shim/iso9660.{h,cpp}`: PVD sector 16, root record, walk direktori, `;1`, nama
  case-insensitive, traversal ditolak. `iso_test` **18/18 PASS 5×** — isi file **byte-identik**
  dengan hasil ekstraksi: SYSTEM.CNF 57 B, GODHAND.FST 32.944 B, IRX.IMS 314.304 B,
  SLUS_215.03 3.607.008 B; `_DATA.AFS` 1.541.885.952 B (lba 2329).
- SID **0x80000597 = cdvdfsv.irx** — dibuktikan dari modul IOP asli: `IOPRP310.IMG` →
  `m09.elf` (33.440 B) mem-bind 0x592/0x593/0x595/0x597; handler server di **0x7AC** melakukan
  dispatch menurut **panjang paket** (300 B = open), nama file NUL di **+36**, self-pointer EE di
  **+292**, mode di **+296**, lalu menyalin struct hasil **36 B** ke [req+292] (DMA IOP→EE) dan
  reply 4 B = nilai balik.
- Implementasi: `sif_rpc` handler `cdvfs_handler` + `ee_writeback()` (DMA balik IOP→EE) yang
  dieksekusi `sif_dma` setelah handler. Field yang TERBUKTI dibaca klien: info+4 = ukuran
  (`cvFsOpen` 0x332C84 `lw a1,36(s1)`), dipakai juga sebagai nilai balik.
- Bukti boot 200 M: `cdvfs: open_ok=4 open_miss=0` — `\MODULES\IRX.IMS;1`, `\0FLISTDV.DIR`,
  `\_DATA.AFS;1` (2×). Sebelumnya semuanya MISS → `cvFsOpen #6: can not open file`.

**3) Dinding baru (jujur, belum selesai)**

- Boot 200 M berhenti di overlay yang di-TPA game ke **0x1FFC00**; loop 0x1FFC30:
  `jal 0x395398 (vsync/stat)` + `jal 0x323000 (adxf_GetStat)` → `bne v0,s0` (v0=2, s0=3),
  ~2,1 M iterasi + 2,15 M syscall 115.
- Status ADX = **2**, belum 3 (ready). Buffer log CRI 0x3EADC0 kini **kosong** — error
  "can't open GodHand.fst"/"adx is NULL" hilang, jadi open file sudah benar; yang kurang adalah
  jalur **baca** data stream.
- Belum dipetakan: SID 0x80000595 **fno=1** (paket 24 B: `{0,0,0,0x100,0x3F9740,0x3F9800}`)
  dipanggil 2×; masih dijawab autostub nol = kandidat utama pembaca sektor/"Read_EE".
- Anomali dicatat: 3 call ke SID **0x19720317** (bukan SID valid) berasal dari paket BIND yang
  field sid-nya 0 (client data belum terisi) — perlu ditelusuri, belum diklaim sebagai fitur.
- Catatan flake: `shim_test` (thread ping-pong) menggantung 1× di batch 180 s; 5 run berikutnya
  PASS identik md5 cc2ea3b0. Diamati, belum diperbaiki.

**4) Test floor 5× (md5 stdout identik, angka nyata)**

`sif_test 41/41` (md5 73372ce0) · `sif_dma_test 10/10` (9e2b870b) · `shim_test ALL` (cc2ea3b0) ·
`bus_test 20/20` (a06a36af) · `mmi_test 25/25` (accd196e) · `gif_test ALL` (ade54fea) ·
`iso_test 18/18` (e8332a85) · `crt0_test 30M` (12465aeb) · `200M` (164dba8f) · `400M` (ac7a6f91),
semua `stop=clean-cap`, nol fault.

**5) Commit:** `505c972` (end_func) + commit ini (iso9660 + cdvfs).

### LOG 2026-10-24 #19 (Phase 9: Read_EE 0x80000595 hidup — CDVD baca sektor nyata; LSN CdlFILE dibetulkan)

**1) Server SID 0x80000595 dipetakan dari modul IOP asli, bukan tebakan**

- `extract/modules/ioprp/m09.elf` (cdvdfsv.irx, 33.440 B): registrasi RPC di **FUN_000044a4**
  → `FUN_00004bc8(0x58e8, 0x80000595, FUN_00003c1c)`. Dispatcher `FUN_00003c1c`
  (string `sce_cdvd N cmd start %d`) memakai **jump table .data 0x53d0** (19 entri, disasm
  objdump `-m mips:isa32`): **fno 1 → 0x3CB4 → FUN_00001dc4**, fno 14 → 0x3F58 (cek drive).
- Paket 24 B (dibangun klien EE 0x399728, dipanggil 0x331830 & 0x333808):
  `+0 lsn, +4 nsec, +8 dst_EE, +12 mode[3]+pad, +16 ctx_EE (144 B, = end_param), +20 out_EE (4 B)`.
  Ukuran sektor dari `mode[2]`: 1 → 2328, 2 → 2340, selain itu 2048 (0x1E98..0x1EE4).
- IOP asli menyalin ke buffer internal lalu **DMA dua kali** (0x2630..0x2684):
  `{0x5780 → ctx_EE, 144 B}` = `{prefix, trail, dst, dst+prefix+main}` dan
  `{0x5a18 → out_EE, 4 B}` = jumlah byte terkirim. Native: baca ISO langsung (`iso9660::read_sectors`)
  lalu tulis ke RAM EE di dua titik yang sama (`cdvd_ee_handler`).

**2) Bug besar yang ketemu karena A/B, bukan opini: field +0 struct open = LSN, bukan flag**

| Layout struct 36 B | Hasil Read_EE di boot nyata |
|---|---|
| `+0 = 1 (flag), +4 = size, +8 = lba` (lama) | **lsn=1 nsec=154** → membaca *system area* ISO, bukan file |
| `+0 = lba, +4 = size` (CdlFILE: `u32 lsn`) | **lsn=2175 nsec=154** = `\MODULES\IRX.IMS;1` (iso_test: lba 2175, 314.304 B) |

Bukti silang: baca kedua `lsn=755203 nsec=2` = `\0FLISTDV.DIR;1` (iso_test lba 755203, 23 B).
Jadi ADXF mengambil LSN dari **+0**; "file ada" dibawa nilai balik RPC (= ukuran), bukan field.
`GH_CDVFS_LEGACY=1` memulihkan layout lama untuk reproduksi.

**3) `SifDmaStat` (syscall 118) salah kontrak → loop abadi 15,7 M syscall**

- Kode game 0x2BE3D8: `while (SifDmaStat(id) < 0) ;` lalu lanjut memakai buffer hasil DMA.
  Implementasi lama mengembalikan **-1 selamanya** → run 200 M menghabiskan 15.693.109 syscall 118,
  boot mati sebelum loadfile. Model baru (antrean nyata): poll pertama setelah kick = **0**
  (entri masih di antrean), poll berikutnya = **-1** (pensiun). Hasil: syscall 118 = **8**.
- `sif_dma_test` diperbarui ke kontrak itu (10 → **11/11 PASS**).

**4) Angka boot 200 M (run nyata, 5× identik)**

| Metrik | LOG #18 | Sekarang |
|---|---|---|
| `cdvfs open` | 4 ok / 0 miss | 4 ok / 0 miss |
| `cdvd-ee Read_EE` | 0 (autostub) | **2 ok / 319.488 B / 0 fail** (fno lain: 2 = cek drive) |
| SIF DMA | 98 kicks / 13.340 B | **114 kicks / 204 entry / 251.068 B ee2iop**, iop2ee 144 B |
| RPC bind/call | 13 / 80 | **13 / 88** (payload 7.244 B, reply 860 B) |
| syscall 118 | 0 (jalur tak tercapai) | 8 |
| Modul IOP dari disk | 9 (jalur loadfile) | 1 (`cri_adxi.irx`) + **8 panggilan loadfile fno=6** (payload 512 B, word0 = alamat IOP 0x130000/0x131480/0x13C800/0x143DC0/0x1461C0/0x149B40) masih autostub |
| Titik akhir 400 M | 0x3B0AF8 | **0x323070** (loop ADXF status) |

**5) Dinding baru (jujur, dengan alamat persis)**

- Handle ADXF di 0x3EAEC8; **status byte = handle+1**. Nilai yang terukur: 1 (init) → **2** (store
  `sb 2,1(s0)` di **0x323C8C**, artinya "read issued") → 3 hanya di **0x323C58** (jalur `v0 == 0`,
  "read end"). Watchpoint 0x3EAEC0..0x3EAEE0 merekam semua tulisan: **tidak ada store 3**.
- Total sample ADX terisi (0x3EAED4 = 0x000FFFFF, 0x3EAED8 = 0x7FFFF800), jadi header stream
  terbaca — yang tidak jalan adalah transisi completion setelah Read_EE.
- `adxf_GetStat(0)` = 0x323000 → 0x324500 = `lb v0,1(handle)`; loop pemanggil 0x1FFC30
  (`jal 0x395398` vsync + `jal 0x323000`) membandingkan dengan 3. 400 M tetap di 0x323070.
- Berikutnya (urut): (a) jalur completion ADXF — callback `end_func=0x3A8580` + isi ctx 144 B
  @0x3F9740 (yang kita tulis `{prefix, trail, dst, dst+main}`); (b) 8× loadfile fno=6
  (LoadModule dari buffer IOP) supaya modul yang sudah di-DMA game benar-benar "dimuat";
  (c) baru VU1/render (Phase 7c).

**6) Test floor 5× (md5 stdout identik, semua PASS)**

`sif_test 41/41` 73372ce0 · `sif_dma_test 11/11` 435566a1 · `shim_test ALL` cc2ea3b0 ·
`bus_test 20/20` a06a36af · `mmi_test 25/25` accd196e · `gif_test ALL` ade54fea ·
`iso_test 18/18` e8332a85 · `crt0_test 30M` 68f69964 · `200M` 7b0a1819 · `400M` a8e98443,
semua `stop=clean-cap`, nol fault.

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

### LOG 2026-10-24 #20 (Phase 10: scheduler thread EE — 5 thread ADXF jalan, Read_EE 2.257 baca / 4,9 MB)

**Akar masalah yang ditemukan (bukan tebakan, dari A/B run):** `StartThread` (syscall 34) **nol**
di seluruh boot sebelumnya. Wrapper game `0x3B0BA8` menolak tiap thread karena cek
`0x3B0BC0 → 0x3B06C8` = `(SR & 1) ^ 1` → SR.IE=0, lalu `0x3B0BD0` DIntc juga melihat EIE
(bit16) mati. Jadi 5 thread ADXF (entry `0x324EB0`, `0x324F08`, `0x325010`, `0x325100` + sound
`0x3B0878`) tidak pernah hidup → tidak ada yang mengirim Read_EE dari jalur ADXF.

**Yang dibangun (kode, bukan catatan):**
- `ee_cpu`: scheduler kooperatif. `EeThread{pc,npc,r[32][4],lo,hi,f,sa,facc,fcsr,inst}`,
  switch **di batas instruksi murni** (sebelum fetch) supaya pasangan `(pc,next_pc)` utuh.
  Percobaan pertama menyimpan `resume=c.pc` lalu restore `next_pc=pc+4` → target branch di
  delay-slot hilang, terukur `memset` 0x3A5364 jatuh ke loop byte dengan `a2=0xFFFF8310`
  (4,29 miliar iterasi, seluruh RAM dinolkan). Diperbaiki dengan menyimpan `pc/npc` apa adanya.
- Timeslice `GH_SLICE` (default 20.000 instr) + `thread_yield_if_other()` untuk WaitSema/
  SleepThread. Gate `GH_PREEMPT_IE=1` (preempt hanya saat IE=1) **terukur lebih buruk**:
  89,8% instruksi boot berjalan dengan SR.IE=0 (`g_ie0_inst`) → preemption mati, ADXF 48 baca
  di 400 M vs 2.257 tanpa gate. Default: tanpa gate.
- **Stack per-thread** dari arena port `0x01D00000 + tid*64K`: 5 CreateThread game semuanya
  mengirim `stack=0x468CF0` dengan ukuran 1.024/2.048/4.096/8.192 → kalau dipakai apa adanya
  dua thread berbagi puncak stack (terukur `jr ra` mendarat di `0x3EC7E0` = .bss, fault
  SPECIAL fn 1). Setelah per-thread stack: nol fault.
- `CreateThread` param dibaca sesuai layout **terukur** di call site `0x3253C0`/`0x3B09B8`:
  `+4 entry, +8 gp, +0xC stackSize, +0x10 stack, +0x14 prio` (layout lama salah: prio/stack
  tertukar → prio 7854992).
- Syscall 32/33/34/35/36/44/47/48 nyata: `ReferThreadStatus` menulis `+0 status=0x10 (WAIT)`,
  `+8 stack`, `+0xC size` (field yang dibaca `0x3B0C20`), `DeleteThread`/`ExitDeleteThread`
  membebaskan slot, `CreateThread` mendaur ulang slot thread yang selesai (tanpa ini 7 slot
  penuh → thread per-request gagal dibuat).
- Alat diagnosa baru: `GH_DUMP_END` (dump memori pasca-run), `GH_PROBE_PC/GH_PROBE_REG/
  GH_PROBE_MIN` (halt bersyarat: pc == X dan reg >= Y), `GH_SWITCH_LOG`, census `[ie]`,
  `[sched]` per-thread (entry/pc/inst).

**Angka nyata `crt0_test 400 M` (5× md5 identik `761f3c72`):**

| Metrik | LOG #19 | Sekarang |
|---|---|---|
| `cdvd-ee Read_EE` | 2 ok / 319.488 B | **2.257 ok / 4.937.728 B / 0 fail** |
| SIF DMA | 114 kicks / 204 entry | **9.146 kicks / 11.493 entry / 887.492 B** |
| RPC bind/call | 13 / 88 | **13 / 9.118** (payload 61.364 B, reply 27.960 B) |
| end_func dieksekusi | 2 | **2.257** |
| syscall total | 977.358 | **1.499.973** (38 nomor, 34 StartThread **5**) |
| Thread hidup | 0 | **5** (inst: t0 160,6 M · t2 177,9 M · t3 28,6 M · t4 24,8 M · t5 7,9 M) |
| Titik akhir 400 M | 0x323070 (poll ADXF) | 0x324E98 (loop idle thread ADXF) |

**Floor 5× (md5 stdout identik, semua PASS):** `sif 41/41` 73372ce0 · `sif_dma 11/11` 435566a1 ·
`shim cc2ea3b0` · `bus 20/20 a06a36af` · `mmi 25/25 accd196e` · `gif ade54fea` · `iso 18/18 e8332a85` ·
`crt0 30 M 0408818b / 200 M 749001f7 / 400 M 761f3c72`, semua `stop=clean-cap`, nol fault.

**Belum (jujur, dengan alamat):** ADXF state byte `handle+1` (handle `0x3EAEC8`) tetap **2**;
`+0x14` (offset) tetap 0 → sektor **2329** dibaca berulang (380× di 100 M; hanya 3 LSN unik:
2175 IRX.IMS, 755203 0FLISTDV.DIR, 2329×N). Tiga fungsi layanan yang seharusnya memproses
completion **tidak pernah dieksekusi** (probe window, 60 M): `0x323AA0` (dispatch status==2 →
`0x323F18`), `0x323B58` (service semua handle → `0x323B80`), `0x324008`/`0x324040` (dipanggil
main loop game di `0x200F9C`/`0x201048`, belum tercapai). Berikutnya: panggil/completion ADXF —
`0x323F18` menghitung `0x328140(fd) - [h+0x14]`, menyetel `[h+0x20]`, lalu `0x323A00` menaikkan
offset; setelah itu baru status 3 → VU1/render.

**Addendum diagnosa (setelah commit 8750803, alat baru GH_HIST_EXACT/GH_COVER_RANGE):**
- Store `handle+1 = 2` terjadi **380×** di 100 M, dan **380 di antaranya datang dari pc
  `0x324140`** — bukan dari `0x323C8C`. `0x324108` = fungsi update ADXF: kalau `h[1]==2`
  ia menyalin `fd_status = 0x328098(fd)` ke `h+1`. Artinya ADXF **sudah** memanggil jalur
  read berulang, dan yang macet adalah **status fd object** (0x3D0DD0 +1) yang tidak pernah
  kembali ke 1 setelah read selesai — bukan lagi masalah sisi ADXF.
- `0x328368(fd)` (wait-idle) menunggu `fd+1 == 1`; `fd+0x24` = 0 (nol pending) tapi `fd+1`
  tetap 2 → tidak ada yang menandai completion di level objek file.
- `end_func` yang benar-benar dipanggil selama ini = **0x398580** (507/507 di 120 M), dan
  fungsi itu membaca `ctx[0]` sebagai **jumlah byte** lalu menyalin dari `ctx+16`, bukan
  layout `{prefix,trail,dst,boundary}` yang kita tulis. Kandidat kuat berikutnya: layout
  ctx 144 B untuk jalur ini salah, sehingga handler completion tidak pernah menandai selesai.

### LOG 2026-10-24 #21 (Phase 11: iSignalSema `i`-variant = akar status fd; ADXF completion AKHIRNYA jalan; ctx-144B falsified)

**Akar dinding LOG #20 (bukan tebakan):** status fd object `0x3D0DD0+1` tak pernah balik ke 1 karena
satu-satunya pemicunya — `iSignalSema` — dipanggil lewat **nomor syscall negatif**. Wrapper di ELF
`0x3afcd0` = `addiu v1,zero,-67 ; syscall` (byte `bd ff 03 24 0c 00 00 00`). Shim lama membandingkan
`num` sebagai u32 → `0xFFFFFFBD` → jatuh ke stub generik (`v0=0`, tanpa efek). Jadi 1 signal hilang
per read; `WaitSema` cuma "selamat" karena model autosig Phase 7b.9, dan itu yang membuat loop
mengulang LSN 2329 **2.255×** tanpa pernah maju.

**Yang dibangun:**
- `ee_cpu` SYSCALL: normalisasi **whitelist** varian `i` (`-67 → 67 iSignalSema`, `-49 → 49
  iReferThreadStatus`) + census `[sysdbg]` untuk negatif tak dikenal (jangan balik semua negatif:
  `-68` akan memetakan ke WaitSema yang memblokir). Gate A/B `GH_NO_IVAR`.
- `crt0_test` sys 49 = `iReferThreadStatus` (kontrak sama dengan 48: isi struct, bukan cuma `v0=0`).
- `ee_cpu` INTC nyata: sys 16 `AddIntcHandler` menyimpan handler, `intc_dispatch(2)` tiap VSync
  virtual (60k instruksi) → tick frame `0x30B148`. Gate A/B `GH_NO_INTC` (terukur: **tidak**
  mengubah jumlah read; effect-nya di jalur frame, bukan CD).
- Callback end-func dipindah ke **batas instruksi sebelum fetch** (`pc/next_pc` utuh, bukan delay
  slot) + `cur_pc` ikut end_func supaya trace/watch tidak meleset.
- `sif_rpc` ctx 144 B: byte head/tail ikut diisi dari data nyata (idempoten kalau rutin 0x398580
  menyalinnya). Gate `GH_CTX_FLAT`.
- Diagnosa baru: `GH_TOP=<n>`, `GH_NO_IVAR`, `GH_NO_INTC`, `GH_STUB593=<n>`, `[sysdbg]` negatif.

**Angka nyata `crt0_test 400 M` (5× md5 identik `fc52f27f`):**

| Metrik | LOG #20 (HEAD `8750803`) | Sekarang |
|---|---|---|
| Read_EE | 2.257 ok / 4.937.728 B — **2.255 di antaranya retry LSN 2329** | **10 ok / 370.688 B, LSN maju:** 2175×154, 755203×2, **2329..2334**×1, 755204×17, 8120×2 |
| `fd+1` status | tetap 2 (tak pernah 3) | **mencapai 3** — `0x3285E4` dieksekusi di inst 44.802.183 |
| `0x323A00` (ADXF notify) | 0 instruksi sepanjang 400 M | **44.802.613** (pertama kali) |
| `0x323AA0` (dispatch status==2) | 0 instruksi | **45.415.202** (pertama kali) |
| `end_func 0x398580` | 2.257 (jalur callback) | **0** — read kini selesai sinkron; jalur callback tak lagi dipakai |
| RPC call | 9.118 | 8.406 |
| INTC | handler 0 / fired 0 | **3 handler, 5.922 fired / 6.665 frame** |
| GIF kicks | 18.689 | 18.689 (sama — belum render) |
| Titik akhir 400 M | 0x324E98 (idle ADXF) | 0x324E9C (idle ADXF) |

**Falsified (jujur):** hipotesis "layout ctx 144 B salah" dari addendum #20 **salah untuk stall ini**.
Disasm `0x398580`: `[ctx+0]`=head_len → salin dari `ctx+0x10` ke `[ctx+8]`; `[ctx+4]`=tail_len →
salin dari `ctx+0x50` ke `[ctx+0xC]`; lalu lompat `0x3984D8` (simpan hasil + `iSignalSema`). Dump live
`0x3F9740` = `{0,0,0x3C5300,0x3C5B00}` → head_len=tail_len=**0** → nol byte disalin, nol korupsi.

**Dinding baru (dengan bukti):** setelah 10 read, game masuk **loop RPC**, bukan render:
- `sid=0x80000593 fno=12` **4.330×** (send=0, reply 4 B) — server tak punya backend (autostub).
  Bind-nya di `0x398F7C` (klien `cd=0x003FA1C0`), pola identik dengan bind cdvd_ee `0x398C7C`.
  A/B isi balasan `GH_STUB593=0/1/2/3` → **nol perubahan** (hipotesis "poll butuh 1" salah).
- `sid=0x19720317 fno=2` **3.304×** (payload 64 B `{0,0x76A9C0,0x76B1C0}`) — SID sampah hasil bind
  di `cd=0x0076A95C`; kuat dugaan SID dibaca dari IRX yang kita "load" tanpa eksekusi.
- `0x323F18`, `0x324008`/`0x324040` (service ADXF dari main loop `0x200F9C`/`0x201048`) tetap
  0 instruksi (probe 400 M).
- `loadfile fno=6` masih autostub (8 panggilan), modul termuat = 1 (`cri_adxi.irx` 76.928 B).

**Floor 5× (md5 stdout identik, semua PASS):** `sif 41/41` 73372ce0 · `sif_dma 11/11` 435566a1 ·
`shim cc2ea3b0` · `bus 20/20 a06a36af` · `mmi 25/25 accd196e` · `gif ade54fea` · `iso 18/18 e8332a85` ·
`crt0 30 M b2c37ecb / 200 M 3b9380b0 / 400 M fc52f27f`, semua `stop=clean-cap`, nol fault.

### LOG 2026-10-24 #22 (Phase 12: trafik GS NYATA akhirnya ter-decode — 50.710 tulis register; instrumentasi provenance)

**Instrumentasi baru (diagnosa ke stderr; stdout tetap deterministik):**
- `rpc-census` per `(sid,fno)` — hitungan + payload/reply terakhir + `cd` + pc; `rpc-caller` (ra syscall 119);
  `sys-pc` (`num@pc=n(ra)`) — provenance jalur RPC tanpa tebakan.
- `mmio-write` census alamat MMIO yang ditulis + `gif-writer` (pc penulis CHCR/MADR/QWC/TADR).
- `[stack] tN pc sp` — unwind kasar: return address `.text` di 0x200 B pertama stack tiap thread.
- `GH_POKE="addr:val:at[,…]"` — poke 64-bit terjadwal (A/B gate flag); `GH_PCCOUNT` + `GH_HIST_EXACT=1` (hit eksak).
- **Loader ELF generik** — baca ELF32 header, muat semua `PT_LOAD`, `memsz` = bss, entry dari header.
  Retail `SLUS_215.03` dan proto JP `SLPM_665.50` dua-duanya `entry=0x100008` (ukuran tak lagi dipaku).

**A/B gate thread ADXF (`0x324EB0` = entry t2):**
```
0x324EB0: if (*(u64*)0x3C8158) goto done;          // flag berhenti
0x324ED8: loop { counter(0x3C8100)++; if (*(u64*)0x3D0000) break; }
done:     *(u64*)0x3C8160 = 1;                     // ack
```
| Poke @inst 20 M | Hasil terukur |
|---|---|
| `0x3D0000 = 1` | **nol perubahan** (400 M metrik identik) → hipotesis gate **falsified** |
| `0x3C8158 = 1` | t2 **exit** (`exits=1`), t0 pindah `0x14FEC0` → `0x2B4BC8`, RPC 593/12 350 → 618 |
- Penulis flag: `0x3257C8` (set 6 flag `0x3C8158..0x3C81A8`=1) **0 eksekusi** sepanjang 400 M; teardown
  `0x325B90` juga **0**. Yang jalan hanya `0x3252F8` (status→DeleteThread) **5.402×**. Jadi worker ADXF
  memang idle menunggu `0x3D0000`; game tidak pernah memanggil init/stop yang kita kira.

**TEMUAN UTAMA — GS sudah bicara, parser kita yang menolak:**
- 16.384 kick pertama (`0x395780`) = paket 96 B (QWC=6) hampir nol (bukan geometri).
- Mulai **kick #16385**, helper `0x396DC0` mengirim paket 368 B (QWC = header+1 = 23): tag
  `{nloop=22, eop=1, pre=0, flg=PACKED, NREG=0, REGS[0]=0xE (A+D)}` + 22 record 16 B `{data, addr}`.
- Alamat yang terbaca: **0x4C FRAME_1 · 0x4E ZBUF_1 · 0x18 XYOFFSET_1 · 0x40 SCISSOR_1 · 0x1A PRMODECONT ·
  0x45/0x46/0x47 (TEXA/FOGCOL/SCANMSK)** — register GS sungguhan; 400 M: **2.305 paket / 50.710 record /
  0 alamat di luar 0x7F** (diukur via counter fallback sebelum fix).
- **Fix:** `gif::parse_stream` — bila `flg=PACKED & NREG=0` dan tafsir 16-reg **tidak muat** di sisa buffer,
  pakai `nreg = avail_qwords / nloop` (hanya kalau habis dibagi; paket valid tak tersentuh). Tag game ini
  menulis NREG=0 untuk 1 register — manual bilang 16, data bilang 1.
- Hasil: `gif: kicks=18.689 tags=18.689 actions=50.710 bad=0` (sebelumnya `actions=0 bad=2.305`).
  `gif_test` tetap 6/6 md5 `ade54fea` → semantik parser unit-test tidak berubah.

**VU1 belum tersentuh (census MMIO 400 M):** nol tulis ke `0x10005000` (VIF1) / `0x11000000` (memori VU1).
Yang ada: `0x1000A000/A010/A020/A030` (GIF) 37.379/35.074/37.378/2.306 · `0x1000F000` 5.388 (INTC) ·
`0x10009000` 4.611 · `0x1000E010/E020` 2.306 · `0x10009020/9030` 2.306/2.306. Provenance penulis blok
`0x10009000`/`0x1000E000`: **`0x31A5A0..0x31A5F0`** (satu fungsi, dipanggil dari frame `0x30AF78` →
`0x31A338`/`0x31A538`), pola {2 → +0x10 dan +0x20; 0 → 0x9020; data → 0x9030; 0x45 lalu 0x145 → 0x9000}.
Blok ini **belum dipetakan** di `ee_bus` (bukan GIF/VIF: alamat & pola beda) — dicatat sebagai kandidat
blok kontrol 4-register (sound/DMA), bukan diklaim.

**Proto JP bisa diboot sekarang** (`SLPM_665.50`): 30 M instruksi, `stop=clean-cap`, nol fault, tapi berhenti
di loop `FindAddress` (syscall **131 × 2.025.777**) memindai `0x80000000..0x80080000` = **IOP RAM** yang
tidak kita modelkan → retail & proto konvergen ke gap yang sama: modul IOP tak hadir di memori.

**Angka `crt0_test 400 M`:** Read_EE 10 ok / 370.688 B · RPC 8.406 call (593/12 = **5.988**, 19720317/2 = 2.305)
· GIF 18.689 kick / 151.319 qword / **50.710 aksi AD** / 0 bad · INTC 3 handler 5.922 fired · VU0 macro 29.307
· sched 57.370 switch (t0 167,7 M · t2 177,9 M).

**Floor 5× hijau (md5 stdout identik):** sif `73372ce0` · sif_dma `435566a1` · shim `cc2ea3b0` · bus `a06a36af`
· mmi `accd196e` · gif `ade54fea` · iso `e8332a85` · crt0 30 M `5ac7275e` / 200 M `c5c11e24` / 400 M `7c44ed47`
· proto JP 30 M `da34a247`, semua `stop=clean-cap`, nol fault.

**Belum (urut kerja):** (a) petakan `0x10009000` & `0x1000E000` (2.306 tulis/paket — kandidat DMA/VIF);
(b) model memori IOP `0x80000000..0x80080000` (FindAddress/IRX lookup + loadfile fno=6);
(c) VU1/VIF1 = syarat geometri retail (belum satu tulis pun).

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
- `render_loop` mengalokasikan output penuh (loop 2x file terbesar = 17.753.672 frame x 2 kanal
  x 2 B = 71 MB, bukan ~140 MB seperti tulisan sebelumnya); untuk streaming runtime perlu versi
  ring-buffer per frame, bukan vektor penuh. → **ditutup di Phase 9c**. 

### LOG 24 Okt 2026 — Phase 9c: `AdxStream`, decoder tarik untuk runtime (blok arbitrer, memori konstan)

**1) Masalah**

Phase 9b mematerialkan seluruh timeline: `adx_m66_0000` dengan 2 loop = 17.753.672 frame x 2
kanal x 2 B = **71 MB** untuk satu lagu. Mixer tidak boleh begini — ia menarik 512–1024 frame per
tick. `AdxStream` (`src/shim/adx.h`) adalah API tarik: satu buffer frame 32 sampel (128 B stereo),
buffer output milik pemanggil, lompat loop O(1), dan **tanpa salinan payload ter-dekripsi**.
`timeline_samples()` memberi panjang total supaya runtime bisa memutuskan kapan berhenti.

Terukur (`tools/adx_stream_rss.cpp`, ikut di-commit): `adx_m66_0000`, file 7.168.000 B, timeline
17.753.672 frame x 2 kanal x 2 B = **71.014.688 B** kalau dimaterialkan — peak RSS proses
**11.108 KB**. Artinya timeline memang tidak pernah ada di memori; yang ada hanya byte ADX milik
pemanggil + buffer frame 128 B + buffer blok pemanggil.

**2) Kontrak yang dibuktikan, bukan diklaim**

`read(count)` untuk `count` = 33 dan 4096 frame (semua 367 file) serta 1 dan 7 frame (8 file
pertama) harus menghasilkan **byte identik** dengan `render_loop()` untuk ketiga state
(`linear`, `carry`, `reset`) — termasuk `read()` setelah timeline habis harus mengembalikan 0, dan
`timeline_samples()` harus sama dengan panjang render. Block 33 dipilih karena bukan pembagi 32
(memaksa sisa frame), 4096 karena itu bentuk blok mixer; 1 dan 7 menghajar cache per-frame dan
jalur offset. (Harness scratch juga menguji 256 frame — lulus, tapi angka run di bawah hanya
yang tercetak gate ini.)

**3) Dua hal yang tidak boleh salah, dan sempat salah**

- **State prediktor di titik lompat.** Versi pertama memakai `run_` (state di *akhir* frame yang
  sedang di-buffer). Untuk `loop_end` yang jatuh di tengah frame — 28 dari 45 stream — itu bukan
  state di `loop_end`. Gate menangkapnya persis di seam: `adx_m66_0004` beda pertama di sampel
  interleaved **26.363.070 = 13.181.535 x 2** (= `loop_end`), `adx_m66_0041` di **910.148 =
  455.074 x 2**. Perbaikan: `history_at_cursor()` merekonstruksi 2 tap dari sampel frame yang
  sudah ter-dekode (`entry_.h1` dipakai kalau offset = 1) — aritmetika yang sama dengan yang
  dilakukan `render_loop` waktu membangun ulang state dari snapshot frame.
- **Keystream type-8 saat lompat.** Keystream maju satu word per blok 18-byte. Kalau keystream
  tidak di-rewind, loop kedua didekripsi dengan state lanjut → sampah, dan `carry`/`linear` akan
  "beda" karena alasan yang salah. Solusi: snapshot `ls_ks_` (state sebelum blok frame
  `loop_start`) diambil sekali di pass pertama, dipulihkan di `jump_to_loop_start()`.
  Konsekuensinya `AdxStream` tidak perlu menyalin payload seperti `AdxDecoder::init()`.

**4) Hasil gate (5 run, semua log identik)**

Perintah: `./build/adx_test --no-ffmpeg --stream-check --loop-state auto`

| run | exit | file | stream exact | blok cocok | wall |
|---|---|---|---|---|---|
| 1 | 0 | 367/367 | 367/367 | 2.250 | 251,57 s |
| 2 | 0 | 367/367 | 367/367 | 2.250 | 248,93 s |
| 3 | 0 | 367/367 | 367/367 | 2.250 | 302,76 s |
| 4 | 0 | 367/367 | 367/367 | 2.250 | 331,20 s |
| 5 | 0 | 367/367 | 367/367 | 2.250 | 213,33 s |

- 2.250 = 367 file x 3 state x 2 blok (33, 4096) + 8 file pertama x 3 state x 2 blok tambahan
  (1, 7) → **11.250 pembandingan byte-exact** untuk 5 run, 0 beda.
- Body log 5 run identik: sha1 `87b4e0b4af5e102a18905519d8388fabb6cff6e5`.
- Timeline yang dilalui = timeline 9b: **1.013.813.378** frame per kanal (akumulasi 45 file loop).
- Potong timeline: `loops=0, tail=false` berhenti tepat di `loop_end` untuk **45/45** file loop
  (dipakai untuk prelisten satu putaran).
- Gate 9b tidak berubah di kode baru: jalur `decode_frame()` hanya dapat parameter tambahan
  `key_word` (default 0 = perilaku lama), dan `render_loop()` tidak disentuh.
- Satu run penuh regresi dengan kode 9c (ffmpeg + vgmstream + synth + loop-render + stream-check
  sekaligus): exit 0, `pass=367/367`, `ffmpeg_exact=367/367`, `vgmstream_exact=367/367`,
  `looprender_exact=45/45`, `stream_exact=367/367` (2.250 blok), `synth_failures=0`,
  `trimmed_samples_total=572.592.917`, `loop_pcm_samples_total=1.013.813.378`,
  wall **1782,09 s**, maxrss **928.452 KB**. Jadi parity ffmpeg/vgmstream tidak regresi karena
  API baru, dan biaya verifikasinya diukur (bukan "kira-kira 15 menit").

**5) Yang belum (jujur)**

- `AdxStream` belum men-resample: 8 stream 24 kHz keluar tetap 24 kHz. Konversi ke laju SPU2
  (48 kHz) belum ada, jadi belum ada klaim kualitas resampling.
- Belum ada upload ke SPU2 / mixing / DSP, belum ada `sid` sound RPC (`0x80000006`, `0x80000592`).
- Semantik loop di hardware tetap belum diputuskan (default = `linear`, referensi vgmstream);
  `carry`/`reset` tersedia sebagai opsi.
- Audio non-ADX (MPEG/SND di AFS, SFD/PSS) belum disentuh.

### LOG 24 Okt 2026 — Phase 9d: konversi laju 24 kHz -> 48 kHz (SPU2) + satu API tarik 48 kHz

**1) Masalah**

Phase 9c berhenti dengan catatan jujur: `AdxStream` belum men-resample, jadi 8 stream 24 kHz
keluar tetap 24 kHz. SPU2 hanya punya satu laju dasar (48 kHz pada basis 59,94 Hz NTSC); kalau
stream 24 kHz diumpankan apa adanya, hasilnya 2x cepat dan timeline-nya separuh. Phase 9d
menutup itu dengan konverter nyata, bukan pengambilan sampel terdekat.

Sebaran laju seluruh AFS (dihitung dari header, bukan diasumsikan): **359 file 48 kHz, 8 file
24 kHz**. Dari 8 itu, **7 file mono byte-identik** (`adx_m204/m208/m270/m328/m331/m360/m438_0000`:
md5 `730906bf0e2f0e7927e9534c459439a7`, 21.226 B, 37.632 sampel) dan satu stereo
`adx_m69_0000` (71.689 sampel). Artinya konverter hanya punya **2 sinyal unik** untuk diuji, dan
**tidak ada satu pun loop native 24 kHz** — uji loop+resample karena itu memakai stream 48 kHz
dengan target 24/96 kHz.

**2) Yang dibangun**

- `adx_resample_s16()` — one-shot, interleaved s16, windowed-sinc polifase.
  Default `taps=128` per fase, Kaiser `beta=8.6`, passband `0.97` dari Nyquist sumber,
  koefisien **Q23** (`int32`, akumulator `int64`), fase integer: rasio 24000:48000 direduksi
  jadi `up=2, down=1` sehingga **2 fase eksak tanpa kuantisasi fase**. Panjang output
  `ceil(in * up / down)`; 24k->48k berarti tepat 2x, bukan "kira-kira 2x".
- `AdxRateConverter` — versi tarik untuk mixer: `process()`/`flush()`, blok arbitrer,
  riwayat FIR dipangkas otomatis, `bypass` saat laju sudah sama (tanpa filter, tanpa salinan
  tambahan).
- `AdxPlayback` — satu pintu untuk runtime: `AdxStream` (decode + timeline loop) ->
  `AdxRateConverter` (laju), satu `read()` mengembalikan s16 pada 48 kHz. Ini yang akan
  dipanggil mixer, bukan dua kelas terpisah.
- `--resample-check` di `src/shim/adx_test.cpp`: enam gate, semuanya membandingkan angka.

**3) Hasil gate (5 run, semua log identik)**

Perintah: `./build/adx_test --resample-check`

Run pertama sesi penulisan gate (tabel A):

| run | exit | file | blok exact | playback exact | wall | maxrss |
|---|---|---|---|---|---|---|
| 1 | 0 | 367/367 | 1.468/1.468 | 367/367 | 188,20 s | 295.672 KB |
| 2 | 0 | 367/367 | 1.468/1.468 | 367/367 | 223,59 s | 295.744 KB |
| 3 | 0 | 367/367 | 1.468/1.468 | 367/367 | 183,78 s | 295.736 KB |
| 4 | 0 | 367/367 | 1.468/1.468 | 367/367 | 175,37 s | 295.724 KB |
| 5 | 0 | 367/367 | 1.468/1.468 | 367/367 | 198,47 s | 295.808 KB |

- **Verifikasi ulang (24 Okt 2026, 11:24–11:32 WIB)** — binary di-rebuild dari kerja terakhir,
  gate dijalankan 5x lagi dari nol:

| run | exit | file | blok exact | playback exact | wall | maxrss |
|---|---|---|---|---|---|---|
| 1 | 0 | 367/367 | 1.468/1.468 | 367/367 | 223,63 s | 295.832 KB |
| 2 | 0 | 367/367 | 1.468/1.468 | 367/367 | 158,89 s | 295.832 KB |
| 3 | 0 | 367/367 | 1.468/1.468 | 367/367 | 127,48 s | 295.420 KB |
| 4 | 0 | 367/367 | 1.468/1.468 | 367/367 | 92,50 s | 295.832 KB |
| 5 | 0 | 367/367 | 1.468/1.468 | 367/367 | 98,92 s | 295.412 KB |

- Body log 5 run identik: sha1 `e60eb002aeba6608d532fd17fbf88becf34a0d3c`. Konvensi: **body =
  seluruh 385 baris kecuali baris header `# adx_test: …`**; sha1 file utuh (`0ff0b8ca…`) bukan
  yang dipakai, karena header memuat jumlah file/key yang bukan hasil gate.
- Wall 92–224 s untuk pekerjaan yang sama (VPS 1 vCPU; run pertama dan kedua sesi sebelumnya
  menabrak load host). **Determinisme diambil dari sha1 isi log, bukan dari wall** — angka gate
  (367, 1.468, 572.592.917, 572.928.030) identik di 10 run yang tercatat di seksi ini.
- **359 file bypass**: output konverter byte-identik dengan PCM native (359/359). 8 file
  dikonversi; 572.592.917 frame input -> **572.928.030 frame output** (selisih = 2x dari 8 file
  24 kHz, tepat).
- **1.468 pembandingan blok** (blok 1, 7, 33, 4096 frame per kanal) semuanya byte-exact
  terhadap jalur one-shot: 1.468/1.468.
- `AdxPlayback` byte-exact terhadap jalur one-shot untuk **367/367** file, dan
  `timeline_frames()` sama dengan panjang output nyata 367/367.

**4) SNR: tiga pembanding berbeda, tiga arti berbeda**

| Pembanding | Hasil | Artinya |
|---|---|---|
| Double-precision twin (kernel sama, tanpa Q23) | **105,60–106,76 dB** | galat fixed-point di bawah lantai 16-bit |
| ffmpeg `swr` (penuh 0–24 kHz) | 29,56 dB min | desain filter beda; energi beda terkonsentrasi 8–12 kHz |
| ffmpeg `swr`, dibatasi <8 kHz di kedua sisi | **70,39 dB min** | di pita yang benar-benar didengar, dua implementasi sepakat |
| ffmpeg `soxr precision=28` (penuh) | 33,00 dB min | pembanding desain lain, bukan kebenaran |

Angka 29,56 dB itu bukan error implementasi: pada file yang sama (`adx_m204_0000`, SFX pendek
berenergi dekat Nyquist) SNR setelah kedua sinyal dibatasi 8 kHz adalah **70,39 dB**, dan
terhadap kembaran double presisi mencapai **105,60 dB**. Yang berbeda di 8–12 kHz adalah
transisi anti-image masing-masing resampler — wilayah di mana tidak ada "kebenaran" tunggal.
Karena itu gate tidak menuntut 60 dB dari ffmpeg; yang dituntut: byte-exact vs jalur one-shot,
>=60 dB vs referensi presisi, dan >=40 dB di pita <8 kHz.

**5) Tone gate analitik (bebas dari decoder mana pun), kedua arah konversi**

1 detik sinus 24 kHz -> 48 kHz, jendela koheren (integer cycle, mulai sampel 480):

| f | gain | SINAD | image (24k-f) |
|---|---|---|---|
| 100 Hz | -0,000 dB | 86,27 dB | -116,23 dBc |
| 1 kHz | -0,000 dB | 88,10 dB | -97,81 dBc |
| 5 kHz | -0,000 dB | 89,02 dB | -102,37 dBc |
| 8 kHz | -0,000 dB | 238,09 dB | -256,99 dBc |
| 10 kHz | -0,000 dB | 96,64 dB | -107,49 dBc |
| 11 kHz | -0,000 dB | 89,27 dB | -92,76 dBc |
| 11,5 kHz | -2,102 dB | 47,55 dB | -47,55 dBc |

Arah sebaliknya (desimasi 48 -> 24 kHz) punya mode kegagalan lain: isi di atas Nyquist baru
harus **dipendam**, bukan dilipat. Diukur: tone 10 kHz lewat dengan gain **-0,002 dB**, dan
tone 15 kHz (tidak punya tempat sah di 24 kHz) menyisakan **-87,74 dBc** di 9 kHz — angka itu
persis lantai 1 LSB int16, artinya residunya di bawah yang bisa direpresentasikan.

SINAD 86–96 dB berarti hasilnya sudah di lantai kuantisasi 16-bit (sinyal 10.000 LSB -> batas
teoretis ~87 dB); 8 kHz keluar eksak karena pada 48 kHz periodenya 6 sampel. 11,5 kHz turun
2,1 dB karena berada di transisi filter — konsekuensi `taps=128` + cutoff 0,97, bukan cacat.
Gate menuntut: |gain| <= 0,05 dB dan SINAD >= 70 dB untuk f <= 10 kHz, image <= -60 dBc,
gain 11 kHz >= -0,5 dB, ditambah desimasi |gain(10 kHz)| <= 0,05 dB dan residu alias <= -80 dBc.
Lulus dengan margin.

**6) Empat hal yang sempat salah**

- **Skala kernel.** Versi pertama memakai `fc*sinc(fc*u)` sehingga cutoff efektif menjadi
  fc/2 = 5,8 kHz. Yang membongkar bukan pembacaan kode, tapi tone gate: 11 kHz keluar
  **-80,33 dB**. Diperbaiki ke `2*fc*sinc(2*fc*u)` (fc dalam siklus/sampel input).
- **Q15 terlalu kasar untuk 128 tap.** Dengan koefisien Q15, SINAD jatuh ke **46,79 dB** di
  10 kHz. Penyebabnya kuantisasi koefisien (~0,4% per tap pada 1/128). Naik ke Q23: SINAD
  96,64 dB, dan SNR terhadap kembaran double 105,60 dB.
- **Bug di harness sendiri.** Tone gate mula-mula membaca sampel di indeks `i` sementara fase
  dihitung pada `i+skip`, sehingga SINAD turun monoton terhadap frekuensi (46 dB di 10 kHz)
  padahal residual sebenarnya 0,10 LSB. Setelah indeks disamakan: 96,64 dB. Pelajaran: ukur
  residual di domain spektrum sebelum menyalahkan filter.
- **Asumsi yang gugur.** Rencana awal: resampler harus memotong `read()` di batas event loop,
  kalau tidak riwayat FIR memuat audio dari sisi seam yang salah. Diukur: **0 beda** antara
  memotong dan tidak memotong (52.439.352 frame, `adx_m66_0004`, 48->96 kHz), karena
  `AdxStream::read()` memang sudah mengeluarkan urutan playback. Komentar header diganti
  dengan fakta terukur. Yang benar-benar butuh `reset()` adalah lompatan eksplisit (seek/
  retrigger): tanpa reset **199.736 dari 299.874 sampel** berbeda (fase ikut bergeser), dengan
  reset byte-exact terhadap one-shot segmen itu (`RESETCHECK ... EXACT`, 100.000 frame).

**7) Loop + resample**

| file | konversi | frame | hasil |
|---|---|---|---|
| `adx_m1070_0000` | 48 -> 24 kHz | 123.840 | EXACT |
| `adx_m69_0002` | 48 -> 24 kHz | 219.426 | EXACT |

Yang dibandingkan: `AdxPlayback` (stream + konverter) dengan `render_loop()` yang di-resample
satu tembakan. Total 343.266 frame, 0 beda.

**8) Yang belum (jujur)**

- Belum ada upload ke SPU2 / mixing / DSP; belum ada `sid` sound RPC (`0x80000006`,
  `0x80000592`). `AdxPlayback` sudah siap dipanggil mixer, tapi mixer-nya belum ada.
- Konverter hanya diuji pada **2 sinyal 24 kHz unik** (7 file mono identik) dan **tidak ada
  loop native 24 kHz**; jalur loop+resample dibuktikan pada stream 48 kHz.
- Audio non-ADX (MPEG/SND di AFS, SFD/PSS) belum disentuh.
- Semantik loop di hardware tetap `linear` (referensi vgmstream); `carry`/`reset` tersedia.
- Resampler tidak melakukan dithering; pembulatan ke int16 dilakukan dengan pembulatan
  setengah-LSB (`+kRateHalf >> 23`), bukan noise-shaped dither.

**9) Verifikasi ulang gate 9d (24 Okt 2026, 20:51–21:06 WIB)**

Binary `build/adx_test` sha256 `52b1c58351317e59` (175.416 B), gate `--resample-check`
dijalankan 5x berturut dari nol:

| run | exit | body sha1 | wall | maxrss |
|---|---|---|---|---|
| 1 | 0 | `e60eb002…` | 154,65 s | 295.824 KB |
| 2 | 0 | `e60eb002…` | 128,21 s | 295.800 KB |
| 3 | 0 | `e60eb002…` | 148,82 s | 295.768 KB |
| 4 | 0 | `e60eb002…` | 240,54 s | 295.768 KB |
| 5 | 0 | `e60eb002…` | 202,04 s | 295.828 KB |

Body sha1 identik dengan angka yang tercatat di tabel 3 — bukan run baru yang kebetulan
mirip: `SUMMARY loopresample_checked=2 loopresample_exact=2 loopresample_frames=343266
reset_checked=1 reset_exact=1` di kelima run, 385 baris body per run.
