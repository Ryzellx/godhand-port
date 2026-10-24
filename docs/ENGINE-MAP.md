# ENGINE MAP — God Hand (SLUS-215.03) native port
Sumber: ELF verified + 6146 fungsi decompiled + disasm 714K baris + syscall inventory 156 situs.

## 1. Boundary kernel EE (shim contract)
- **156 situs syscall**, 141 nomor unik, 0 UNKNOWN — kontrak penuh di
  `~/ports/godhand/extract/syscall_contract.csv` (addr, nomor, nama ps2sdk, fungsi Ghidra)
- Konvensi EE: nomor di **register v1** sebelum instruksi `syscall` (bukan v0)
- Kategori sites: thread 33 · gs/sif 21 · eventflag 16 · intc/alarm 13 · sema 10 · dmac 9 · misc 54
- Bukti boot: `li v1,60` = SetupThread, `li v1,61` = SetupHeap (crt0, addr 0x1001c4/0x1001e0)
- Stub libkernel ter-link utuh (nomer 0..131 + varian negatif i-*) — shim tinggal implement
  tabel kecil: std::thread (CreateThread/StartThread/...), std::mutex+condvar (Sema),
  flag (EventFlag), no-op/trace (IntcDmac), SIF-RPC stub, GS IMR/PGIF stub, print→printf.

## 2. Jalur render (interception point Vulkan)
- Game **tidak pakai syscall buat data path GS** — libgs (static) **programming DMA channel 2
  (GIF) via MMIO mentah** di blok `0x1000A000`:
  - `0x1000A000` CHCR-ish kick (bit8 STR, bit0 DIR), poll bit 0x100
  - `0x1000A010` ADDR/QWC dengan bit31 TTE saat DMAtag ID=7
  - `0x1000A020` QWC write = (nloop&0x7fff)+1 (bukti dari FUN_00396d60 = sceGsPutDrawEnv kick)
- Native port: semua tulisan MMIO EE DMA → **shim register table** → baca rantai qword dari
  memori "EE" → **GIF parser** (`godhand-port/src/gif/`) → aksi → backend Vulkan.
- Libgs API yang dipakai game (strings): sceGsPutDrawEnv, sceGsExecStoreImage,
  sceGsExecLoadImage, sceGsSetDefLoadImage, sceGsSyncPath, sceGsSetDefDrawEnv (implicit),
  sceGsSetDefClear, sceGsSetDefDispEnv — semua berakhir di rantai GIF yang sama.

## 3. GIF tag semantics (terverifikasi vs PCSX2 Gif_Unit.h)
- PACKED: payload qwords = NLOOP × NREG (MVP asumsi 1 qword/register; PRIM 80-bit edge
  case ditandai tak didukung — jarang di paket game)
- REGLIST: **dua nilai 64-bit per qword**, payload qwords = ceil(NLOOP×NREG/2);
  A+D = pasangan (addr, data) alternan
- IMAGE: payload = NLOOP qword mentah (upload texture/local FB)
- FLG=3 diperlakukan IMAGE (alias IMAGE2 PCSX2)
- Unit test 6/6 PASS (`godhand-port/build_gif_test`)

## 4. Sistem lain (untuk shim)
- **IOP RPC** (SifSetDma/SifCallRpc/SifBindRpc + IOPRP310.IMG/IRX.IMS di ISO): padman,
  CRI ADX audio (SDHD/SDRDRV/SNDDRV/LIBSD), MCMAN/MCSERV (memory card), CDVD (sceCdSearchFile).
  Port native: implement backend host — pad→XInput, ADX→decode lib (or re-encode asset),
  MC→file save, CDVD→filesystem langsung (AFS/FST dari PROJECT.md).
- **File I/O**: sceOpen/Close/Lseek/Read/Write/Ioctl — path cdrom0: → root ISO extract.
- **VU0 math**: sceVu0* (14 strings) — VU0 macro mode → shim ke C float/matrix (CPU SSE).
- **Timing**: SetVSyncFlag, SetCPUTimer, Alarm — vsync loop → D3D/Vulkan present + QPC.

## 5. Urutan milestone
- [x] M1 recon (PROJECT.md phase 0-1)
- [x] M2a syscall contract + GIF parser + unit test (ini)
- [ ] M2b shim skeleton EE (thread/sema/file, compile Windows mingw + Linux)
- [ ] M3  GPU: GS register state machine (FRAME/ZBUF/TEX0/SCISSOR/...) dari AD writes →
       Vulkan pipeline state
- [ ] M4  boot vertikal slice: crt0 subset → DrawEnv → clear screen di Vulkan window
- [ ] M5  asset pipeline (TIM2→KTX, AFS unpack, ADX) + stage st0 load
- [ ] M6  gameplay loop integration

## 6. VU0 micro-mode encoding (dari census ELF .text — sesi 19 Okt)
- Instruksi micro VU0 di .text game: primary opcode 0x12 (COP2), rs field:
  - rs 0x00..0x07 = moves (1=QMFC2, 2=CFC2, 5=QMTC2, 6=CTC2, dll)
  - rs 0x08 = BC2 (branch, rt field = cond)
  - rs >= 0x10 = microinstruction: **function field 9-bit** (bits 8-0):
    - func 0x000-0x3B → SPECIAL1 table (idx = func): VADDx..VOPMSUB, VMINI(0x2F), VCALLMS..
    - func 0x3C-0x3F → SPECIAL2: idx = (func & 0x3) | ((inst >> 4) & 0x7C) —
      cover VADDAx..VRXOR termasuk VMOVE(52), VMR32(53), VLQI/VSQI/VDIV/VSQRT/VWAITQ/VMTIR/VMFIR..
  - Field micro: bit25 tf, bit24 ts, ft=bits23-19, fs=bits18-14, fd=bits13-9, func=bits8-0
  - Register: VF0..VF31 (xyzw float), VI0..VI31 (int, VI0=0), accumulator ACC (x,y,z,w)
- Census statik .text (2,832,160 B @ 0x100000): SQC2 6.863 · LQC2 4.889 · COP2 3.054 situs

## 7. Peta SID SIF — terukur dari boot retail (2026-10-24)
| SID | Server | Bukti |
|---|---|---|
| 0x80000001 | fileio | bind game cd=0x7816c0; fno 255 (payload 8 B = 2 pointer 0x780c40/0x781080) dijawab -1 — semantik belum diidentifikasi, bukan sekadar 'versi' |
| 0x80000003 | iopheap (sceSifAllocIopHeap/Free) | wrapper 0x3b5968 (fno 1) & 0x3b59d8 (fno 2) |
| 0x80000006 | loadfile (IOPRP ROMDIR idx 12) | fno 255 = word "3100"; fno 0 = LoadModule, 7 modul nyata |
| 0x80000701 | belum dikenal (autostub) | bind setelah cri_adxi dimuat |
| 0x80000592 | libsd (audio) | bind awal CRT, 2× |
| 0x90000200 | SJX/CRI sound (PS2RNA) | fno = 0x400\|opcode; op 0x02 DTX, 0x22 SJUNI_CreateRmt |
| 0x19720317 | tag internal game (bukan SID IOP) | immediate di 0x375874, dipakai objek async 0x3b2f48 |

Catatan model: `cd` (SifRpcClientData) game = {+0 pkt_addr, +4 rpc_id, +8 sema_id, +20 buf,
+24 cbuf, +36 server}. `rec_id` bit0 = request aktif, bit1 = selesai (dibuktikan `is_busy()`
@0x3b3328). Paket `sifcmd` EE→IOP didecode dari salinan RAM IOP (dua-pass kick).
