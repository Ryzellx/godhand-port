// vk_backend.h — backend Vulkan offscreen (slice vertical God Hand port)
// M2/M3: clear + triangle/sprite dari stream aksi GIF -> framebuffer dibaca balik.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace vkbe {

struct Vertex { float x, y; float r, g, b, a; };   // posisi SUDAH dalam piksel layar

class Backend {
public:
    Backend(uint32_t width, uint32_t height);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    // Mulai frame: clear warna (0-255) + scissor opsional dalam piksel
    void begin_frame(uint8_t cr, uint8_t cg, uint8_t cb,
                     int sx = 0, int sy = 0, int sw = -1, int sh = -1);
    // Gambar daftar vertex (jumlah genap; tri-strip/sprite sudah dipecah executor)
    void draw_triangles(const std::vector<Vertex>& verts);
    // Akhiri + baca balik hasil (RGB, urutan baris atas-ke-bawah)
    std::vector<uint8_t> end_frame();

    uint32_t width()  const { return m_w; }
    uint32_t height() const { return m_h; }

private:
    struct Impl; Impl* m_impl;
    uint32_t m_w, m_h;
};

} // namespace vkbe
