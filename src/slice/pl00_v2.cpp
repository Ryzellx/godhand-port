// slice/pl00_v2.cpp — Render model Gene via GIF pipeline (SATU tag besar)
// Fix: batch parser stop di tag pertama -> semua vertex dalam satu GIF PACKED tag
#include "../gif/gif.h"
#include "../gs/gs_state.h"
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <fstream>

using namespace gif;

static void put_tag(std::vector<uint8_t>& v, uint32_t nloop, bool eop,
                    Flag flg, uint32_t nreg, const std::vector<uint8_t>& regs) {
    uint64_t w0 = (nloop & 0x7FFFull) | ((uint64_t)eop << 15)
                | ((uint64_t)(uint32_t)flg << 28) | ((uint64_t)(nreg & 0xF) << 30);
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

static const uint32_t W = 640, H = 448;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[pl00v2] render via GIF pipeline\n");

    std::ifstream vf("out/pl00_verts.bin", std::ios::binary);
    if (!vf) { std::printf("FAIL\n"); return 1; }
    uint32_t vcount = 0;
    vf.read(reinterpret_cast<char*>(&vcount), 4);
    std::vector<float> verts(vcount * 5);
    vf.read(reinterpret_cast<char*>(verts.data()), vcount * 5 * 4);
    std::printf("[pl00v2] %u vertices\n", vcount);

    auto project = [&](float x, float y, float z, float& sx, float& sy) {
        float d = 3.0f - z;
        if (d < 0.1f) d = 0.1f;
        sx = (x * 3.0f / d + 1.0f) * 0.5f * W;
        sy = (1.0f - y * 3.0f / d) * 0.5f * H;
    };

    uint32_t tri_count = vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    // build GIF packet: env + PRIM + SATU vertex tag
    std::vector<uint8_t> pkt;
    // env
    put_tag(pkt, 8, false, Flag::PACKED, 1, {gif::AD});
    put_qw(pkt, gs::pack_frame(0, W/64, 0, 0), gs::FRAME_1);
    put_qw(pkt, 0x1ull, gs::ZBUF_1);
    put_qw(pkt, gs::pack_xyoffset(0, 0), gs::XYOFFSET_1);
    put_qw(pkt, gs::pack_scissor(0, W-1, 0, H-1), gs::SCISSOR_1);
    put_qw(pkt, 0, gs::TEST_1);
    put_qw(pkt, 1, gs::PRMODECONT);
    put_qw(pkt, 1, gs::COLCLAMP);
    put_qw(pkt, 0, gs::ALPHA_1);
    // PRIM
    put_tag(pkt, 1, false, Flag::PACKED, 1, {gif::AD});
    put_qw(pkt, gs::P_TRIANGLE, gs::PRIM);
    // SATU tag untuk SEMUA vertex
    put_tag(pkt, tri_count * 3, true, Flag::PACKED, 2, {gif::RGBAQ, gif::XYZ2});
    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base = t * 3;
        float nz = verts[base*5+2] - verts[(base+1)*5+2];
        uint8_t r = (uint8_t)(120 + ((int)(nz*80) & 0x7F));
        uint8_t g = (uint8_t)(100 + ((int)(verts[base*5+1]*60) & 0x7F));
        uint8_t b = (uint8_t)(180 + ((int)(verts[base*5]*50) % 75));
        for (uint32_t vi = 0; vi < 3; vi++) {
            uint32_t idx = (base + vi) * 5;
            float sx, sy;
            project(verts[idx], verts[idx+1], verts[idx+2], sx, sy);
            int isx = (int)sx, isy = (int)sy;
            if (isx < 0) isx = 0; if (isx >= (int)W) isx = W-1;
            if (isy < 0) isy = 0; if (isy >= (int)H) isy = H-1;
            put_qw(pkt, gs::pack_rgbaq(r, g, b, 128));
            put_qw(pkt, gs::pack_xyz2((uint16_t)(isx << 4), (uint16_t)(isy << 4), 0));
        }
    }
    std::printf("[pl00v2] packet: %zu bytes\n", pkt.size());

    // parse
    std::vector<Action> acts;
    size_t cons = 0;
    if (!parse_stream(pkt.data(), pkt.size(), acts, &cons) || cons != pkt.size()) {
        std::printf("FAIL: parse (%zu / %zu)\n", cons, pkt.size()); return 1;
    }
    std::printf("[pl00v2] actions: %zu\n", acts.size());

    // execute GS + collect vertices
    // gs::State st; // skip untuk debug
    std::vector<vkbe::Vertex> batch;
    vkbe::Vertex pending = {};
    uint32_t xyz_count = 0;
    for (auto& a : acts) {
        switch (a.kind) {
        case Action::AD_WRITE: break; // skip GS state untuk debug
        case Action::RGBA_WRITE: {
            uint64_t w = a.ad_data;
            pending.r = ((w >> 0) & 0xFF) / 255.0f;
            pending.g = ((w >> 8) & 0xFF) / 255.0f;
            pending.b = ((w >> 16) & 0xFF) / 255.0f;
            pending.a = 1.0f;
            break;
        }
        case Action::XYZ_WRITE: {
            uint64_t w = a.ad_data;
            pending.x = (float)(int)((w >> 0) & 0xFFFF) / 16.0f;
            pending.y = (float)(int)((w >> 16) & 0xFFFF) / 16.0f;
            batch.push_back(pending);
            xyz_count++;
            break;
        }
        default: break;
        }
    }
    acts.clear(); acts.shrink_to_fit();
    pkt.clear(); pkt.shrink_to_fit();
    std::printf("[pl00v2] vertices: %zu (xyz_count=%u)\n", batch.size(), xyz_count);
    for (size_t i = 0; i < 6 && i < batch.size(); i++)
        std::printf("[pl00v2] v[%zu] x=%.1f y=%.1f r=%.2f g=%.2f b=%.2f\n",
            i, batch[i].x, batch[i].y, batch[i].r, batch[i].g, batch[i].b);

    // render
    vkbe::Backend vkb(W, H);
    vkb.begin_frame(0, 0, 32);
    vkb.draw_triangles(batch);
    auto pixels = vkb.end_frame();

    FILE* fp = fopen("out/pl00_v2.ppm", "wb");
    if (fp) { fprintf(fp, "P6\n%u %u\n255\n", W, H); fwrite(pixels.data(), 1, pixels.size(), fp); fclose(fp); }

    uint32_t nonbg = 0;
    for (uint32_t i = 0; i < W*H*3; i += 3)
        if (pixels[i] || pixels[i+1] || pixels[i+2]) nonbg++;
    std::printf("[pl00v2] non-bg: %u (%.1f%%)\n", nonbg, (float)nonbg/(W*H)*100.0f);
    std::printf("PASS\n");
    return 0;
}
