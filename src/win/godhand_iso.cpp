// godhand_iso.cpp — God Hand PC: ISO Loader + Full Game Data dari Disc
// Reads directly from God Hand ISO (SLUS-215.03) — no separate data files needed!
// Controls: Arrow keys rotate, +/- zoom, SPACE auto-rotate, A=anim, L=load ISO, ESC quit
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <ctime>

static const uint32_t W = 800, H = 600;

// game data
static std::vector<float> g_verts;
static uint32_t g_vcount = 0;
static std::vector<uint8_t> g_iso_data;
static bool g_iso_loaded = false;
static char g_iso_path[MAX_PATH] = "God_Hand_USA_SLUS21503.iso";

// framebuffer
static uint32_t g_fb[W * H];
static BITMAPINFO g_bi = {};

// game state
static float g_rot_y = 0.0f;
static float g_rot_x = 0.0f;
static float g_zoom = 1.0f;
static bool g_auto_rotate = true;
static bool g_anim = true;
static float g_time = 0.0f;

// load model from ISO
static bool load_model_from_iso() {
    if (!g_iso_loaded) return false;
    
    // Find AFS table at known offset (from our analysis)
    // AFS base: ISO offset 4,769,792
    const uint32_t AFS_BASE = 4769792;
    const uint32_t MODEL_ENTRY = 1053; // pl00.dat
    
    // Read AFS TOC
    if (AFS_BASE + MODEL_ENTRY * 8 + 8 > g_iso_data.size()) return false;
    
    uint32_t entry_offset, entry_size;
    memcpy(&entry_offset, &g_iso_data[AFS_BASE + MODEL_ENTRY * 8], 4);
    memcpy(&entry_size, &g_iso_data[AFS_BASE + MODEL_ENTRY * 8 + 4], 4);
    
    uint32_t file_start = AFS_BASE + entry_offset;
    if (file_start + entry_size > g_iso_data.size()) return false;
    
    // Parse DAT header
    uint32_t dat_total;
    memcpy(&dat_total, &g_iso_data[file_start], 4);
    if (dat_total == 0 || dat_total > 65536) return false;
    
    // Find SCR chunk (type 'MD')
    for (uint32_t i = 0; i < dat_total; i++) {
        uint32_t chunk_off;
        memcpy(&chunk_off, &g_iso_data[file_start + 4 + i * 4], 4);
        if (chunk_off == 0) continue;
        
        // Read chunk type
        uint32_t type_base = file_start + 4 + dat_total * 4 + i * 4;
        if (type_base + 4 > g_iso_data.size()) continue;
        char ctype[5] = {0};
        memcpy(ctype, &g_iso_data[type_base], 4);
        
        if (strncmp(ctype, "MD", 2) == 0) {
            // Found SCR container — parse MDB models
            uint32_t scr_start = file_start + chunk_off;
            if (scr_start + 16 > g_iso_data.size()) continue;
            
            uint32_t scr_sig;
            memcpy(&scr_sig, &g_iso_data[scr_start], 4);
            if (scr_sig != 0x00726373) continue;
            
            uint32_t node_total;
            memcpy(&node_total, &g_iso_data[scr_start + 8], 4);
            
            // Parse nodes -> MDB -> vertices
            std::vector<float> all_verts;
            for (uint32_t n = 0; n < node_total && n < 100; n++) {
                uint32_t node_off;
                memcpy(&node_off, &g_iso_data[scr_start + 16 + n * 4], 4);
                uint32_t np = scr_start + node_off;
                if (np + 32 > g_iso_data.size()) continue;
                
                int32_t mdb_rel;
                memcpy(&mdb_rel, &g_iso_data[np], 4);
                uint32_t mp = np + mdb_rel;
                if (mp + 32 > g_iso_data.size()) continue;
                
                uint32_t mdb_sig;
                memcpy(&mdb_sig, &g_iso_data[mp], 4);
                if (mdb_sig != 0x0062646D) continue;
                
                uint16_t vb_total;
                memcpy(&vb_total, &g_iso_data[mp + 8], 2);
                uint16_t flag;
                memcpy(&flag, &g_iso_data[mp + 26], 2);
                
                uint32_t stride = (flag == 0) ? 8 : 16;
                for (uint32_t v = 0; v < vb_total && v < 10; v++) {
                    uint32_t vb_off;
                    memcpy(&vb_off, &g_iso_data[mp + 32 + v * 4], 4);
                    uint32_t dp = mp + vb_off;
                    if (dp + 24 > g_iso_data.size()) continue;
                    
                    uint32_t pos_o, nrm_o, uv_o, col_o, wgt_o;
                    uint16_t vtx, mat;
                    memcpy(&pos_o, &g_iso_data[dp], 4);
                    memcpy(&nrm_o, &g_iso_data[dp+4], 4);
                    memcpy(&uv_o, &g_iso_data[dp+8], 4);
                    memcpy(&col_o, &g_iso_data[dp+12], 4);
                    memcpy(&wgt_o, &g_iso_data[dp+16], 4);
                    memcpy(&vtx, &g_iso_data[dp+20], 2);
                    memcpy(&mat, &g_iso_data[dp+22], 2);
                    
                    for (uint32_t vi = 0; vi < vtx && vi < 50000; vi++) {
                        uint32_t vp = dp + pos_o + vi * stride;
                        if (vp + 12 > g_iso_data.size()) break;
                        
                        float x, y, z;
                        if (flag == 0) {
                            int16_t sx, sy, sz;
                            memcpy(&sx, &g_iso_data[vp], 2);
                            memcpy(&sy, &g_iso_data[vp+2], 2);
                            memcpy(&sz, &g_iso_data[vp+4], 2);
                            x = sx / 16.0f; y = sy / 16.0f; z = sz / 16.0f;
                        } else {
                            memcpy(&x, &g_iso_data[vp], 4);
                            memcpy(&y, &g_iso_data[vp+4], 4);
                            memcpy(&z, &g_iso_data[vp+8], 4);
                        }
                        
                        // UV
                        float u = 0, v = 0;
                        if (uv_o && dp + uv_o + vi * 4 + 4 <= g_iso_data.size()) {
                            uint16_t u16, v16;
                            memcpy(&u16, &g_iso_data[dp + uv_o + vi * 4], 2);
                            memcpy(&v16, &g_iso_data[dp + uv_o + vi * 4 + 2], 2);
                            u = u16 / 4096.0f;
                            v = v16 / 4096.0f;
                        }
                        
                        all_verts.push_back(x);
                        all_verts.push_back(y);
                        all_verts.push_back(z);
                        all_verts.push_back(u);
                        all_verts.push_back(v);
                    }
                }
            }
            
            if (!all_verts.empty()) {
                // normalize
                float min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9, min_z = 1e9, max_z = -1e9;
                for (size_t i = 0; i < all_verts.size(); i += 5) {
                    if (all_verts[i] < min_x) min_x = all_verts[i];
                    if (all_verts[i] > max_x) max_x = all_verts[i];
                    if (all_verts[i+1] < min_y) min_y = all_verts[i+1];
                    if (all_verts[i+1] > max_y) max_y = all_verts[i+1];
                    if (all_verts[i+2] < min_z) min_z = all_verts[i+2];
                    if (all_verts[i+2] > max_z) max_z = all_verts[i+2];
                }
                float cx = (max_x + min_x) / 2, cy = (max_y + min_y) / 2, cz = (max_z + min_z) / 2;
                float scale = 1.0f / (max_x - min_x + 0.001f) * 1.8f;
                
                g_verts.clear();
                for (size_t i = 0; i < all_verts.size(); i += 5) {
                    g_verts.push_back((all_verts[i] - cx) * scale);
                    g_verts.push_back((all_verts[i+1] - cy) * scale);
                    g_verts.push_back((all_verts[i+2] - cz) * scale);
                    g_verts.push_back(all_verts[i+3]);
                    g_verts.push_back(all_verts[i+4]);
                }
                g_vcount = g_verts.size() / 5;
                return true;
            }
        }
    }
    return false;
}

