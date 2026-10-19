// ee_bus.cpp — RAM EE + register DMA ch2 (GIF). Transfer sinkron: kick -> sink GIF
// (eekern::dma_ch2_kick) -> STR clear, jadi poll busy game (CHCR & 0x100) selalu lolos.
#include "ee_bus.h"
#include "ee_kernel.h"

#include <cstring>
#include <vector>

namespace eebus {
namespace {

std::vector<u8> g_ram;      // lazily allocated 32 MB
u32 g_chcr = 0, g_madr = 0, g_qwc = 0, g_tadr = 0;
u64 g_vif_polls = 0;      // VIF_STAT0 poll counter (sintesis bit 0x200)
u64 g_gif_busy = 0;       // GIF_STAT busy countdown (set saat kick 0x1000F180)
Stats g_stats;

std::vector<u8>& ram() {
    if (g_ram.empty()) g_ram.resize(RAM_SIZE, 0);
    return g_ram;
}

// Kick: salin qwc qword dari RAM lalu serahkan ke sink GIF (parser -> GS -> backend)
void kick(u32 madr, u32 qwc, bool tte) {
    if (qwc == 0) return;
    u32 addr = madr & 0x7FFFFFFF & ~0xFu;         // buang TTE (bit31) + align 16B
    u32 off  = addr & RAM_MASK;
    // guard: jangan baca melewati window RAM
    if ((u64)off + (u64)qwc * 16 > (u64)RAM_SIZE) {
        u32 maxq = (RAM_SIZE - off) / 16;
        if (maxq == 0) return;
        qwc = maxq;                                // potong, jangan OOB
    }
    eekern::dma_ch2_kick(ram().data() + off, qwc);
    g_stats.kicks++;
    g_stats.qwords += qwc;
    g_stats.last_madr = off;                        // offset RAM final (setelah mask window)
    g_stats.last_qwc = qwc;
    g_stats.last_tte = tte;
}

} // namespace

void reset() {
    if (!g_ram.empty()) std::memset(g_ram.data(), 0, g_ram.size());
    g_chcr = g_madr = g_qwc = g_tadr = 0;
    g_stats = Stats{};
}

void mem_write(u32 addr, const void* data, size_t n) {
    if (n == 0) return;
    u32 off = addr & RAM_MASK;
    // tulis potongan terakhir boleh dipotong (tanpa wrap — alamat EE tak wrap)
    size_t avail = (off <= RAM_SIZE) ? (RAM_SIZE - off) : 0;
    size_t k = n < avail ? n : avail;
    if (k) std::memcpy(ram().data() + off, data, k);
}

bool mem_read(u32 addr, void* out, size_t n) {
    if (n == 0) return true;
    u32 off = addr & RAM_MASK;
    if ((u64)off + n > (u64)RAM_SIZE) return false;
    std::memcpy(out, ram().data() + off, n);
    return true;
}

void mmio_write32(u32 addr, u32 val) {
    switch (addr) {
    case CH2_QWC:  g_qwc = val & 0xFFFF; break;
    case CH2_MADR: g_madr = val; break;
    case CH2_TADR: g_tadr = val; break;
    case CH2_CHCR: {
        g_chcr = val;
        g_stats.last_chcr = val;
        // STR (bit8) set = start transfer dari MADR sepanjang QWC (mode non-chain,
        // persis perilaku game: CHCR=0x101 dengan QWC+MADR sudah diisi lebih dulu)
        if (val & 0x100) {
            bool tte = (g_madr >> 31) != 0;
            kick(g_madr, g_qwc, tte);
            g_chcr = val & ~0x100u;               // sinkron: STR seketika clear
        }
        break;
    }
    default:
        g_stats.mmio_other++;                      // MMIO lain (VIF1/SIF/INTC...) — trace dulu
        break;
    }
}

u32 mmio_read32(u32 addr) {
    switch (addr) {
    case CH2_CHCR: g_stats.chcr_polls++; return g_chcr;   // STR selalu 0 (sinkron)
    case CH2_MADR: return g_madr;
    case CH2_QWC:  return g_qwc;
    case CH2_TADR: return g_tadr;
    // VIF_STAT0 (0x10008000): crt0 init libkernel polling bit 0x200 (VIF1 fifo
    // drain/finish) utk proceed — di HW asli ke-set saat VIF/DMA selesai. Native:
    // synthesizer deterministik — poll-poll-poll lalu DONE sekali (mirip sinkron
    // GIF CHCR di atas). Tanpa ini boot nge-spin 71M iter di jr-ra loop @0x3b690c.
    case 0x10008000:
        g_vif_polls++;
        g_stats.vif_polls++;
        return (g_vif_polls >= 8) ? 0x200u : 0u;
    // GIF_STAT (0x1000F130): bit 15 (0x8000) = busy; code @0x3b0d50 polls
    // ANDI 0x8000 / BNE sampai CLEAR. Native: sibuk bbrp poll lalu clear
    // (deterministik, pola sama spt VIF_STAT0 di atas). 2026-10-19.
    case 0x1000F130:
        return 0u;  // GIF_STAT: selalu ready
    default:
        g_stats.mmio_other++;
        return 0;
    }
}

const Stats& stats() { return g_stats; }

// ---- akses terpadu utk CPU (RAM window / MMIO window) ----
static inline bool is_mmio(u32 a) { return (a >> 20) == 0x100; }  // 0x10000000..0x100FFFFF

u8 read8(u32 addr) {
    if (is_mmio(addr)) { u32 b = mmio_read32(addr & ~3u); return (b >> ((addr & 3) * 8)) & 0xFF; }
    u32 off = addr & RAM_MASK;
    return off < RAM_SIZE ? ram()[off] : 0;
}
u16 read16(u32 addr) {
    if (is_mmio(addr)) { u32 w = mmio_read32(addr & ~3u); return (w >> ((addr & 2) * 8)) & 0xFFFF; }
    u32 off = addr & RAM_MASK;
    if (off + 2 > RAM_SIZE) return 0;
    return (u16)(ram()[off] | (ram()[off+1] << 8));
}
u32 read32(u32 addr) {
    if (is_mmio(addr)) return mmio_read32(addr);
    u32 off = addr & RAM_MASK;
    if (off + 4 > RAM_SIZE) return 0;
    u32 v; std::memcpy(&v, ram().data() + off, 4);
    return v;
}
void write8(u32 addr, u8 v) {
    if (is_mmio(addr)) {
        if (addr == 0x1000F180) g_gif_busy = 6;  // kick GIF -> sibuk 6 poll
        return;
    }
    u32 off = addr & RAM_MASK;
    if (off < RAM_SIZE) ram()[off] = v;
}
void write16(u32 addr, u16 v) {
    if (is_mmio(addr)) return;
    u32 off = addr & RAM_MASK;
    if (off + 2 <= RAM_SIZE) { ram()[off] = v & 0xFF; ram()[off+1] = (v >> 8) & 0xFF; }
}
void write32(u32 addr, u32 v) {
    if (is_mmio(addr)) { mmio_write32(addr, v); return; }
    u32 off = addr & RAM_MASK;
    if (off + 4 <= RAM_SIZE) std::memcpy(ram().data() + off, &v, 4);
}

} // namespace eebus
