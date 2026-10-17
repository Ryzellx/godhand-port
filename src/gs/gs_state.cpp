#include "gs_state.h"

namespace gs {

void State::ad_write(uint32_t addr, uint64_t data) {
    switch (addr) {
    case PRIM: {
        prim.type = (int)(data & 7);
        prim.iip  = (data >> 3) & 1;
        prim.tme  = (data >> 4) & 1;
        prim.fge  = (data >> 5) & 1;
        prim.abe  = (data >> 6) & 1;
        prim.aa1  = (data >> 7) & 1;
        prim.fst  = (data >> 8) & 1;
        prim.ctxt = (data >> 9) & 1;
        prim.fix  = (data >> 10) & 1;
        prim.ctx  = prim.ctxt ? 1 : 0;
        break;
    }
    case PRMODECONT: prmodecont = data & 1; break;
    case COLCLAMP:   colclamp   = data & 1; break;
    // RGBAQ TIDAK punya alamat A+D — latch warna diperbarui executor dari aksi RGBA_WRITE
    case XYOFFSET_1: xyofx[0] = (uint32_t)(data & 0xFFFF); xyofy[0] = (uint32_t)((data >> 16) & 0xFFFF); break;
    case XYOFFSET_2: xyofx[1] = (uint32_t)(data & 0xFFFF); xyofy[1] = (uint32_t)((data >> 16) & 0xFFFF); break;
    case SCISSOR_1: scissor[0] = data; break;
    case SCISSOR_2: scissor[1] = data; break;
    case FRAME_1:   frame[0]   = data; break;
    case FRAME_2:   frame[1]   = data; break;
    case ZBUF_1:    zbuf[0]    = data; break;
    case ZBUF_2:    zbuf[1]    = data; break;
    case TEST_1:    test[0]    = data; break;
    case TEST_2:    test[1]    = data; break;
    case ALPHA_1:   alpha[0]   = data; break;
    case ALPHA_2:   alpha[1]   = data; break;
    case TEX0_1:    tex0[0]    = data; break;
    case TEX0_2:    tex0[1]    = data; break;
    default: break;   // register lain dicatat nanti saat butuh (TEX*, DIMX, dsb)
    }
}

State::Frame State::frame_of(int c) const {
    return Frame{ (uint32_t)(frame[c] & 0x1FF),
                  (uint32_t)((frame[c] >> 16) & 0x3F),
                  (uint32_t)((frame[c] >> 24) & 0x3F),
                  frame[c] >> 32 };
}

State::Scissor State::scissor_of(int c) const {
    return Scissor{ (uint16_t)(scissor[c] & 0x3FFF),
                    (uint16_t)((scissor[c] >> 16) & 0x3FFF),
                    (uint16_t)((scissor[c] >> 32) & 0x3FFF),
                    (uint16_t)((scissor[c] >> 48) & 0x3FFF) };
}

State::XYOff State::xyoffset_of(int c) const { return XYOff{ xyofx[c], xyofy[c] }; }

} // namespace gs
