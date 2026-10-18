// slice/direct_render.cpp — Render model Gene LANGSUNG ke Vulkan (bypass GIF parser)
// Load vertex dari disc -> proyeksi 3D -> vkbe::Vertex -> Vulkan -> PPM/PNG
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <fstream>

static const uint32_t W = 640, H = 448;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[direct] render model Gene dari disc\n");

    std::ifstream vf("out/pl00_verts.bin", std::ios::binary);
    if (!vf) { std::printf("FAIL: out/pl00_verts.bin\n"); return 1; }
    uint32_t vcount = 0;
    vf.read(reinterpret_cast<char*>(&vcount), 4);
    std::vector<float> verts(vcount * 5);  // x,y,z,u,v
    vf.read(reinterpret_cast<char*>(verts.data()), vcount * 5 * 4);
    std::printf("[direct] %u vertices loaded\n", vcount);

    // projection: normalized 3D -> screen pixels
    auto project = [&](float x, float y, float z, float& sx, float& sy) {
        float fov = 3.0f;
        float d = 3.0f - z;
        if (d < 0.1f) d = 0.1f;
        sx = (x * fov / d + 1.0f) * 0.5f * W;
        sy = (1.0f - y * fov / d) * 0.5f * H;
    };

    // build triangles
    uint32_t tri_count = vcount / 3;
    if (tri_count > 20000) tri_count = 20000;
    std::printf("[direct] %u triangles\n", tri_count);

    std::vector<vkbe::Vertex> batch;
    batch.reserve(tri_count * 3);
    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base = t * 3;
        float nz = verts[base*5+2] - verts[(base+1)*5+2];
        float r = 0.47f + nz * 0.3f;
        float g = 0.39f + verts[base*5+1] * 0.25f;
        float b = 0.71f + verts[base*5] * 0.2f;
        if (r < 0) r = 0; if (r > 1) r = 1;
        if (g < 0) g = 0; if (g > 1) g = 1;
        if (b < 0) b = 0; if (b > 1) b = 1;
        for (uint32_t vi = 0; vi < 3; vi++) {
            uint32_t idx = (base + vi) * 5;
            float sx, sy;
            project(verts[idx], verts[idx+1], verts[idx+2], sx, sy);
            vkbe::Vertex vx;
            vx.x = sx; vx.y = sy;
            vx.r = r; vx.g = g; vx.b = b; vx.a = 1.0f;
            batch.push_back(vx);
        }
    }
    std::printf("[direct] %zu vertices batched\n", batch.size());

    // render
    vkbe::Backend vkb(W, H);
    vkb.begin_frame(0, 0, 32);
    vkb.draw_triangles(batch);
    std::vector<uint8_t> pixels = vkb.end_frame();

    // save PPM
    FILE* fp = fopen("out/direct_render.ppm", "wb");
    if (fp) {
        fprintf(fp, "P6\n%u %u\n255\n", W, H);
        fwrite(pixels.data(), 1, pixels.size(), fp);
        fclose(fp);
        std::printf("[direct] saved: out/direct_render.ppm\n");
    }

    uint32_t nonbg = 0;
    for (uint32_t i = 0; i < W*H*3; i += 3)
        if (pixels[i] || pixels[i+1] || pixels[i+2]) nonbg++;
    std::printf("[direct] non-bg: %u / %u (%.1f%%)\n", nonbg, W*H, (float)nonbg/(W*H)*100.0f);
    std::printf("PASS: direct render\n");
    return 0;
}
