// slice/game_demo.cpp — PLAYABLE DEMO: Model Gene interactive + animation
// Keyboard: arrow keys rotate model, space = auto-rotate toggle
// Render: Vulkan offscreen -> PPM (headless) atau window (future)
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <fstream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

static const uint32_t W = 640, H = 448;

struct GameState {
    float rot_y = 0.0f;
    float rot_x = 0.0f;
    float zoom = 1.0f;
    bool auto_rotate = true;
    int frame = 0;
};

// non-blocking keyboard input
static int kbhit() {
    struct termios oldt, newt;
    int ch, oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    return ch;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[demo] === GOD HAND PLAYABLE DEMO ===\n");
    std::printf("[demo] Controls: A/D=rotasi Y, W/S=rotasi X, Q/E=zoom, SPACE=auto-rotate, X=quit\n\n");

    // load model
    std::ifstream vf("out/pl00_verts.bin", std::ios::binary);
    if (!vf) { std::printf("FAIL: no model\n"); return 1; }
    uint32_t vcount = 0;
    vf.read(reinterpret_cast<char*>(&vcount), 4);
    std::vector<float> verts(vcount * 5);
    vf.read(reinterpret_cast<char*>(verts.data()), vcount * 5 * 4);
    std::printf("[demo] %u vertices loaded\n", vcount);

    uint32_t tri_count = vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    GameState gs;
    vkbe::Backend vkb(W, H);

    std::printf("[demo] starting interactive loop...\n");

    // main game loop
    for (int iter = 0; iter < 1000000; iter++) {
        gs.frame++;

        // keyboard input
        int ch = kbhit();
        if (ch != -1) {
            switch (ch) {
            case 'a': case 'A': gs.rot_y -= 0.1f; gs.auto_rotate = false; break;
            case 'd': case 'D': gs.rot_y += 0.1f; gs.auto_rotate = false; break;
            case 'w': case 'W': gs.rot_x -= 0.1f; gs.auto_rotate = false; break;
            case 's': case 'S': gs.rot_x += 0.1f; gs.auto_rotate = false; break;
            case 'q': case 'Q': gs.zoom *= 1.1f; break;
            case 'e': case 'E': gs.zoom *= 0.9f; break;
            case ' ': gs.auto_rotate = !gs.auto_rotate; break;
            case 'x': case 'X': std::printf("[demo] quit\n"); return 0;
            }
        }

        if (gs.auto_rotate) gs.rot_y += 0.02f;

        // render
        float cos_y = cosf(gs.rot_y), sin_y = sinf(gs.rot_y);
        float cos_x = cosf(gs.rot_x), sin_x = sinf(gs.rot_x);

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
                // rotate Y then X
                float rx = x * cos_y - z * sin_y;
                float rz = x * sin_y + z * cos_y;
                float ry = y * cos_x - rz * sin_x;
                rz = y * sin_x + rz * cos_x;
                // project with zoom
                float d = 3.0f - rz * gs.zoom;
                if (d < 0.1f) d = 0.1f;
                float sx = (rx * gs.zoom * 3.0f / d + 1.0f) * 0.5f * W;
                float sy = (1.0f - ry * gs.zoom * 3.0f / d) * 0.5f * H;

                vkbe::Vertex vx;
                vx.x = sx; vx.y = sy;
                vx.r = r; vx.g = g; vx.b = b; vx.a = 1.0f;
                batch.push_back(vx);
            }
        }

        vkb.begin_frame(0, 0, 32);
        vkb.draw_triangles(batch);
        auto pixels = vkb.end_frame();

        // save frame every 30 iterations (about 1 per second)
        if (iter % 30 == 0) {
            char path[64];
            snprintf(path, sizeof(path), "out/demo_frame_%04d.ppm", iter / 30);
            FILE* fp = fopen(path, "wb");
            if (fp) {
                fprintf(fp, "P6\n%u %u\n255\n", W, H);
                fwrite(pixels.data(), 1, pixels.size(), fp);
                fclose(fp);
            }
            std::printf("[demo] frame %d (rot_y=%.2f) -> %s\n", gs.frame, gs.rot_y, path);
        }
    }
    std::printf("[demo] done — %d frames rendered\n", gs.frame);
    return 0;
}
