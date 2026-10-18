// bus_test — Phase 7a: tabel register MMIO DMA ch2 (GIF) = jalur libgs game -> sink GIF.
// Meniru SECARA LANGSUNG urutan tulis register dari decomp game asli:
//   FUN_00396d60 (sceGsPutDrawEnv):  poll CHCR&0x100 -> QWC=(tag&0x7fff)+1 -> MADR -> CHCR=0x101
//   FUN_00395758 (sceGsExecLoadImage): poll -> QWC=6 -> MADR (TTE jika addr&0x70000000) -> CHCR=0x101
// Verifikasi: kick terkirim dgn qwc benar, parse GIF -> gs::State cocok, guard OOB.
#include "ee_bus.h"
#include "ee_kernel.h"
#include "../gif/gif.h"
#include "../gs/gs_state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace eebus;

// ---- penangkap kick (sink GIF) ----
static std::vector<uint8_t> g_captured;
static u32 g_kick_qwc = 0, g_kick_calls = 0;
static void sink(const void* pkt, u32 qwc) {
    g_kick_calls++;
    g_kick_qwc = qwc;
    g_captured.assign((const uint8_t*)pkt, (const uint8_t*)pkt + (size_t)qwc * 16);
}

static void put_tag(std::vector<uint8_t>& v, uint32_t nloop, bool eop, uint32_t nreg,
                    const std::vector<uint8_t>& regs, uint32_t flg = 0) {
    uint64_t w0 = (nloop & 0x7FFFull) | ((uint64_t)eop << 15) | ((uint64_t)flg << 28)
                | ((uint64_t)(nreg & 0xF) << 30);
    uint64_t w1 = 0;
    for (size_t i = 0; i < regs.size(); i++) w1 |= (uint64_t)regs[i] << (i * 4);
    uint8_t b[16];
    std::memcpy(b, &w0, 8); std::memcpy(b + 8, &w1, 8);
    v.insert(v.end(), b, b + 16);
}
static void put_qw(std::vector<uint8_t>& v, uint64_t lo, uint64_t hi = 0) {
    uint8_t b[16];
    std::memcpy(b, &lo, 8); std::memcpy(b + 8, &hi, 8);
    v.insert(v.end(), b, b + 16);
}

