// gif_test — unit test parser GIF tag. Stdlib only, jalan tanpa GPU.
#include "gif.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gif;

static void put_tag(std::vector<uint8_t>& v, uint32_t nloop, bool eop, bool pre,
                    uint32_t prim, Flag flg, uint32_t nreg, const std::vector<uint8_t>& regs) {
    uint64_t w0 = (nloop & 0x7FFFull)
                | ((uint64_t)eop << 15)
                | ((uint64_t)pre << 16)
                | ((uint64_t)(prim & 0x7FF) << 17)
                | ((uint64_t)(uint32_t)flg << 28)
                | ((uint64_t)(nreg & 0xF) << 30);
    uint64_t w1 = 0;
    for (size_t i = 0; i < regs.size(); i++)
        w1 |= (uint64_t)regs[i] << (i * 4);
    uint8_t b[16];
    std::memcpy(b, &w0, 8); std::memcpy(b + 8, &w1, 8);
    v.insert(v.end(), b, b + 16);
}

static void put_qw(std::vector<uint8_t>& v, uint64_t lo, uint64_t hi = 0) {
    uint8_t b[16];
    std::memcpy(b, &lo, 8); std::memcpy(b + 8, &hi, 8);
    v.insert(v.end(), b, b + 16);
}

int main() {
    // ---- T1: decode_tag fields ----
    {
        std::vector<uint8_t> v;
        put_tag(v, /*nloop*/5, /*eop*/true, /*pre*/true, /*prim*/0x155, Flag::PACKED,
                /*nreg*/3, {RGBAQ, ST, XYZ2});
        Tag t = decode_tag(v.data());
        assert(t.nloop == 5 && t.eop && t.pre && t.prim == 0x155);
        assert(t.flg == Flag::PACKED && t.nreg == 3);
        assert(t.regs[0] == RGBAQ && t.regs[1] == ST && t.regs[2] == XYZ2);
        std::printf("T1 decode_tag            PASS\n");
    }
    // ---- T2: PACKED RGBAQ+ST+XYZ2 -> aksi ----
    {
        std::vector<uint8_t> v;
        put_tag(v, 1, true, false, 0, Flag::PACKED, 3, {RGBAQ, ST, XYZ2});
        put_qw(v, 0x00000000FF808080ull);        // RGBAQ: r g b a q
        put_qw(v, 0x4000000040000000ull);        // S=2.0 T=2.0
        put_qw(v, 0x0000000000000000ull);        // XYZ2
        std::vector<Action> acts;
        size_t cons = 0;
        assert(parse_stream(v.data(), v.size(), acts, &cons));
        assert(cons == v.size());
        assert(acts.size() == 3);
        assert(acts[0].kind == Action::RGBA_WRITE && acts[1].kind == Action::UV_WRITE
               && acts[2].kind == Action::XYZ_WRITE);
        std::printf("T2 packed vertex         PASS\n");
    }
    // ---- T3: REGLIST A+D (DrawEnv style) — PCSX2 semantics: 2 nilai 64-bit per qword ----
    {
        std::vector<uint8_t> v;
        // nloop=2 nreg=2 regs={AD,AD} -> 4 nilai 64-bit = 2 qword: (addr,data)(addr,data)
        put_tag(v, 2, true, false, 0, Flag::REGLIST, 2, {AD, AD});
        put_qw(v, 0x000000000000004Cull, 0x0000000002000200ull); // FRAME_1=0x4C (gsKit verified)|value
        put_qw(v, 0x000000000000004Eull, 0x0000000000000105ull); // ZBUF_1=0x4E (gsKit verified)|value
        std::vector<Action> acts;
        size_t cons = 0;
        assert(parse_stream(v.data(), v.size(), acts, &cons));
        assert(cons == v.size());
        assert(acts.size() == 2);
        assert(acts[0].kind == Action::AD_WRITE && acts[0].ad_addr == 0x4C
               && acts[0].ad_data == 0x2000200ull);
        assert(acts[1].kind == Action::AD_WRITE && acts[1].ad_addr == 0x4E);
        std::printf("T3 reglist A+D env       PASS\n");
    }
    // ---- T4: IMAGE transfer (upload texture) ----
    {
        std::vector<uint8_t> v;
        put_tag(v, 4, true, false, 0, Flag::IMAGE, 1, {0});
        for (int i = 0; i < 4; i++) put_qw(v, 0x1122334455667788ull);
        std::vector<Action> acts;
        size_t cons = 0;
        assert(parse_stream(v.data(), v.size(), acts, &cons));
        assert(acts.size() == 1 && acts[0].kind == Action::IMAGE_DATA && acts[0].qwords == 4);
        assert(cons == v.size());
        std::printf("T4 image transfer        PASS\n");
    }
    // ---- T5: rantai campur 2 paket, EOP di paket kedua ----
    {
        std::vector<uint8_t> v;
        put_tag(v, 1, /*eop*/false, true, 0x0B3, Flag::PACKED, 1, {PRIM});
        put_qw(v, 0);
        // REGLIST nloop=4 nreg=1 {AD} -> 4 nilai = 2 write = 2 qword (lo=addr, hi=data)
        put_tag(v, 4, true, false, 0, Flag::REGLIST, 1, {AD});
        put_qw(v, 0x1ull, 0xAAAull);
        put_qw(v, 0x2ull, 0xBBBull);
        std::vector<Action> acts;
        size_t cons = 0;
        assert(parse_stream(v.data(), v.size(), acts, &cons));
        assert(cons == v.size());
        // PRIM dari PRE + PRIM descriptor + 2 AD
        assert(acts.size() == 4);
        assert(acts[0].kind == Action::PRIM_WRITE && acts[0].prim == 0x0B3);
        assert(acts[2].kind == Action::AD_WRITE && acts[2].ad_data == 0xAAAull);
        std::printf("T5 chained packets       PASS\n");
    }
    // ---- T6: truncated -> false ----
    {
        std::vector<uint8_t> v;
        put_tag(v, 3, true, false, 0, Flag::IMAGE, 1, {0});
        put_qw(v, 0x1);                            // cuma 1 dari 3 qword
        std::vector<Action> acts;
        assert(!parse_stream(v.data(), v.size(), acts, nullptr));
        std::printf("T6 truncated guard       PASS\n");
    }
    std::printf("ALL GIF PARSER TESTS PASS\n");
    return 0;
}
