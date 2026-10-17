// gs_state.h — GS register state machine (interception layer port God Hand)
// ID register TERVERIFIKASI dari gsKit resmi: ee/gs/include/gsInit.h (ps2dev/gsKit master)
#pragma once
#include <cstdint>

namespace gs {

enum Reg : uint32_t {
    PRIM        = 0x00,
    TEX0_1      = 0x06, TEX0_2   = 0x07,
    CLAMP_1     = 0x08, CLAMP_2  = 0x09,
    TEX1_1      = 0x14, TEX1_2   = 0x15,
    XYOFFSET_1  = 0x18, XYOFFSET_2 = 0x19,
    PRMODECONT  = 0x1A,
    TEXCLUT     = 0x1C,
    TEXA        = 0x3B,
    FOGCOL      = 0x3D,
    SCISSOR_1   = 0x40, SCISSOR_2 = 0x41,
    ALPHA_1     = 0x42, ALPHA_2  = 0x43,
    DIMX        = 0x44, DTHE     = 0x45,
    COLCLAMP    = 0x46,
    TEST_1      = 0x47, TEST_2   = 0x48,
    PABE        = 0x49,
    FBA_1       = 0x4A, FBA_2    = 0x4B,
    FRAME_1     = 0x4C, FRAME_2  = 0x4D,
    ZBUF_1      = 0x4E, ZBUF_2   = 0x4F,
    BITBLTBUF   = 0x50, TRXPOS   = 0x51,
    TRXREG      = 0x52, TRXDIR   = 0x53,
};

enum PrimType : int {
    P_POINT = 0, P_LINE = 1, P_LINESTRIP = 2,
    P_TRIANGLE = 3, P_TRISTRIP = 4, P_TRIFAN = 5, P_SPRITE = 6,
};

struct PrimState {
    int  type = P_TRIANGLE;
    bool iip = false, tme = false, fge = false, abe = false;
    bool aa1 = false, fst = false, ctxt = false, fix = false;
    uint16_t ctx = 0;
};

// Packing helpers (format nilai = gsKit GS_SETREG_*)
inline uint64_t pack_frame(uint32_t fbp, uint32_t fbw, uint32_t psm, uint64_t fbmsk) {
    return (uint64_t)fbp | ((uint64_t)fbw << 16) | ((uint64_t)psm << 24) | (fbmsk << 32);
}
inline uint64_t pack_scissor(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    return (uint64_t)x0 | ((uint64_t)x1 << 16) | ((uint64_t)y0 << 32) | ((uint64_t)y1 << 48);
}
inline uint64_t pack_xyoffset(uint16_t ofx, uint16_t ofy) {
    return (uint64_t)ofx | ((uint64_t)ofy << 16);
}
inline uint64_t pack_rgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a, float q = 1.0f) {
    uint32_t qbits; float qq = q;
    __builtin_memcpy(&qbits, &qq, 4);
    return (uint64_t)r | ((uint64_t)g << 8) | ((uint64_t)b << 16)
         | ((uint64_t)a << 24) | ((uint64_t)qbits << 32);
}
inline uint64_t pack_xyz2(uint16_t x, uint16_t y, uint32_t z) {
    return (uint64_t)x | ((uint64_t)y << 16) | ((uint64_t)z << 32);
}

struct State {
    // konteks gambar 1/2 (god hand pakai ctx 1)
    uint64_t frame[2]{}, zbuf[2]{}, scissor[2]{}, alpha[2]{}, test[2]{};
    uint32_t xyofx[2]{}, xyofy[2]{};
    uint64_t tex0[2]{};
    uint64_t prmodecont = 1;          // 1 = kendali dari reg PRIM
    uint64_t colclamp = 1;
    uint64_t rgbaq = 0x80808000ull;   // latch warna aktif (Q=1.0 implisit)
    PrimState prim{}, prmode{};

    // Terima satu A+D write. addr PRIM (0x00) = ganti state primitive.
    void ad_write(uint32_t addr, uint64_t data);

    // Akses ter-decode buat backend
    struct Frame  { uint32_t fbp, fbw, psm; uint64_t msb; };
    struct Scissor{ uint16_t x0, x1, y0, y1; };
    struct XYOff  { uint32_t x, y; };
    Frame   frame_of(int c) const;
    Scissor scissor_of(int c) const;
    XYOff   xyoffset_of(int c) const;
    const PrimState& active_prim() const { return prmodecont ? prim : prmode; }
};

} // namespace gs