// urutan register persis seperti game (dekomp): baca CHCR dulu (poll), lalu QWC, MADR, CHCR
static void game_kick_sequence(u32 pkt_addr, u32 qwc, bool force_tte) {
    u32 spins = 0;
    while ((mmio_read32(CH2_CHCR) & 0x100) != 0 && spins++ < 0x1000000) {}   // poll STR
    mmio_write32(CH2_QWC, qwc);
    u32 madr = pkt_addr & 0x0FFFFFFF;
    if (force_tte) madr |= 0x80000000;              // game: addr&0x70000000 -> MADR |= bit31 (TTE)
    mmio_write32(CH2_MADR, madr);
    mmio_write32(CH2_CHCR, 0x101);                   // STR|DIR persis tulisan game
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    eekern::set_gif_kick(sink);
    reset();
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("[%s] %s -> %s\n", ok ? "PASS" : "FAIL", what, ok ? "ok" : "GAGAL");
        if (!ok) fails++;
    };

    // ---- T1: mem write/read roundtrip + guard lintas window ----
    {
        const uint32_t A = 0x100000;
        std::vector<uint8_t> pat(64);
        for (int i = 0; i < 64; i++) pat[i] = (uint8_t)(i * 7 + 1);
        mem_write(A, pat.data(), pat.size());
        uint8_t back[64] = {0};
        check(mem_read(A, back, 64) && std::memcmp(pat.data(), back, 64) == 0,
              "T1a mem roundtrip 64B");
        uint8_t tmp[16];
        check(!mem_read(RAM_SIZE - 8, tmp, 16), "T1b OOB read ditolak");
        check(!mem_read(0x0FFFFFF0, tmp, 64), "T1c addr jauh di luar window ditolak");
    }

    // ---- T2: jalur sceGsPutDrawEnv — env packet 8 AD write -> parse -> gs state ----
    {
        const uint32_t W = 640, H = 448, A = 0x100000;
        std::vector<uint8_t> pkt;
        put_tag(pkt, 8, false, 1, {gif::AD});                    // nreg=1 -> QWC = nloop+1
        put_qw(pkt, gs::pack_frame(0, W / 64, 0, 0), gs::FRAME_1);
        put_qw(pkt, 0x1ull, gs::ZBUF_1);
        put_qw(pkt, gs::pack_xyoffset(0, 0), gs::XYOFFSET_1);
        put_qw(pkt, gs::pack_scissor(0, W - 1, 0, H - 1), gs::SCISSOR_1);
        put_qw(pkt, 0, gs::TEST_1);
        put_qw(pkt, 1, gs::PRMODECONT);
        put_qw(pkt, 1, gs::COLCLAMP);
        put_qw(pkt, 0, gs::ALPHA_1);
        mem_write(A, pkt.data(), pkt.size());

        u32 qwc_from_tag = (pkt[0] & 0x7fff) + 1;                // formula game
        check(qwc_from_tag == pkt.size() / 16, "T2a formula QWC=(tag&0x7fff)+1 == ukuran paket");
        g_kick_calls = 0;
        game_kick_sequence(A, qwc_from_tag, /*force_tte*/false);
        check(g_kick_calls == 1 && g_kick_qwc == 9 && g_captured.size() == pkt.size(),
              "T2b kick terkirim (qwc=9, byte identik)");
        check(std::memcmp(g_captured.data(), pkt.data(), pkt.size()) == 0,
              "T2c isi kick == isi RAM");

        std::vector<gif::Action> acts;
        size_t cons = 0;
        bool ok = gif::parse_stream(g_captured.data(), g_captured.size(), acts, &cons)
                  && cons == g_captured.size();
        gs::State st;
        for (auto& a : acts) if (a.kind == gif::Action::AD_WRITE) st.ad_write(a.ad_addr, a.ad_data);
        check(ok && acts.size() == 8, "T2d parse GIF -> 8 aksi AD");
        check(st.frame_of(0).fbw == W / 64 && st.scissor_of(0).x1 == W - 1
              && st.xyoffset_of(0).y == 0 && st.frame_of(0).msb == 0,
              "T2e gs::State FRAME/SCISSOR/XYOFFSET cocok");
        check((mmio_read32(CH2_CHCR) & 0x100) == 0, "T2f poll STR lolos (sinkron)");
        check(stats().chcr_polls >= 1, "T2g poll CHCR terhitung");
    }

    // ---- T3: jalur sceGsExecLoadImage — QWC=6 konstan + TTE bit31 ----
    {
        const uint32_t A = 0x200000;
        std::vector<uint8_t> pkt;
        put_tag(pkt, 5, true, 1, {}, /*flg IMAGE*/2);            // tag IMAGE + 5 qword data = 6
        for (int i = 0; i < 5; i++) put_qw(pkt, 0x1122334455667788ull + i, 0xAABBCCDD00000000ull + i);
        mem_write(A, pkt.data(), pkt.size());
        g_kick_calls = 0;
        game_kick_sequence(A, /*QWC=6*/6, /*force_tte*/true);    // addr | 0x70000000 di MADR
        check(g_kick_calls == 1 && g_kick_qwc == 6, "T3a kick QWC=6 (ExecLoadImage)");
        check(stats().last_tte, "T3b flag TTE (bit31) terbaca dari MADR");
        check(stats().last_madr == A, "T3c MADR di-mask -> alamat paket benar (0x200000)");
        std::vector<gif::Action> acts;
        size_t cons = 0;
        bool ok = gif::parse_stream(g_captured.data(), g_captured.size(), acts, &cons)
                  && cons == g_captured.size();
        check(ok && acts.size() == 1 && acts[0].kind == gif::Action::IMAGE_DATA
              && acts[0].qwords == 5, "T3d parse -> IMAGE_DATA 5 qword");
    }

    // ---- T4: register lain / edge ----
    {
        u32 k0 = g_kick_calls;
        mmio_write32(CH2_QWC, 0);                 // QWC=0 -> kick nihil
        mmio_write32(CH2_MADR, 0x300000);
        mmio_write32(CH2_CHCR, 0x101);
        check(g_kick_calls == k0, "T4a QWC=0 -> tanpa kick");
        mmio_read32(0x1000C000);                  // MMIO di luar ch2 (mis VIF1/SIF)
        mmio_write32(0x1000E010, 0x1234);
        check(stats().mmio_other == 2, "T4b MMIO non-ch2 di-trace (bukan error)");
        // OOB kick: MADR dekat akhir RAM, QWC besar -> dipotong, tak crash
        mem_write(RAM_SIZE - 64, "ABCDEFGHIJKLMNOP", 16);
        g_kick_calls = 0;
        mmio_write32(CH2_QWC, 0x7FFF);
        mmio_write32(CH2_MADR, RAM_SIZE - 64);
        mmio_write32(CH2_CHCR, 0x101);
        check(g_kick_calls == 1 && g_kick_qwc == 4, "T4c OOB dipotong ke 4 qword (tanpa crash)");
        u32 chcr_after = mmio_read32(CH2_CHCR);
        check((chcr_after & 0x100) == 0, "T4d STR clear setelah kick");
    }

    // ---- T5: statistik akhir ----
    {
        const Stats& s = stats();
        std::printf("[stats] kicks=%llu qwords=%llu chcr_polls=%llu mmio_other=%llu "
                    "last{madr=0x%x qwc=%u chcr=0x%x tte=%d}\n",
                    (unsigned long long)s.kicks, (unsigned long long)s.qwords,
                    (unsigned long long)s.chcr_polls, (unsigned long long)s.mmio_other,
                    s.last_madr, s.last_qwc, s.last_chcr, (int)s.last_tte);
        check(s.kicks == 3 && s.qwords == 9 + 6 + 4, "T5 hitungan akhir konsisten");
    }

    std::printf(fails ? "BUS TEST: %d FAIL\n" : "BUS TEST: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
