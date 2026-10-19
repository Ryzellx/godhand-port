// godhand_full.cpp — God Hand PC Build dengan Animasi + Textured Model
// Software rendering — no Vulkan/DirectX dependency!
// Controls: Arrow keys rotate, +/- zoom, Space auto-rotate, A=toggle anim, ESC quit
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdio>
#include <ctime>

static const uint32_t W = 800, H = 600;

static std::vector<float> g_verts; // x,y,z,u,v per vertex
static uint32_t g_vcount = 0;
static uint32_t g_fb[W * H];
static BITMAPINFO g_bi = {};

static float g_rot_y = 0.0f;
static float g_rot_x = 0.0f;
static float g_zoom = 1.0f;
static bool g_auto_rotate = true;
static bool g_anim = true;
static float g_time = 0.0f;

// procedural animation: wave motion
static void animate(float* x, float* y, float* z, float t, float phase) {
    float wave = sinf(t * 2.0f + phase) * 0.05f;
    *y += wave;
    *x += sinf(t * 1.5f + phase * 0.7f) * 0.03f;
    *z += cosf(t * 1.8f + phase * 0.5f) * 0.02f;
}

static void render() {
    // clear
    for (uint32_t i = 0; i < W * H; i++) g_fb[i] = 0x00200000;

    float cos_y = cosf(g_rot_y), sin_y = sinf(g_rot_y);
    float cos_x = cosf(g_rot_x), sin_x = sinf(g_rot_x);
    uint32_t tri_count = g_vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base = t * 3;
        // texture-like coloring from UV
        float u = g_verts[base*5+3], v = g_verts[base*5+4];
        float nz = g_verts[base*5+2] - g_verts[(base+1)*5+2];
        uint8_t r = (uint8_t)(60 + u * 180);
        uint8_t g = (uint8_t)(40 + v * 12);
        uint8_t b = (uint8_t)(120 + nz * 60 + 60);
        if (g > 255) g = 255;

        int px[3], py[3];
        for (uint32_t vi = 0; vi < 3; vi++) {
            uint32_t idx = (base + vi) * 5;
            float x = g_verts[idx], y = g_verts[idx+1], z = g_verts[idx+2];

            // procedural animation
            if (g_anim) {
                float phase = y * 3.0f + t * 0.01f;
                animate(&x, &y, &z, g_time, phase);
            }

            // rotate
            float rx = x * cos_y - z * sin_y;
            float rz = x * sin_y + z * cos_y;
            float ry = y * cos_x - rz * sin_x;
            rz = y * sin_x + rz * cos_x;

            float d = 3.0f - rz * g_zoom;
            if (d < 0.1f) d = 0.1f;
            float sx = (rx * g_zoom * 3.0f / d + 1.0f) * 0.5f * W;
            float sy = (1.0f - ry * g_zoom * 3.0f / d) * 0.5f * H;
            px[vi] = (int)sx;
            py[vi] = (int)sy;
        }

        // rasterize
        int min_x = px[0], max_x = px[0], min_y = py[0], max_y = py[0];
        for (int i = 1; i < 3; i++) {
            if (px[i] < min_x) min_x = px[i];
            if (px[i] > max_x) max_x = px[i];
            if (py[i] < min_y) min_y = py[i];
            if (py[i] > max_y) max_y = py[i];
        }
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x >= (int)W) max_x = W - 1;
        if (max_y >= (int)H) max_y = H - 1;

        uint32_t color = (r << 16) | (g << 8) | b;
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                int d0 = (px[1]-px[0])*(y-py[0]) - (py[1]-py[0])*(x-px[0]);
                int d1 = (px[2]-px[1])*(y-py[1]) - (py[2]-py[1])*(x-px[1]);
                int d2 = (px[0]-px[2])*(y-py[2]) - (py[0]-py[2])*(x-px[2]);
                if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                    g_fb[y * W + x] = color;
                }
            }
        }
    }

    // HUD
    const char* hud = "GOD HAND - Native PC | ARROWS=Rotate +/-=Zoom SPACE=Auto A=Anim ESC=Quit";
    // simple text rendering (top-left)
    HDC hdc = GetDC(NULL); // just for font metrics
    ReleaseDC(NULL, hdc);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:  g_rot_y -= 0.1f; g_auto_rotate = false; break;
        case VK_RIGHT: g_rot_y += 0.1f; g_auto_rotate = false; break;
        case VK_UP:    g_rot_x -= 0.1f; g_auto_rotate = false; break;
        case VK_DOWN:  g_rot_x += 0.1f; g_auto_rotate = false; break;
        case VK_ADD: case VK_OEM_PLUS:  g_zoom *= 1.1f; break;
        case VK_SUBTRACT: case VK_OEM_MINUS: g_zoom *= 0.9f; break;
        case VK_SPACE: g_auto_rotate = !g_auto_rotate; break;
        case 'A': case 'a': g_anim = !g_anim; break;
        case VK_ESCAPE: PostQuitMessage(0); break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        StretchDIBits(hdc, 0, 0, W, H, 0, 0, W, H, g_fb, &g_bi, DIB_RGB_COLORS, SRCCOPY);
        // draw HUD text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        TextOut(hdc, 10, 10, "GOD HAND - Native PC Demo", 25);
        TextOut(hdc, 10, 30, "ARROWS=Rotate  +/-=Zoom  SPACE=Auto  A=Anim  ESC=Quit", 54);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    std::ifstream vf("battle_scene.bin", std::ios::binary);
    if (!vf) {
        MessageBox(NULL, "pl00_verts.bin not found!\nPut it next to the exe.", "God Hand", MB_OK);
        return 1;
    }
    vf.read(reinterpret_cast<char*>(&g_vcount), 4);
    g_verts.resize(g_vcount * 5);
    vf.read(reinterpret_cast<char*>(g_verts.data()), g_vcount * 5 * 4);

    g_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bi.bmiHeader.biWidth = W;
    g_bi.bmiHeader.biHeight = -H;
    g_bi.bmiHeader.biPlanes = 1;
    g_bi.bmiHeader.biBitCount = 32;
    g_bi.bmiHeader.biCompression = BI_RGB;

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "GodHandFull";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("GodHandFull", "God Hand — Native PC Build",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, W+16, H+59,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);

    MSG msg;
    clock_t last = clock();
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        clock_t now = clock();
        float dt = (float)(now - last) / CLOCKS_PER_SEC;
        last = now;
        g_time += dt;

        if (g_auto_rotate) g_rot_y += 0.01f;
        render();
        InvalidateRect(hwnd, NULL, FALSE);
        Sleep(16); // ~60 FPS
    }
    return 0;
}
