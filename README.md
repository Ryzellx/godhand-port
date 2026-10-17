<div align="center">

# 🥋 GOD HAND  
### *Native PC Port — No Emulator. No Interpreter. Pure Reimplementation.*

**Kicking ass on modern hardware, straight from the PlayStation 2 metal.**

[![Status](https://img.shields.io/badge/status-WIP%20%7C%20active--dev-ff6b35?style=for-the-badge&logo=hackthebox&logoColor=white)](#-status)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20%7C%20Linux-4488ff?style=for-the-badge&logo=windows95)](#)
[![GPU](https://img.shields.io/badge/renderer-Vulkan-native-9b0000?style=for-the-badge&logo=vulkan)](#)
[![License](https://img.shields.io/badge/license-AGPL--3.0-informational?style=for-the-badge&logo=gnu)](#-license)
[![No Emulator](https://img.shields.io/badge/emulator-NONE-000000?style=for-the-badge)](#)

*A project by [Ryzellx](https://github.com/Ryzellx) · automated decomp-and-reimpl pipeline*

</div>

---

## ⚠️ STATUS — SEDANG DIKEMBANGKAN

> [!IMPORTANT]
> **Ini build aktif yang lagi dikembangin.** Render pipeline + kernel shim + asset toolchain
> sudah jalan dan teruji; integrasi runtime game penuh masih digarap.
> **Belum ada yang bisa diklik-dan-main hari ini** — pantau commit & [milestone](#-roadmap).
> Bintang repo ini kalau mau ngeliat perkembangannya. 🔥

---

## 🎯 Apa Ini?

Port **God Hand** (Clover Studio / Capcom, 2006) ke PC Windows & Linux sebagai
**eksekusi native x86-64** — bukan emulator, bukan dynarec:

- Kode game didecompile dari MIPS R5900 ELF (Ghidra, 6146 fungsi) → di-*reimplement*
- Kernel PS2 (156 syscall site, 141 API) → shim layer standar (std::thread/mutex/file)
- GS → **Vulkan** via intercept packet GIF dari DMA channel 2 (parsing 1:1 spec)
- Asset TIM2/AFS/ADX → konverter sendiri (stdlib, tanpa dependensi berat)

Gagasan besarnya: *jangan tiru konsol — pahami game, bangun ulang mesinnya.*

## 🧱 Arsitektur

```
ISO God Hand (SLUS-21503)
 ├── tools/afs_batch.py   → 1445/1445 asset ter-unpack + klasifikasi
 ├── tools/tim2_to_png.py → texture TIM2 → PNG (pal4/pal8/rgba16/rgba32)
 ├── tools/adx_decode.py  → CRI ADX → PCM (byte-exact vs ffmpeg, 367/367)
 │
 ├── src/gif/             → parser GIF tag (PACKED/REGLIST/IMAGE)  [6/6 test]
 ├── src/gs/              → GS register state machine (FRAME/SCISSOR/PRIM…) [id dari gsKit]
 ├── src/vk/              → backend Vulkan offscreen (clear/tri/sprite)  [pixel-verified]
 ├── src/shim/            → EE kernel shim (thread/sema/eventflag/file)  [5/5 test]
 └── src/slice/           → M4 vertical slice: packet GIF → Vulkan → PPM  [5/5 floor]
```

## 🖼️ Bukti Kerja

**Frame pertama** yang dirender pakai jalur asli game (packet GIF → GS state → Vulkan):

*(screenshot slice + texture konversi ada di folder `out/` setelah build — lihat commit)*

## 📊 Milestone

- [x] Recon binary + 6146 fungsi decompile
- [x] Syscall contract (156 site, 0 unknown)
- [x] GIF tag parser + GS state (verified vs PCSX2/gsKit)
- [x] Vulkan backend offscreen (pixel-verified 5/5)
- [x] EE kernel shim (thread/sema/EF/file, mingw-OK)
- [x] Asset pipeline: AFS/TIM2/ADX (100% terconv)
- [ ] Kalibrasi struct & compile funnels ( running )
- [ ] IOP RPC host backend (pad/sound/memcard)
- [ ] Runtime integration: crt0 → libgs → first room
- [ ] **PLAYABLE** 🎮

## 🛠️ Build (developer)

Butuh: C++17, CMake 3.16+, Vulkan SDK, (opsional) mingw-w64 buat cross-compile Windows.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
./build/gif_test      # 6/6 PASS
./build/shim_test     # 5/5 PASS
setarch -R ./build/godhand_slice   # buffer slice: out/slice.ppm
```

> Catatan lingkungan: pada worker CI/VPS dengan lavapipe (software Vulkan), jalankan
> binary render dengan `setarch -R` — bug driver internal yang ASLR-exposed (mesa 25.2.x),
> bukan bug kode port.

## 🙏 Kredit & Tradisi

- **Clover Studio / Capcom** — game sekelasnya sendiri.
- **PS2 dev community** — ps2dev/gsKit, PCSX2, OpenKh, psdevwiki (spesifikasi & referensi).
- **FFmpeg** — referensi decoder ADX.

Game, trademark, dan asset original milik **Capcom**. Proyek ini *reimplementation layer*
yang **tidak mendistribusikan** ISO/asset game — kamu harus punya disc-nya sendiri.

## ⚖️ License

Diproduksi ulang di bawah **[GNU AGPL-3.0](LICENSE)**.

> Kenapa AGPL? Kode RE hasil kerja keras ini harusnya tetap terbuka selamanya — siapa pun
> yang nge-fork & improving (termasuk sebagai layanan) wajib membagikan kodenya balik.
> Sangat cocok buat proyek decompilation/reimplementation komunitas.

<div align="center">

**🔥 di-star biar gak ketinggalan — playable milestone bakal di-announce di sini 🔥**

*“The road to hell is paved with good intentions… and god hands.”*

</div>
