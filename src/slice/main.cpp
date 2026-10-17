// slice/main.cpp — M4 vertikal slice: packet GIF sintetis -> parser -> GS state ->
// Vulkan offscreen -> verifikasi piksel + simpan PPM. Jalan tanpa window (headless-safe).
#include "../gif/gif.h"
#include "../gs/gs_state.h"
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gif;

static void put_tag(std::vector<uint8_t>& v, uint32_t nloop, bool eop, bool pre,
                    uint32_t prim, Flag flg, uint32_t nreg, const std::vector<uint8_t>& regs) {
    uint64_t w0 = (nloop & 0x7FFFull) | ((uint64_t)eop << 15) | ((uint64_t)pre << 16)
                | ((uint64_t)(prim & 0x7FF) << 17) | ((uint64_t)(uint32_t)flg << 28)
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

static const uint32_t W = 640, H = 448;   // NTSC

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // crash-diagnosis: output tak ke-buffer
    std::fprintf(stdout, "[phase] main-start\n");
    using namespace gs;
    // ---------- susun paket seperti libgs: env (A+D) + PRIM + vertex ----------
    std::vector<uint8_t> pkt;
    // env: 8 A+D write
    put_tag(pkt, 8, /*eop*/false, 0, 0, Flag::PACKED, 1, {AD});
    put_qw(pkt, pack_frame(0, W / 64, 0 /*PSMCT32*/, 0), FRAME_1);
    put_qw(pkt, 0x1ull, ZBUF_1);                       // msk=1 (Z nonaktif — MVP)
    put_qw(pkt, pack_xyoffset(0, 0), XYOFFSET_1);
    put_qw(pkt, pack_scissor(0, W - 1, 0, H - 1), SCISSOR_1);
    put_qw(pkt, 0, TEST_1);                            // ATE=0 ZTE=0 (lolos semua)
    put_qw(pkt, 1, PRMODECONT);                        // PRIM reg berlaku
    put_qw(pkt, 1, COLCLAMP);
    put_qw(pkt, 0, ALPHA_1);
    // tri merah: PRIM(TRIANGLE) lalu PACKED RGBAQ+XYZ2 x3
    put_tag(pkt, 1, false, 0, 0, Flag::PACKED, 1, {AD});
    put_qw(pkt, P_TRIANGLE, gs::PRIM);
    put_tag(pkt, 3, false, 0, 0, Flag::PACKED, 2, {RGBAQ, XYZ2});
    put_qw(pkt, pack_rgbaq(255, 0, 0, 128));
    put_qw(pkt, pack_xyz2(100 << 4, 100 << 4, 0));
    put_qw(pkt, pack_rgbaq(255, 0, 0, 128));
    put_qw(pkt, pack_xyz2(540 << 4, 100 << 4, 0));
    put_qw(pkt, pack_rgbaq(255, 0, 0, 128));
    put_qw(pkt, pack_xyz2(320 << 4, 380 << 4, 0));
    // sprite hijau: PRIM(SPRITE) + 2 vertex
    put_tag(pkt, 1, false, 0, 0, Flag::PACKED, 1, {AD});
    put_qw(pkt, P_SPRITE, gs::PRIM);
    put_tag(pkt, 2, true, 0, 0, Flag::PACKED, 2, {RGBAQ, XYZ2});
    put_qw(pkt, pack_rgbaq(0, 255, 0, 128));
    put_qw(pkt, pack_xyz2(40 << 4, 380 << 4, 0));
    put_qw(pkt, pack_rgbaq(0, 255, 0, 128));
    put_qw(pkt, pack_xyz2(200 << 4, 430 << 4, 0));

    // ---------- parse ----------
    std::vector<Action> acts;
    size_t cons = 0;
    if (!parse_stream(pkt.data(), pkt.size(), acts, &cons) || cons != pkt.size()) {
        std::printf("SLICE FAIL: parse\n"); return 1;
    }
    std::printf("[slice] actions parsed: %zu\n", acts.size());

    // ---------- eksekusi -> state GS + batch vertex ----------
    State st;
    auto decode_rgba = [](uint64_t w, float o[4]) {
        o[0] = ((w >> 0) & 0xFF) / 255.0f;
        o[1] = ((w >> 8) & 0xFF) / 255.0f;
        o[2] = ((w >> 16) & 0xFF) / 255.0f;
        o[3] = ((w >> 24) & 0xFF) / 128.0f;      // GS alpha 0x80 = 1.0
    };
    std::vector<vkbe::Vertex> tris;
    std::vector<vkbe::Vertex> pending;           // vertex primitive aktif
    float col[4] = {1, 1, 1, 1};
    auto px = [](uint64_t xyz, int idx) { return (float)((xyz >> (idx * 16)) & 0x7FFF) / 16.0f; };

    for (const Action& a : acts) {
        switch (a.kind) {
        case Action::AD_WRITE:
            if (pending.size() >= 2 && st.active_prim().type == P_SPRITE) { pending.clear(); }
            st.ad_write(a.ad_addr, a.ad_data);
            break;
        case Action::RGBA_WRITE:
            decode_rgba(a.ad_data, col);
            break;
        case Action::PRIM_WRITE:
            st.ad_write(gs::PRIM, a.prim);
            break;
        case Action::XYZ_WRITE: {
            float x = px(a.ad_data, 0) - (float)st.xyoffset_of(0).x / 16.0f;
            float y = px(a.ad_data, 1) - (float)st.xyoffset_of(0).y / 16.0f;
            pending.push_back({x, y, col[0], col[1], col[2], col[3]});
            int type = st.active_prim().type;
            if (type == P_TRIANGLE && pending.size() == 3) {
                tris.insert(tris.end(), pending.begin(), pending.end());
                pending.clear();
            } else if (type == P_SPRITE && pending.size() == 2) {
                vkbe::Vertex p0 = pending[0], p1 = pending[1];
                vkbe::Vertex a00{p0.x, p0.y, col[0], col[1], col[2], col[3]};
                vkbe::Vertex a10{p1.x, p0.y, col[0], col[1], col[2], col[3]};
                vkbe::Vertex a01{p0.x, p1.y, col[0], col[1], col[2], col[3]};
                vkbe::Vertex a11{p1.x, p1.y, col[0], col[1], col[2], col[3]};
                tris.insert(tris.end(), {a00, a10, a01, a10, a11, a01});
                pending.clear();
            }
            break;
        }
        default: break;
        }
    }
    std::printf("[slice] triangles: %zu | frame.fbw=%u scissor.x1=%u\n",
                tris.size() / 3, st.frame_of(0).fbw, st.scissor_of(0).x1);

    // ---------- render + verifikasi ----------
    std::fprintf(stdout, "[phase] backend-ctor\n");
    vkbe::Backend be(W, H);
    std::fprintf(stdout, "[phase] backend-done\n");
    auto sc = st.scissor_of(0);
    std::fprintf(stdout, "[phase] begin-frame\n");
    be.begin_frame(16, 24, 40, sc.x0, sc.y0, sc.x1 - sc.x0 + 1, sc.y1 - sc.y0 + 1);
    std::fprintf(stdout, "[phase] draw\n");
    be.draw_triangles(tris);
    std::fprintf(stdout, "[phase] end-frame\n");
    std::vector<uint8_t> fb = be.end_frame();
    std::fprintf(stdout, "[phase] readback-done\n");

    auto px_at = [&](int x, int y) {
        size_t o = ((size_t)y * W + x) * 3;
        return std::vector<uint8_t>{fb[o], fb[o + 1], fb[o + 2]};
    };
    int fails = 0;
    auto expect = [&](int x, int y, int r, int g, int b, const char* what) {
        auto p = px_at(x, y);
        bool ok = p[0] == (uint8_t)r && p[1] == (uint8_t)g && p[2] == (uint8_t)b;
        std::printf("[%s] px(%d,%d) = (%d,%d,%d) expect (%d,%d,%d) -> %s\n",
                    what, x, y, p[0], p[1], p[2], r, g, b, ok ? "OK" : "FAIL");
        if (!ok) fails++;
    };
    expect(10, 10, 16, 24, 40, "clear");
    expect(320, 224, 255, 0, 0, "tri-red");
    expect(60, 390, 0, 255, 0, "sprite-green");   // dlm tri1, jauh dari diagonal
    expect(300, 410, 16, 24, 40, "outside-sprite");
    // state contract
    if (st.frame_of(0).fbw != W / 64 || st.scissor_of(0).x1 != W - 1) {
        std::printf("[state] contract FAIL\n"); fails++;
    } else {
        std::printf("[state] FRAME/SCISSOR decoded OK\n");
    }

    // simpan PPM
    FILE* f = std::fopen("out/slice.ppm", "wb");
    if (f) {
        std::fprintf(f, "P6\n%u %u\n255\n", W, H);
        std::fwrite(fb.data(), 1, fb.size(), f);
        std::fclose(f);
        std::printf("[slice] saved out/slice.ppm (%ux%u)\n", W, H);
    }
    std::printf(fails ? "SLICE: %d FAIL\n" : "SLICE: ALL PASS\n", fails);
    { int rc = fails ? 1 : 0; std::printf("[phase] main-end rc=%d\n", rc); return rc; }
}