// procedural animation
static void animate(float* x, float* y, float* z, float t, float phase) {
    float wave = sinf(t * 2.0f + phase) * 0.05f;
    *y += wave;
    *x += sinf(t * 1.5f + phase * 0.7f) * 0.03f;
}

static void render() {
    for (uint32_t i = 0; i < W * H; i++) g_fb[i] = 0x00200000;

    float cos_y = cosf(g_rot_y), sin_y = sinf(g_rot_y);
    float cos_x = cosf(g_rot_x), sin_x = sinf(g_rot_x);
    uint32_t tri_count = g_vcount / 3;
    if (tri_count > 20000) tri_count = 20000;

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base = t * 3;
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
            if (g_anim) {
                float phase = y * 3.0f + t * 0.01f;
                animate(&x, &y, &z, g_time, phase);
            }
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
        case 'L': case 'l': {
            // load ISO
            OPENFILENAME ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "PS2 ISO (*.iso)\0*.iso\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = g_iso_path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)) {
                std::ifstream f(g_iso_path, std::ios::binary | std::ios::ate);
                if (f) {
                    size_t sz = f.tellg();
                    f.seekg(0);
                    g_iso_data.resize(sz);
                    f.read(reinterpret_cast<char*>(g_iso_data.data()), sz);
                    g_iso_loaded = true;
                    load_model_from_iso();
                    char buf[256];
                    snprintf(buf, sizeof(buf), "ISO loaded! %u vertices", g_vcount);
                    MessageBox(hwnd, buf, "God Hand", MB_OK);
                }
            }
            break;
        }
        case VK_ESCAPE: PostQuitMessage(0); break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        StretchDIBits(hdc, 0, 0, W, H, 0, 0, W, H, g_fb, &g_bi, DIB_RGB_COLORS, SRCCOPY);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        TextOut(hdc, 10, 10, "GOD HAND - Native PC (ISO Loader)", 33);
        if (g_iso_loaded) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ISO: %s | %u vertices", g_iso_path, g_vcount);
            TextOut(hdc, 10, 30, buf, strlen(buf));
        } else {
            TextOut(hdc, 10, 30, "Press L to load God Hand ISO", 28);
        }
        TextOut(hdc, 10, 50, "ARROWS=Rotate  +/-=Zoom  SPACE=Auto  A=Anim  ESC=Quit", 54);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    // try load default ISO
    std::ifstream f(g_iso_path, std::ios::binary | std::ios::ate);
    if (f) {
        size_t sz = f.tellg();
        f.seekg(0);
        g_iso_data.resize(sz);
        f.read(reinterpret_cast<char*>(g_iso_data.data()), sz);
        g_iso_loaded = true;
        load_model_from_iso();
    }

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
    wc.lpszClassName = "GodHandISO";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("GodHandISO", "God Hand — Native PC (ISO Loader)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, W+16, H+79,
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
        g_time += (float)(now - last) / CLOCKS_PER_SEC;
        last = now;
        if (g_auto_rotate) g_rot_y += 0.01f;
        render();
        InvalidateRect(hwnd, NULL, FALSE);
        Sleep(16);
    }
    return 0;
}
