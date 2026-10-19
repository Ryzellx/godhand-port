// godhand_demo_win.cpp — God Hand Playable Demo untuk Windows
// Software rendering (no Vulkan dependency) — double-click to run!
// Controls: Arrow keys rotate, +/- zoom, Space auto-rotate, ESC quit
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdio>

static const uint32_t W = 800, H = 600;

struct Vec3 { float x, y, z; };
struct Color { uint8_t r, g, b; };

// 3D model data (loaded from embedded data or file)
static std::vector<float> g_verts; // x,y,z,u,v per vertex
static uint32_t g_vcount = 0;

// framebuffer
static uint32_t g_fb[W * H];
static BITMAPINFO g_bi = {};

// game state
static float g_rot_y = 0.0f;
static float g_rot_x = 0.0f;
static float g_zoom = 1.0f;
static bool g_auto_rotate = true;

// render software
static void render() {
    // clear
    for (uint32_t i = 0; i < W * H; i++) g_fb[i] = 0x00200000; // dark blue

    float cos_y = cosf(g_rot_y), sin_y = sinf(g_rot_y);
    float cos_x = cosf(g_rot_x), sin_x = sinf(g_rot_x);
    uint32_t tri_count = g_vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base = t * 3;
        // vertex colors from normal
        float nz = g_verts[base*5+2] - g_verts[(base+1)*5+2];
        uint8_t r = (uint8_t)(120 + ((int)(nz*80) & 0x7F));
        uint8_t g = (uint8_t)(100 + ((int)(g_verts[base*5+1]*60) & 0x7F));
        uint8_t b = (uint8_t)(180 + ((int)(g_verts[base*5]*50) % 75));

        int px[3], py[3];
        for (uint32_t vi = 0; vi < 3; vi++) {
            uint32_t idx = (base + vi) * 5;
            float x = g_verts[idx], y = g_verts[idx+1], z = g_verts[idx+2];
            // rotate Y then X
            float rx = x * cos_y - z * sin_y;
            float rz = x * sin_y + z * cos_y;
            float ry = y * cos_x - rz * sin_x;
            rz = y * sin_x + rz * cos_x;
            // project
            float d = 3.0f - rz * g_zoom;
            if (d < 0.1f) d = 0.1f;
            float sx = (rx * g_zoom * 3.0f / d + 1.0f) * 0.5f * W;
            float sy = (1.0f - ry * g_zoom * 3.0f / d) * 0.5f * H;
            px[vi] = (int)sx;
            py[vi] = (int)sy;
        }

        // rasterize triangle (simple fill)
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
                // barycentric test
                int d0 = (px[1]-px[0])*(y-py[0]) - (py[1]-py[0])*(x-px[0]);
                int d1 = (px[2]-px[1])*(y-py[1]) - (py[2]-py[1])*(x-px[1]);
                int d2 = (px[0]-px[2])*(y-py[2]) - (py[0]-py[2])*(x-px[2]);
                if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                    g_fb[y * W + x] = color;
                }
            }
        }
    }
}

// window proc
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
        case VK_ESCAPE: PostQuitMessage(0); break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        StretchDIBits(hdc, 0, 0, W, H, 0, 0, W, H, g_fb, &g_bi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    // load model from file (same directory as exe)
    std::ifstream vf("pl00_verts.bin", std::ios::binary);
    if (!vf) {
        MessageBox(NULL, "pl00_verts.bin not found!\nPut it next to the exe.", "Error", MB_OK);
        return 1;
    }
    vf.read(reinterpret_cast<char*>(&g_vcount), 4);
    g_verts.resize(g_vcount * 5);
    vf.read(reinterpret_cast<char*>(g_verts.data()), g_vcount * 5 * 4);

    // setup bitmap
    g_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bi.bmiHeader.biWidth = W;
    g_bi.bmiHeader.biHeight = -H; // top-down
    g_bi.bmiHeader.biPlanes = 1;
    g_bi.bmiHeader.biBitCount = 32;
    g_bi.bmiHeader.biCompression = BI_RGB;

    // register window
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "GodHandDemo";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("GodHandDemo", "God Hand — Native PC Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, W+16, H+39,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);

    // main loop
    MSG msg;
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (g_auto_rotate) g_rot_y += 0.01f;
        render();
        InvalidateRect(hwnd, NULL, FALSE);
        Sleep(33); // ~30 FPS
    }
    return 0;
}
