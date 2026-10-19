// slice/rotate_render.cpp — Render model Gene berputar 360° (bukti 3D real)
// Load vertex dari disc -> rotasi Y -> proyeksi -> Vulkan -> sequence PNG
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <fstream>

static const uint32_t W = 640, H = 448;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[rotate] render model Gene 360 derajat\n");

    std::ifstream vf("out/pl00_verts.bin", std::ios::binary);
    if (!vf) { std::printf("FAIL\n"); return 1; }
    uint32_t vcount = 0;
    vf.read(reinterpret_cast<char*>(&vcount), 4);
    std::vector<float> verts(vcount * 5);
    vf.read(reinterpret_cast<char*>(verts.data()), vcount * 5 * 4);
    std::printf("[rotate] %u vertices\n", vcount);

    uint32_t tri_count = vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    // render 12 frame (setiap 30 derajat)
    const int FRAMES = 12;
    for (int frame = 0; frame < FRAMES; frame++) {
        float angle = frame * (2.0f * 3.14159f / FRAMES);
        float cos_a = cosf(angle), sin_a = sinf(angle);

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
                float x = verts[idx], y = verts[idx+1], z = verts[idx+2];
                // rotasi Y
                float rx = x * cos_a - z * sin_a;
                float rz = x * sin_a + z * cos_a;
                // proyeksi
                float fov = 3.0f;
                float d = 3.0f - rz;
                if (d < 0.1f) d = 0.1f;
                float sx = (rx * fov / d + 1.0f) * 0.5f * W;
                float sy = (1.0f - y * fov / d) * 0.5f * H;

                vkbe::Vertex vx;
                vx.x = sx; vx.y = sy;
                vx.r = r; vx.g = g; vx.b = b; vx.a = 1.0f;
                batch.push_back(vx);
            }
        }

        // reuse instance: baru bikin di frame pertama
        static vkbe::Backend* vkb = nullptr;
        if (!vkb) vkb = new vkbe::Backend(W, H);
        vkb->begin_frame(0, 0, 32);
        vkb->draw_triangles(batch);
        auto pixels = vkb->end_frame();

        char path[64];
        snprintf(path, sizeof(path), "out/rotate_%02d.ppm", frame);
        FILE* fp = fopen(path, "wb");
        if (fp) {
            fprintf(fp, "P6\n%u %u\n255\n", W, H);
            fwrite(pixels.data(), 1, pixels.size(), fp);
            fclose(fp);
        }

        uint32_t nonbg = 0;
        for (uint32_t i = 0; i < W*H*3; i += 3)
            if (pixels[i] || pixels[i+1] || pixels[i+2]) nonbg++;
        std::printf("[rotate] frame %2d/%d: non-bg=%u (%.1f%%)\n",
                    frame+1, FRAMES, nonbg, (float)nonbg/(W*H)*100.0f);
    }
    std::printf("PASS: %d frames rendered\n", FRAMES);
    return 0;
}
