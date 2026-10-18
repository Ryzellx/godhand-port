// ee_bus.h — bus EE: RAM 32MB + tabel register MMIO DMA channel 2 (GIF)
// Bagian Phase 7a (integrasi runtime). Sumber kontrak register: ENGINE-MAP.md §2 +
// decomp nyata game — FUN_00395758 (sceGsExecLoadImage: QWC=6 konstan) dan
// FUN_00396d60 (sceGsPutDrawEnv: QWC=(nloop&0x7fff)+1), keduanya:
//   poll CHCR & 0x100 (STR busy) -> tulis QWC -> tulis MADR (bit31 TTE jika
//   addr&0x70000000==0x70000000) -> tulis CHCR = 0x101.
// Native: transfer sinkron — STR langsung clear setelah kick (poll selalu lolos).
#pragma once
#include <cstdint>
#include <cstddef>

namespace eebus {

using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;

// EE RAM utama 32 MB (konsol asli); alamat di-mask ke window ini.
inline constexpr u32 RAM_SIZE = 32u * 1024u * 1024u;
inline constexpr u32 RAM_MASK = RAM_SIZE - 1;

// Register DMA channel 2 (basis 0x1000A000, pola per-channel EE DMAC)
inline constexpr u32 CH2_BASE = 0x1000A000;
inline constexpr u32 CH2_CHCR = CH2_BASE + 0x00;   // bit8 STR (busy/start), bit0 DIR
inline constexpr u32 CH2_MADR = CH2_BASE + 0x10;   // alamat paket (bit31 = TTE saat chain-tag)
inline constexpr u32 CH2_QWC  = CH2_BASE + 0x20;   // jumlah qword transfer
inline constexpr u32 CH2_TADR = CH2_BASE + 0x30;   // chain mode (belum dipakai game utk GIF kick)

void reset();                              // bersihkan RAM + register + statistik

// Memori EE (alamat fisik; mask RAM_MASK)
void  mem_write(u32 addr, const void* data, size_t n);
bool  mem_read(u32 addr, void* out, size_t n);   // false kalau lintas batas window

// Dispatch tulis/baca MMIO — dipanggil lapis CPU nanti saat eksekusi sw/stw
void  mmio_write32(u32 addr, u32 val);
u32   mmio_read32(u32 addr);

// Akses memori terpadu utk CPU (auto dispatch: RAM window / MMIO window)
u8   read8(u32 addr);  u16 read16(u32 addr);  u32 read32(u32 addr);
void write8(u32 addr, u8 v); void write16(u32 addr, u16 v); void write32(u32 addr, u32 v);

// Statistik (bukti test)
struct Stats {
    u64 kicks = 0;          // berapa kali CHCR STR di-set & transfer jalan
    u64 qwords = 0;         // total qword terkirim ke sink GIF
    u64 chcr_polls = 0;     // berapa kali CHCR dibaca (poll busy)
    u64 vif_polls = 0;      // berapa kali VIF_STAT0 dibaca (poll drain)
    u64 mmio_other = 0;     // tulis/baca MMIO di luar DMA ch2 (trace, bukan error)
    u32 last_madr = 0, last_qwc = 0, last_chcr = 0;
    bool last_tte = false;  // bit31 TTE pada kick terakhir
};
const Stats& stats();

} // namespace eebus
