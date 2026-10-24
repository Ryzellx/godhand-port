// ee_bus.cpp — RAM EE + register DMA ch2 (GIF). Transfer sinkron: kick -> sink GIF
// (eekern::dma_ch2_kick) -> STR clear, jadi poll busy game (CHCR & 0x100) selalu lolos.
#include "ee_bus.h"
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "ee_kernel.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace eebus {
u32 g_watch_lo = 0, g_watch_hi = 0, g_cur_pc = 0;
u64 g_watch_hits = 0, g_watch_cap = 200;
void watch_set(u32 lo, u32 hi) { g_watch_lo = lo; g_watch_hi = hi; }

namespace {
inline void watch_log(u32 addr, u32 val, int sz) {
    if (!g_watch_lo) return;
    // Alias EE (0x2xxxxxxx uncached / 0x8xxxxxxx) dinormalkan: game sering menulis
    // field objek lewat alias, jadi jendela watch harus melihat alamat kanonik
    // (terukur: fd+1 tidak pernah muncul sebelum normalisasi ini).
    u32 a = addr & 0x1FFFFFFFu;
    if (a < g_watch_lo || a >= g_watch_hi) return;
    g_watch_hits++;
    if (g_watch_hits <= g_watch_cap)
        std::fprintf(stderr, "[WW] pc=%08x addr=%08x val=%08x sz=%d raw=%08x\n",
                     g_cur_pc, a, val, sz, addr);
}

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

// Phase 12: census alamat MMIO yang DITULIS (siapa yang digerakkan game).
std::map<u32, u64> g_mmio_census;
std::map<u32, std::map<u32, u64>> g_mmio_pc;   // addr -> (pc -> n), top writer
void mmio_write32(u32 addr, u32 val) {
    g_mmio_census[addr]++;
    // Phase 12: provenance penulis untuk SEBARANG alamat MMIO (bukan hanya GIF) supaya
    // blok yang belum dipetakan (0x10009000, 0x1000E010/E020, ...) bisa diidentifikasi.
    g_mmio_pc[addr][g_cur_pc]++;
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
    // 0x1000F000 — register SIF DMA sisi EE (dipakai lib SIF game @0x3b0140,
    // 0x3b01e0, 0x3b0234: tulis 4 -> sync -> poll ANDI 0x4 sampai set, dengan
    // jalur abort kalau word status di stack != 0). Pola clear-then-wait:
    // bit2 = "transfer SIF selesai". Model: DMA kita sinkron, jadi bit2 = 1
    // (transfer dianggap langsung selesai). Tulis 4 = clear flag -> langsung
    // ter-set lagi pada pembacaan berikutnya.
    case 0x1000F000:
        g_stats.mmio_other++;
        return 0x4u;
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
    watch_log(addr, v, 1);
    u32 off = addr & RAM_MASK;
    if (off < RAM_SIZE) ram()[off] = v;
}
void write16(u32 addr, u16 v) {
    if (is_mmio(addr)) return;
    watch_log(addr, v, 2);
    u32 off = addr & RAM_MASK;
    if (off + 2 <= RAM_SIZE) { ram()[off] = v & 0xFF; ram()[off+1] = (v >> 8) & 0xFF; }
}
void write32(u32 addr, u32 v) {
    if (is_mmio(addr)) { mmio_write32(addr, v); return; }
    watch_log(addr, v, 4);
    u32 off = addr & RAM_MASK;
    if (off + 4 <= RAM_SIZE) std::memcpy(ram().data() + off, &v, 4);
}

std::string mmio_pc_csv(u32 addr, size_t top) {
    std::vector<std::pair<u64, u32>> v;
    for (auto& kv : g_mmio_pc[addr]) v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::string s;
    for (size_t i = 0; i < v.size() && i < top; i++) {
        char b[40]; std::snprintf(b, sizeof b, "%08x=%llu; ", v[i].second, (unsigned long long)v[i].first);
        s += b;
    }
    return s;
}
std::string mmio_csv(size_t top) {
    std::vector<std::pair<u64, u32>> v;
    for (auto& kv : g_mmio_census) v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::string s;
    for (size_t i = 0; i < v.size() && i < top; i++) {
        char b[40]; std::snprintf(b, sizeof b, "%08x=%llu; ", v[i].second, (unsigned long long)v[i].first);
        s += b;
    }
    return s;
}

} // namespace eebus
