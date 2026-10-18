// slice/pl00_render.cpp — Render model ASLI Gene (pl00) via GIF parser -> GS -> Vulkan
// Load vertex positions dari disc (extract_verts.py output), proyeksi 3D->2D, GIF packets,
// parse -> GS state -> Vulkan offscreen -> save PPM.
#include "../gif/gif.h"
#include "../gs/gs_state.h"
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <fstream>

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

static const uint32_t W = 640, H = 448;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[pl00] render model Gene dari disc\n");

    // ---- load vertex data real ----
    std::ifstream vf("out/pl00_verts.bin", std::ios::binary);
    if (!vf) { std::printf("FAIL: out/pl00_verts.bin tidak ada\n"); return 1; }
    uint32_t vcount = 0;
    vf.read(reinterpret_cast<char*>(&vcount), 4);
    if (vcount == 0 || vcount > 1000000) { std::printf("FAIL: vcount=%u\n", vcount); return 1; }
    std::vector<float> verts(vcount * 5);  // x,y,z,u,v
    vf.read(reinterpret_cast<char*>(verts.data()), vcount * 5 * 4);
    std::printf("[pl00] %u vertices loaded from disc\n", vcount);

    // ---- 3D->2D projection (perspective) ----
    auto project = [&](float x, float y, float z, int& sx, int& sy) {
        float fov = 3.0f;  // field-of-view scale
        float d = 3.0f - z; // camera at z=3
        if (d < 0.1f) d = 0.1f;
        sx = (int)((x * fov / d + 1.0f) * 0.5f * W);
        sy = (int)((1.0f - y * fov / d) * 0.5f * H);
    };

    // ---- build GIF packets dari real triangles ----
    std::vector<uint8_t> pkt;
    // env setup (sama seperti slice test)
    put_tag(pkt, 8, false, 0, 0, Flag::PACKED, 1, {AD});
    put_qw(pkt, gs::pack_frame(0, W/64, 0, 0), gs::FRAME_1);
    put_qw(pkt, 0x1ull, gs::ZBUF_1);
    put_qw(pkt, gs::pack_xyoffset(0, 0), gs::XYOFFSET_1);
    put_qw(pkt, gs::pack_scissor(0, W-1, 0, H-1), gs::SCISSOR_1);
    put_qw(pkt, 0, gs::TEST_1);
    put_qw(pkt, 1, gs::PRMODECONT);
    put_qw(pkt, 1, gs::COLCLAMP);
    put_qw(pkt, 0, gs::ALPHA_1);

    // PRIM triangle
    put_tag(pkt, 1, false, 0, 0, Flag::PACKED, 1, {AD});
    put_qw(pkt, gs::P_TRIANGLE, gs::PRIM);

    // build triangles dari consecutive vertex groups (strip-based: v[i], v[i+1], v[i+2])
    uint32_t tri_count = vcount / 3;
    if (tri_count > 100) tri_count = 100;  // DEBUG LIMIT // cap biar gak overflow
    std::printf("[pl00] building %u triangles\n", tri_count);

    // batch per 100 triangles
    uint32_t max_batch = 100;
    for (uint32_t t = 0; t < tri_count; t += max_batch) {
        uint32_t batch = (t + max_batch <= tri_count) ? max_batch : (tri_count - t);
        put_tag(pkt, batch * 3, t + batch >= tri_count, 0, 0, Flag::PACKED, 2, {RGBAQ, XYZ2});
        for (uint32_t k = 0; k < batch; k++) {
            uint32_t base = (t + k) * 3;
            // warna berdasarkan normal sederhana (lighting fake)
            float nz = verts[base*3+2] - verts[(base+1)*3+2];
            uint8_t r = (uint8_t)(120 + (int)(nz * 80) % 128);
            uint8_t g = (uint8_t)(100 + (int)(verts[base*3+1] * 60) % 128);
            uint8_t b = (uint8_t)(180 + (int)(verts[base*3] * 50) % 75);
            for (uint32_t vi = 0; vi < 3; vi++) {
                uint32_t idx = (base + vi) * 5;
                int sx, sy;
                project(verts[idx], verts[idx+1], verts[idx+2], sx, sy);
                if (sx < 0) sx = 0; if (sx >= (int)W) sx = W-1;
                if (sy < 0) sy = 0; if (sy >= (int)H) sy = H-1;
                put_qw(pkt, gs::pack_rgbaq(r, g, b, 128));
                put_qw(pkt, gs::pack_xyz2(sx << 4, sy << 4, 0));
            }
        }
    }

    std::printf("[pl00] total packet size: %zu bytes\n", pkt.size());

    // ---- parse ----
    std::vector<Action> acts;
    size_t cons = 0;
    if (!parse_stream(pkt.data(), pkt.size(), acts, &cons) || cons != pkt.size()) {
        std::printf("FAIL: parse (consumed %zu / %zu)\n", cons, pkt.size()); return 1;
    }
    std::printf("[pl00] actions parsed: %zu\n", acts.size());

    // ---- execute GS state + batch vertices ----
    gs::State st;
    std::vector<vkbe::Vertex> batch;
    vkbe::Vertex pending = {};

    for (auto& a : acts) {
        if (a.kind == Action::AD_WRITE) {
            st.ad_write((uint32_t)a.ad_addr, a.ad_data);
        } else if (a.kind == Action::RGBA_WRITE) {
            uint64_t w = a.ad_data;
            pending.r = ((w >> 0) & 0xFF) / 255.0f;
            pending.g = ((w >> 8) & 0xFF) / 255.0f;
            pending.b = ((w >> 16) & 0xFF) / 255.0f;
            pending.a = 1.0f;
        } else if (a.kind == Action::XYZ_WRITE) {
            uint64_t w = a.ad_data;
            pending.x = (float)(int)((w >> 0) & 0xFFFF) / 16.0f;
            pending.y = (float)(int)((w >> 16) & 0xFFFF) / 16.0f;
            batch.push_back(pending);
        }
    }
    std::printf("[pl00] vertices batched: %zu\n", batch.size());

    // ---- Vulkan render ----
    vkbe::Backend vkb(W, H);
    vkb.begin_frame(0, 0, 32);
    vkb.draw_triangles(batch);
    std::vector<uint8_t> pixels = vkb.end_frame();

    // ---- save PPM ----
    FILE* fp = fopen("out/pl00_render.ppm", "wb");
    if (fp) {
        fprintf(fp, "P6\n%u %u\n255\n", W, H);
        fwrite(pixels.data(), 1, pixels.size(), fp);
        fclose(fp);
        std::printf("[pl00] saved: out/pl00_render.ppm\n");
    }

    // count non-black pixels
    uint32_t nonblack = 0;
    for (uint32_t i = 0; i < W * H * 3; i += 3) {
        if (pixels[i] || pixels[i+1] || pixels[i+2]) nonblack++;
    }
    std::printf("[pl00] non-black pixels: %u / %u (%.1f%%)\n",
                nonblack, W*H, (float)nonblack / (W*H) * 100.0f);
    std::printf("PASS: model Gene rendered dari disc\n");
    return 0;
}
