#include "gif.h"
#include <cstring>

namespace gif {

Tag decode_tag(const uint8_t* q) {
    // dua little-endian u64: word0 = kontrol, word1 = REGS
    uint64_t w0, w1;
    std::memcpy(&w0, q, 8);
    std::memcpy(&w1, q + 8, 8);
    Tag t{};
    t.nloop = (uint32_t)(w0 & 0x7FFF);
    t.eop   = (w0 >> 15) & 1;
    t.pre   = (w0 >> 16) & 1;
    t.prim  = (uint32_t)((w0 >> 17) & 0x7FF);
    t.flg   = (Flag)((w0 >> 28) & 0x3);
    t.nreg  = (uint32_t)((w0 >> 30) & 0xF);
    for (int i = 0; i < 16; i++)
        t.regs[i] = (uint8_t)((w1 >> (i * 4)) & 0xF);
    return t;
}

size_t payload_qwords(const Tag& t) {
    size_t nreg = t.nreg == 0 ? 16 : t.nreg;
    switch (t.flg) {
    case Flag::PACKED:  return t.nloop * nreg;  // MVP: 1 qword per register (lihat catatan gif.h)
    case Flag::REGLIST:                          // PCSX2 Gif_Unit.h: 2 nilai 64-bit per qword
        return (t.nloop * nreg + 1) >> 1;
    case Flag::IMAGE:   return t.nloop;
    }
    // FLG=3 (IMAGE2 di PCSX2) — perlakukan sebagai IMAGE
    return t.nloop;
}

static inline uint64_t rd64(const uint8_t* p) {
    uint64_t v; std::memcpy(&v, p, 8); return v;
}

bool parse_stream(const uint8_t* buf, size_t len, std::vector<Action>& out, size_t* consumed) {
    size_t off = 0;
    while (off + 16 <= len) {
        Tag t = decode_tag(buf + off);
        size_t nreg = t.nreg == 0 ? 16 : t.nreg;
        if (t.flg == (Flag)3) t.flg = Flag::IMAGE;  // IMAGE2 alias (PCSX2)
        off += 16;
        size_t pay = payload_qwords(t);
        if (off + pay * 16 > len) return false;     // truncated

        // PRE=1 (PACKED): PRIM dikirim bareng tag
        if (t.pre && t.flg == Flag::PACKED) {
            Action a{}; a.kind = Action::PRIM_WRITE; a.prim = t.prim;
            out.push_back(a);
        }

        const uint8_t* p = buf + off;
        switch (t.flg) {
        case Flag::PACKED: {
            size_t idx = 0;
            for (uint32_t loop = 0; loop < t.nloop; loop++) {
                for (size_t r = 0; r < nreg; r++, idx++) {
                    uint8_t reg = t.regs[r];
                    uint64_t w = rd64(p + idx * 16);
                    Action a{};
                    switch (reg) {
                    case AD:                       // addr di word1 (bit 0-10), data di word0
                        a.kind = Action::AD_WRITE;
                        a.ad_data = w;
                        a.ad_addr = rd64(p + idx * 16 + 8) & 0x7FF;
                        out.push_back(a);
                        break;
                    case PRIM:
                        a.kind = Action::PRIM_WRITE; a.prim = (uint32_t)(w & 0x7FF);
                        out.push_back(a);
                        break;
                    case RGBAQ:
                        a.kind = Action::RGBA_WRITE; a.ad_data = w;
                        out.push_back(a);
                        break;
                    case ST: case UV:
                        a.kind = Action::UV_WRITE; a.ad_data = w;
                        out.push_back(a);
                        break;
                    case XYZ2: case XYZF2:
                        a.kind = Action::XYZ_WRITE; a.ad_data = w;
                        out.push_back(a);
                        break;
                    default:
                        a.kind = Action::NOP; out.push_back(a);
                        break;
                    }
                }
            }
            break;
        }
        case Flag::REGLIST: {
            // PCSX2 semantics: daftar nilai 64-bit, DUA nilai per qword (lo dulu, lalu hi).
            // Deskriptor cycling per nilai. A+D: pasangan (addr, data) berurutan —
            // state machine alternasi, cocok untuk nreg=1{AD} maupun {AD,AD}.
            size_t total = t.nloop * nreg;
            std::vector<uint64_t> vals;
            vals.reserve(total);
            for (size_t i = 0; i < pay; i++) {
                uint64_t q0 = rd64(p + i * 16);
                uint64_t q1 = rd64(p + i * 16 + 8);
                vals.push_back(q0);
                vals.push_back(q1);
            }
            vals.resize(total);        // ceil() bisa kelebihan separuh qword terakhir
            bool pending_addr = false;
            uint64_t addr = 0;
            for (size_t i = 0; i < total; i++) {
                uint8_t reg = t.regs[i % nreg];
                Action a{};
                if (reg == AD) {
                    if (!pending_addr) {
                        addr = vals[i] & 0x7FF;
                        pending_addr = true;
                    } else {
                        a.kind = Action::AD_WRITE;
                        a.ad_addr = addr;
                        a.ad_data = vals[i];
                        out.push_back(a);
                        pending_addr = false;
                    }
                } else if (reg == PRIM) {
                    a.kind = Action::PRIM_WRITE; a.prim = (uint32_t)(vals[i] & 0x7FF);
                    out.push_back(a);
                } else {
                    a.kind = Action::NOP; out.push_back(a);
                }
            }
            break;
        }
        case Flag::IMAGE: {
            Action a{}; a.kind = Action::IMAGE_DATA; a.qwords = t.nloop;
            out.push_back(a);
            break;
        }
        default: break;
        }
        off += pay * 16;
        if (t.eop) break;                            // akhir rantai
    }
    if (consumed) *consumed = off;
    return true;
}

} // namespace gif
