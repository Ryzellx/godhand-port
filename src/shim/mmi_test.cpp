// mmi_test.cpp — unit test MMI R5900 (opcode 0x1C) di interpreter EE.
// Setiap ekspektasi diturunkan dari definisi lane (LE: lane0 = bit0..31) + tabel
// PCSX2 MMI.c. Tujuan: mengunci fix 2026-10-24 (stub "tulis rs ke rd" merusak
// pcpyh/pcpyld -> memset libkernel menulis pola 0x01010101 ke tabel SJUNI).
#include "ee_cpu.h"
#include "ee_mmi.h"
#include <cstdio>
#include <cstring>

using namespace eecpu;

static int g_fail = 0, g_pass = 0;

static void chk(const char* name, bool ok, const char* detail = "") {
    if (ok) { g_pass++; std::printf("  PASS %-22s %s\n", name, detail); }
    else    { g_fail++; std::printf("  FAIL %-22s %s\n", name, detail); }
}

// encode MMI: op=0x1C, funct f, sa, rs/rt/rd
static u32 enc(u32 f, u32 sa, u32 rs, u32 rt, u32 rd) {
    return (0x1Cu << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | f;
}

struct Case { Cpu c; };
static void set(Cpu& c, u32 r, const u32 v[4]) { for (int i = 0; i < 4; i++) c.r[r][i] = v[i]; }
static bool eq4(const Cpu& c, u32 r, const u32 v[4]) {
    for (int i = 0; i < 4; i++) if (c.r[r][i] != v[i]) return false;
    return true;
}
static void dump(const Cpu& c, u32 r) {
    std::printf("       got { %08x %08x %08x %08x }\n",
                c.r[r][0], c.r[r][1], c.r[r][2], c.r[r][3]);
}

int main() {
    std::printf("MMI unit test — tabel PCSX2, lane LE\n");

    {   // PADDW (MMI0 0x00): per-lane 32-bit add
        Cpu c; c.reset(0);
        u32 s[4] = {1, 2, 0x80000000u, 4}, t[4] = {10, 20, 1, 40}, want[4] = {11, 22, 0x80000001u, 44};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x00, 4, 5, 6)) && eq4(c, 6, want);
        chk("paddw", ok); if (!ok) dump(c, 6);
    }
    {   // PSUBW (MMI0 0x01): wrap 32-bit
        Cpu c; c.reset(0);
        u32 s[4] = {5, 0, 7, 7}, t[4] = {3, 1, 9, 7}, want[4] = {2, 0xFFFFFFFFu, 0xFFFFFFFEu, 0};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x01, 4, 5, 6)) && eq4(c, 6, want);
        chk("psubw", ok); if (!ok) dump(c, 6);
    }
    {   // PCGTW (MMI0 0x02): signed compare -> all-ones
        Cpu c; c.reset(0);
        u32 s[4] = {2, 0xFFFFFFFFu, 1, 1}, t[4] = {1, 0, 5, 1};
        u32 want[4] = {0xFFFFFFFFu, 0, 0, 0};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x02, 4, 5, 6)) && eq4(c, 6, want);
        chk("pcgtw", ok); if (!ok) dump(c, 6);
    }
    {   // PCPYH (MMI3 0x1B) + PCPYLD (MMI2 0x0E) = idiom memset libkernel
        // t0 = byte | byte<<8 = 0x0101 -> pcpyh -> pcpyld -> 16 B pola 0x01010101
        Cpu c; c.reset(0);
        u32 b[4] = {0x0101, 0, 0, 0};
        set(c, 8, b);                                   // t0
        mmi::exec(c, enc(0x29, 0x1B, 0, 8, 3));          // pcpyh v1,t0
        u32 want1[4] = {0x01010101u, 0x01010101u, 0, 0};
        bool ok1 = eq4(c, 3, want1);
        mmi::exec(c, enc(0x09, 0x0E, 3, 3, 8));          // pcpyld t0,v1,v1
        u32 want2[4] = {0x01010101u, 0x01010101u, 0x01010101u, 0x01010101u};
        bool ok2 = eq4(c, 8, want2);
        chk("pcpyh", ok1); if (!ok1) dump(c, 3);
        chk("pcpyld (memset idiom)", ok2); if (!ok2) dump(c, 8);
    }
    {   // PCPYLD murni: d = {t.LO, s.LO}
        Cpu c; c.reset(0);
        u32 s[4] = {0xA1, 0xA2, 0xA3, 0xA4}, t[4] = {0xB1, 0xB2, 0xB3, 0xB4};
        u32 want[4] = {0xB1, 0xB2, 0xA1, 0xA2};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x09, 0x0E, 4, 5, 6)) && eq4(c, 6, want);
        chk("pcpyld", ok); if (!ok) dump(c, 6);
    }
    {   // PEXTLW: d = {t0, s0, t1, s1}
        Cpu c; c.reset(0);
        u32 s[4] = {0x10, 0x11, 0x12, 0x13}, t[4] = {0x20, 0x21, 0x22, 0x23};
        u32 want[4] = {0x20, 0x10, 0x21, 0x11};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x12, 4, 5, 6)) && eq4(c, 6, want);
        chk("pextlw", ok); if (!ok) dump(c, 6);
    }
    {   // PPACB: ambil byte genap t lalu s
        Cpu c; c.reset(0);
        u32 s[4] = {0x04030201u, 0x08070605u, 0x0C0B0A09u, 0x100F0E0Du};
        u32 t[4] = {0x14131211u, 0x18171615u, 0x1C1B1A19u, 0x201F1E1Du};
        u32 want[4] = {0x17151311u, 0x1F1D1B19u, 0x07050301u, 0x0F0D0B09u};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x1B, 4, 5, 6)) && eq4(c, 6, want);
        chk("ppacb", ok); if (!ok) dump(c, 6);
    }
    {   // PAND / POR / PNOR / PXOR (MMI2 0x12/0x13, MMI3 0x12/0x13)
        Cpu c; c.reset(0);
        u32 s[4] = {0xF0F0F0F0u, 0x00FF00FFu, 0xFFFFFFFFu, 0x12345678u};
        u32 t[4] = {0x0F0F0F0Fu, 0x0000FFFFu, 0x80000000u, 0x12345678u};
        u32 wa[4] = {0x00000000u, 0x000000FFu, 0x80000000u, 0x12345678u};
        u32 wo[4] = {0xFFFFFFFFu, 0x00FFFFFFu, 0xFFFFFFFFu, 0x12345678u};
        u32 wx[4] = {0xFFFFFFFFu, 0x00FFFF00u, 0x7FFFFFFFu, 0x00000000u};
        u32 wn[4]; for (int i = 0; i < 4; i++) wn[i] = ~(s[i] | t[i]);
        set(c, 4, s); set(c, 5, t);
        bool a = mmi::exec(c, enc(0x09, 0x12, 4, 5, 6)) && eq4(c, 6, wa);
        set(c, 4, s); set(c, 5, t);
        bool o = mmi::exec(c, enc(0x29, 0x12, 4, 5, 6)) && eq4(c, 6, wo);
        set(c, 4, s); set(c, 5, t);
        bool x = mmi::exec(c, enc(0x09, 0x13, 4, 5, 6)) && eq4(c, 6, wx);
        set(c, 4, s); set(c, 5, t);
        bool n = mmi::exec(c, enc(0x29, 0x13, 4, 5, 6)) && eq4(c, 6, wn);
        chk("pand", a); chk("por", o); chk("pxor", x); chk("pnor", n);

    }
    {   // PADDSW saturasi (MMI0 0x10)
        Cpu c; c.reset(0);
        u32 s[4] = {0x7FFFFFFFu, 0x80000000u, 5, 0xFFFFFFFFu};
        u32 t[4] = {1, 0xFFFFFFFFu, 7, 0xFFFFFFFFu};
        u32 want[4] = {0x7FFFFFFFu, 0x80000000u, 12, 0xFFFFFFFEu};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x10, 4, 5, 6)) && eq4(c, 6, want);
        chk("paddsw (sat)", ok); if (!ok) dump(c, 6);
    }
    {   // PADDUW saturasi unsigned (MMI1 0x10)
        Cpu c; c.reset(0);
        u32 s[4] = {0xFFFFFFFFu, 1, 0, 0x80000000u};
        u32 t[4] = {1, 2, 3, 0x80000000u};
        u32 want[4] = {0xFFFFFFFFu, 3, 3, 0xFFFFFFFFu};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x28, 0x10, 4, 5, 6)) && eq4(c, 6, want);
        chk("padduw (sat)", ok); if (!ok) dump(c, 6);
    }
    {   // PADDH / PSUBH halfword lane (MMI0 0x04/0x05)
        Cpu c; c.reset(0);
        u32 s[4] = {0x00010002u, 0x00030004u, 0, 0};
        u32 t[4] = {0x00050006u, 0x00070008u, 0, 0};
        u32 want[4] = {0x00060008u, 0x000A000Cu, 0, 0};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x08, 0x04, 4, 5, 6)) && eq4(c, 6, want);
        chk("paddh", ok); if (!ok) dump(c, 6);
    }
    {   // PMAXH / PMINH signed halfword (MMI0 0x07 / MMI1 0x07)
        Cpu c; c.reset(0);
        u32 s[4] = {0x0002FFFEu, 0x0005FFF0u, 0, 0};   // half: 2, -2, 5, -16
        u32 t[4] = {0x0001FFFFu, 0x0009FFF8u, 0, 0};   // half: 1, -1, 9, -8
        // lane halfword: US0 = low 16 bit. s = {-2, 2, -16, 5}, t = {-1, 1, -8, 9}
        u32 wmax[4] = {0x0002FFFFu, 0x0009FFF8u, 0, 0};
        u32 wmin[4] = {0x0001FFFEu, 0x0005FFF0u, 0, 0};
        set(c, 4, s); set(c, 5, t);
        bool mx = mmi::exec(c, enc(0x08, 0x07, 4, 5, 6)) && eq4(c, 6, wmax);
        set(c, 4, s); set(c, 5, t);
        bool mn = mmi::exec(c, enc(0x28, 0x07, 4, 5, 6)) && eq4(c, 6, wmin);
        chk("pmaxh", mx); chk("pminh", mn);
    }
    {   // PSLLW / PSRLW / PSRAH dengan sa field (fn 0x3C/0x3E/0x37)
        Cpu c; c.reset(0);
        u32 t[4] = {0x80000001u, 0x0000000Fu, 0xFFFFFFFFu, 0};
        u32 wsl[4] = {0x00000008u, 0x00000078u, 0xFFFFFFF8u, 0};
        u32 wsr[4] = {0x10000000u, 1, 0x1FFFFFFFu, 0};
        set(c, 5, t);
        bool sl = mmi::exec(c, enc(0x3C, 3, 0, 5, 6)) && eq4(c, 6, wsl);
        set(c, 5, t);
        bool sr = mmi::exec(c, enc(0x3E, 3, 0, 5, 6)) && eq4(c, 6, wsr);
        chk("psllw", sl); if (!sl) dump(c, 6);
        chk("psrlw", sr); if (!sr) dump(c, 6);
        // PSRAH: halfword 0x8000 >> 4 = 0xF800 (aritmetik)
        Cpu d; d.reset(0);
        u32 th[4] = {0x8000FFFFu, 0x7FFF8000u, 0, 0};
        u32 wh[4] = {0xF800FFFFu, 0x07FFF800u, 0, 0};
        set(d, 5, th);
        bool sh = mmi::exec(d, enc(0x37, 4, 0, 5, 6)) && eq4(d, 6, wh);
        chk("psrah (arith)", sh); if (!sh) dump(d, 6);
    }
    {   // MULT1 (MMI fn 0x18): hasil -> LO/HI pipe1, rd = LO.UD[1]
        Cpu c; c.reset(0);
        u32 s[4] = {0x10000, 0, 0, 0}, t[4] = {0x10000, 0, 0, 0};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x18, 0, 4, 5, 6));
        u32 lo0 = c.lo[2], hi0 = c.hi[2];
        bool ok2 = (lo0 == 0) && (hi0 == 1u);            // 0x10000*0x10000 = 1<<32
        u32 want[4] = {0, 0, 0, 0};
        bool ok3 = eq4(c, 6, want);
        chk("mult1 lo/hi pipe1", ok && ok2); chk("mult1 rd=LO.UD[1]", ok3);
        if (!ok2) std::printf("       lo[2]=%08x hi[2]=%08x\n", lo0, hi0);
    }
    {   // MADD (MMI fn 0x00): accumulate LO/HI pipe0 + product, rd = LO.SD[0]
        Cpu c; c.reset(0);
        c.lo[0] = 10; c.hi[0] = 0;
        u32 s[4] = {5, 0, 0, 0}, t[4] = {7, 0, 0, 0};
        set(c, 4, s); set(c, 5, t);
        bool ok = mmi::exec(c, enc(0x00, 0, 4, 5, 6));
        u32 want[4] = {45, 0, 0, 0};
        chk("madd acc+product", ok && eq4(c, 6, want) && c.lo[0] == 45);
    }
    {   // QFSRV (MMI1 0x1B) dengan SA register
        Cpu c; c.reset(0);
        c.sa = 0;
        u32 s[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
        u32 t[4] = {0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu};
        set(c, 4, s); set(c, 5, t);
        bool ok0 = mmi::exec(c, enc(0x28, 0x1B, 4, 5, 6)) && eq4(c, 6, t);  // sa=0 -> copy rt
        chk("qfsrv sa=0", ok0);
    }
    {   // instruksi MMI tak dikenal -> false (pemanggil WAJIB fault, bukan menebak)
        Cpu c; c.reset(0);
        bool unknown = !mmi::exec(c, enc(0x08, 0x0F, 0, 0, 1));
        chk("unknown -> fault", unknown);
    }

    std::printf("MMI: %d/%d PASS\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
