// ee_mmi.h — implementasi MMI R5900 (opcode 0x1C) LENGKAP untuk interpreter EE.
// Decode: funct (inst&0x3F) memilih kelas (MMI0=0x08, MMI1=0x28, MMI2=0x09, MMI3=0x29)
// atau op langsung (MADD/MADDU/PLZCW/MFHI1../PMFHL/PSLLH..); sub-op = sa field.
// Tabel & semantik: PCSX2 R5900OpcodeTables.cpp + MMI.c (interpreter reference).
// Alasan: stub lama "tulis rs ke rd" membuat 2256 situs MMI di .text game salah
// (pcpyh/pcpyld di memset libkernel -> pola 0x01010101 -> 64 slot tabel SJUNI
// ditandai sibuk -> SJUNI_Create = 0 -> hang fatal di 0x337180).
#pragma once
#include "ee_cpu.h"
#include <cstdio>
#include <cstdlib>

namespace eecpu {
namespace mmi {

using s8 = int8_t;
using s16 = int16_t;

// lane helper (LE: indeks 0 = bit 0..31)
inline u32 gUS(const u32* v, int n) { return v[n & 3]; }
inline u32 gUH(const u32* v, int n) { return (v[(n >> 1) & 3] >> ((n & 1) * 16)) & 0xFFFFu; }
inline u32 gUB(const u32* v, int n) { return (v[(n >> 2) & 3] >> ((n & 3) * 8)) & 0xFFu; }
inline s32 gSL(const u32* v, int n) { return (s32)v[n & 3]; }
inline s32 gSH(const u32* v, int n) { return (s32)(s16)((v[(n >> 1) & 3] >> ((n & 1) * 16)) & 0xFFFFu); }
inline s32 gSB(const u32* v, int n) { return (s32)(s8)gUB(v, n); }
inline void sH_(u32* v, int n, u32 x) {
    u32& w = v[(n >> 1) & 3]; int sh = (n & 1) * 16;
    w = (w & ~(0xFFFFu << sh)) | ((x & 0xFFFFu) << sh);
}
inline void sB_(u32* v, int n, u32 x) {
    u32& w = v[(n >> 2) & 3]; int sh = (n & 3) * 8;
    w = (w & ~(0xFFu << sh)) | ((x & 0xFFu) << sh);
}
inline u64 lo64(const Cpu& c, int p) { return (u64)c.lo[p * 2] | ((u64)c.lo[p * 2 + 1] << 32); }
inline u64 hi64(const Cpu& c, int p) { return (u64)c.hi[p * 2] | ((u64)c.hi[p * 2 + 1] << 32); }
inline void setLo(Cpu& c, int p, u64 v) { c.lo[p * 2] = (u32)v; c.lo[p * 2 + 1] = (u32)(v >> 32); }
inline void setHi(Cpu& c, int p, u64 v) { c.hi[p * 2] = (u32)v; c.hi[p * 2 + 1] = (u32)(v >> 32); }
inline void setLoLane(Cpu& c, int n, u64 v) { c.lo[n] = (u32)v; c.lo[n + 1] = (u32)(v >> 32); }
inline void setHiLane(Cpu& c, int n, u64 v) { c.hi[n] = (u32)v; c.hi[n + 1] = (u32)(v >> 32); }
inline u32 lanes64(u32* d, int at, u64 v) { d[at] = (u32)v; d[at + 1] = (u32)(v >> 32); return 0; }

inline s64 sat32(s64 v) {
    return v > 2147483647ll ? 2147483647ll : (v < -2147483648ll ? -2147483648ll : v);
}
inline s32 sat16(s32 v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
inline s32 sat8(s32 v) { return v > 127 ? 127 : (v < -128 ? -128 : v); }

// ---------------- MMI0 (funct 0x08) ----------------
inline bool doMMI0(const u32* s, const u32* t, u32* d, u32 sa) {
    for (int i = 0; i < 4; i++) d[i] = 0;
    switch (sa) {
    case 0x00: for (int n = 0; n < 4; n++) d[n] = gUS(s, n) + gUS(t, n); break;                        // PADDW
    case 0x01: for (int n = 0; n < 4; n++) d[n] = gUS(s, n) - gUS(t, n); break;                        // PSUBW
    case 0x02: for (int n = 0; n < 4; n++) d[n] = (gSL(s, n) > gSL(t, n)) ? 0xFFFFFFFFu : 0u; break;   // PCGTW
    case 0x03: for (int n = 0; n < 4; n++) d[n] = (gSL(s, n) > gSL(t, n)) ? gUS(s, n) : gUS(t, n); break; // PMAXW
    case 0x04: for (int n = 0; n < 8; n++) sH_(d, n, gUH(s, n) + gUH(t, n)); break;                    // PADDH
    case 0x05: for (int n = 0; n < 8; n++) sH_(d, n, gUH(s, n) - gUH(t, n)); break;                    // PSUBH
    case 0x06: for (int n = 0; n < 8; n++) sH_(d, n, (gSH(s, n) > gSH(t, n)) ? 0xFFFFu : 0u); break;   // PCGTH
    case 0x07: for (int n = 0; n < 8; n++) sH_(d, n, (gSH(s, n) > gSH(t, n)) ? gUH(s, n) : gUH(t, n)); break; // PMAXH
    case 0x08: for (int n = 0; n < 16; n++) sB_(d, n, gUB(s, n) + gUB(t, n)); break;                   // PADDB
    case 0x09: for (int n = 0; n < 16; n++) sB_(d, n, gUB(s, n) - gUB(t, n)); break;                   // PSUBB
    case 0x0A: for (int n = 0; n < 16; n++) sB_(d, n, (gSB(s, n) > gSB(t, n)) ? 0xFFu : 0u); break;    // PCGTB
    case 0x10: for (int n = 0; n < 4; n++) d[n] = (u32)(s32)sat32((s64)gSL(s, n) + (s64)gSL(t, n)); break; // PADDSW
    case 0x11: for (int n = 0; n < 4; n++) d[n] = (u32)(s32)sat32((s64)gSL(s, n) - (s64)gSL(t, n)); break; // PSUBSW
    case 0x12: d[0] = gUS(t, 0); d[1] = gUS(s, 0); d[2] = gUS(t, 1); d[3] = gUS(s, 1); break;          // PEXTLW
    case 0x13: d[0] = gUS(t, 0); d[1] = gUS(t, 2); d[2] = gUS(s, 0); d[3] = gUS(s, 2); break;          // PPACW
    case 0x14: for (int n = 0; n < 8; n++) sH_(d, n, (u32)(s32)sat16((s32)gSH(s, n) + (s32)gSH(t, n))); break; // PADDSH
    case 0x15: for (int n = 0; n < 8; n++) sH_(d, n, (u32)(s32)sat16((s32)gSH(s, n) - (s32)gSH(t, n))); break; // PSUBSH
    case 0x16: for (int n = 0; n < 4; n++) { sH_(d, 2 * n, gUH(t, n)); sH_(d, 2 * n + 1, gUH(s, n)); } break; // PEXTLH
    case 0x17: for (int n = 0; n < 4; n++) { sH_(d, n, gUH(t, 2 * n)); sH_(d, 4 + n, gUH(s, 2 * n)); } break; // PPACH
    case 0x18: for (int n = 0; n < 16; n++) sB_(d, n, (u32)(s32)sat8((s32)gSB(s, n) + (s32)gSB(t, n))); break; // PADDSB
    case 0x19: for (int n = 0; n < 16; n++) sB_(d, n, (u32)(s32)sat8((s32)gSB(s, n) - (s32)gSB(t, n))); break; // PSUBSB
    case 0x1A: for (int n = 0; n < 8; n++) { sB_(d, 2 * n, gUB(t, n)); sB_(d, 2 * n + 1, gUB(s, n)); } break;  // PEXTLB
    case 0x1B: for (int n = 0; n < 8; n++) { sB_(d, n, gUB(t, 2 * n)); sB_(d, 8 + n, gUB(s, 2 * n)); } break;  // PPACB
    case 0x1E: for (int n = 0; n < 4; n++) {                                                            // PEXT5
        u32 v = gUS(t, n);
        d[n] = ((v & 0x0000001Fu) << 3) | ((v & 0x000003E0u) << 6) |
               ((v & 0x00007C00u) << 9) | ((v & 0x00008000u) << 16);
    } break;
    case 0x1F: for (int n = 0; n < 4; n++) {                                                            // PPAC5
        u32 v = gUS(t, n);
        d[n] = ((v >> 3) & 0x0000001Fu) | ((v >> 6) & 0x000003E0u) |
               ((v >> 9) & 0x00007C00u) | ((v >> 16) & 0x00008000u);
    } break;
    default: return false;   // slot tak terdefinisi di tabel PCSX2 = instruksi ilegal
    }
    return true;
}

// ---------------- MMI1 (funct 0x28) ----------------
inline bool doMMI1(Cpu& c, const u32* s, const u32* t, u32* d, u32 sa) {
    for (int i = 0; i < 4; i++) d[i] = 0;
    switch (sa) {
    case 0x01: for (int n = 0; n < 4; n++) { s32 v = gSL(s, n); d[n] = (v < 0) ? (u32)(-v) : (u32)v; } break; // PABSW
    case 0x02: for (int n = 0; n < 4; n++) d[n] = (gUS(s, n) == gUS(t, n)) ? 0xFFFFFFFFu : 0u; break;           // PCEQW
    case 0x03: for (int n = 0; n < 4; n++) d[n] = (gSL(s, n) < gSL(t, n)) ? gUS(s, n) : gUS(t, n); break;       // PMINW
    case 0x04: for (int n = 0; n < 4; n++) sH_(d, n, gUH(s, n) - gUH(t, n));                                    // PADSBH
               for (int n = 4; n < 8; n++) sH_(d, n, gUH(s, n) + gUH(t, n)); break;
    case 0x05: for (int n = 0; n < 8; n++) { s32 v = gSH(s, n); sH_(d, n, (v < 0) ? (u32)(-v) : (u32)v); } break; // PABSH
    case 0x06: for (int n = 0; n < 8; n++) sH_(d, n, (gUH(s, n) == gUH(t, n)) ? 0xFFFFu : 0u); break;           // PCEQH
    case 0x07: for (int n = 0; n < 8; n++) sH_(d, n, (gSH(s, n) < gSH(t, n)) ? gUH(s, n) : gUH(t, n)); break;   // PMINH
    case 0x0A: for (int n = 0; n < 16; n++) sB_(d, n, (gUB(s, n) == gUB(t, n)) ? 0xFFu : 0u); break;            // PCEQB
    case 0x10: for (int n = 0; n < 4; n++) { s64 v = (s64)gUS(s, n) + (s64)gUS(t, n);
                  d[n] = (v > 0xFFFFFFFFll) ? 0xFFFFFFFFu : (u32)v; } break;                                    // PADDUW
    case 0x11: for (int n = 0; n < 4; n++) { s64 v = (s64)gUS(s, n) - (s64)gUS(t, n);
                  d[n] = (v <= 0) ? 0u : (u32)v; } break;                                                       // PSUBUW
    case 0x12: d[0] = gUS(t, 2); d[1] = gUS(s, 2); d[2] = gUS(t, 3); d[3] = gUS(s, 3); break;                    // PEXTUW
    case 0x14: for (int n = 0; n < 8; n++) { s32 v = (s32)gUH(s, n) + (s32)gUH(t, n);
                  sH_(d, n, v > 0xFFFF ? 0xFFFFu : (u32)v); } break;                                            // PADDUH
    case 0x15: for (int n = 0; n < 8; n++) { s32 v = (s32)gUH(s, n) - (s32)gUH(t, n);
                  sH_(d, n, v <= 0 ? 0u : (u32)v); } break;                                                     // PSUBUH
    case 0x16: for (int n = 0; n < 4; n++) { sH_(d, 2 * n, gUH(t, 4 + n)); sH_(d, 2 * n + 1, gUH(s, 4 + n)); } break; // PEXTUH
    case 0x18: for (int n = 0; n < 16; n++) { u32 v = gUB(s, n) + gUB(t, n); sB_(d, n, v > 0xFF ? 0xFFu : v); } break; // PADDUB
    case 0x19: for (int n = 0; n < 16; n++) { s32 v = (s32)gUB(s, n) - (s32)gUB(t, n);
                  sB_(d, n, v <= 0 ? 0u : (u32)v); } break;                                                     // PSUBUB
    case 0x1A: for (int n = 0; n < 8; n++) { sB_(d, 2 * n, gUB(t, 8 + n)); sB_(d, 2 * n + 1, gUB(s, 8 + n)); } break; // PEXTUB
    case 0x1B: {                                                                                                // QFSRV (reg SA)
        u64 s0 = (u64)s[0] | ((u64)s[1] << 32), s1 = (u64)s[2] | ((u64)s[3] << 32);
        u64 t0 = (u64)t[0] | ((u64)t[1] << 32), t1 = (u64)t[2] | ((u64)t[3] << 32);
        u32 sh = c.sa & 0x7F;
        u64 r0, r1;
        if (sh == 0) { r0 = t0; r1 = t1; }
        else if (sh < 64) { r0 = (t0 >> sh) | (t1 << (64 - sh)); r1 = (t1 >> sh) | (s0 << (64 - sh)); }
        else { u32 k = sh - 64;
               if (k == 0) { r0 = t1; r1 = s0; }
               else { r0 = (t1 >> k) | (s0 << (64 - k)); r1 = (s1 >> k) | (s0 << (64 - k)); } }
        d[0] = (u32)r0; d[1] = (u32)(r0 >> 32); d[2] = (u32)r1; d[3] = (u32)(r1 >> 32);
    } break;
    default: return false;
    }
    return true;
}

// ---------------- MMI2 (funct 0x09) ----------------
inline bool doMMI2(Cpu& c, const u32* s, const u32* t, u32* d, u32 sa) {
    for (int i = 0; i < 4; i++) d[i] = 0;
    switch (sa) {
    case 0x00: case 0x04: {                                     // PMADDW / PMSUBW
        int sgn = (sa == 0x00) ? 1 : -1;
        for (int p = 0; p < 2; p++) {
            s64 acc = (s64)((s64)(s32)c.lo[p * 2] | ((s64)(s32)c.hi[p * 2] << 32));
            s64 temp = acc + (s64)sgn * (s64)((s64)gSL(s, p * 2) * (s64)gSL(t, p * 2));
            s64 lv = (s64)(s32)(u32)temp, hv = (s64)(s32)(u32)(temp >> 32);
            setLoLane(c, p * 2, (u64)lv);
            setHiLane(c, p * 2, (u64)hv);
            lanes64(d, p * 2, (u64)temp);
        }
    } break;
    case 0x02: for (int n = 0; n < 2; n++) {                    // PSLLVW
        u32 v = gUS(t, n * 2) << (gUS(s, n * 2) & 0x1F);
        lanes64(d, n * 2, (u64)(s64)(s32)v);
    } break;
    case 0x03: for (int n = 0; n < 2; n++) {                    // PSRLVW
        u32 v = gUS(t, n * 2) >> (gUS(s, n * 2) & 0x1F);
        lanes64(d, n * 2, (u64)(s64)(s32)v);
    } break;
    case 0x08: d[0] = c.hi[0]; d[1] = c.hi[1]; d[2] = c.hi[2]; d[3] = c.hi[3]; break;   // PMFHI
    case 0x09: d[0] = c.lo[0]; d[1] = c.lo[1]; d[2] = c.lo[2]; d[3] = c.lo[3]; break;   // PMFLO
    case 0x0A: for (int n = 0; n < 4; n++) { sH_(d, 2 * n, gUH(t, n)); sH_(d, 2 * n + 1, gUH(s, 4 + n)); } break; // PINTH
    case 0x0C: case 0x0D: {                                     // PMULTW / PDIVW
        for (int p = 0; p < 2; p++) {
            s32 a = gSL(s, p * 2), b = gSL(t, p * 2);
            if (sa == 0x0D) {
                if (b == 0) continue;
                s64 q = (s64)(a / b), r = (s64)(a % b);
                setLoLane(c, p * 2, (u64)q);
                setHiLane(c, p * 2, (u64)r);   // PDIVW: hanya LO/HI, rd TIDAK ditulis
            } else {
                s64 temp = (s64)a * (s64)b;
                s64 lv = (s64)(s32)(u32)temp, hv = (s64)(s32)(u32)(temp >> 32);
                setLoLane(c, p * 2, (u64)lv);
                setHiLane(c, p * 2, (u64)hv);
                lanes64(d, p * 2, (u64)temp);
            }
        }
    } break;
    case 0x0E: d[0] = t[0]; d[1] = t[1]; d[2] = s[0]; d[3] = s[1]; break;               // PCPYLD
    case 0x10: case 0x14: {                                     // PMADDH / PMSUBH
        int sgn = (sa == 0x10) ? 1 : -1;
        u32* accs[8] = {&c.lo[0], &c.lo[1], &c.hi[0], &c.hi[1],
                        &c.lo[2], &c.lo[3], &c.hi[2], &c.hi[3]};
        for (int k = 0; k < 8; k++) {
            s32 temp = (s32)*accs[k] + sgn * ((s32)gSH(s, k) * (s32)gSH(t, k));
            *accs[k] = (u32)temp;
            if ((k & 1) == 0) d[k >> 1] = (u32)temp;
        }
    } break;
    case 0x11: case 0x15: {                                     // PHMADH / PHMSBH
        static const int aoff[4] = {0, 0, 2, 2};
        for (int k = 0; k < 4; k++) {
            u32* acc = (k & 1) ? &c.hi[aoff[k]] : &c.lo[aoff[k]];
            s32 cross = (s32)gSH(s, k * 2 + 1) * (s32)gSH(t, k * 2 + 1);
            s32 same  = (s32)gSH(s, k * 2)     * (s32)gSH(t, k * 2);
            s32 temp = (sa == 0x11) ? (cross + same) : (cross - same);
            *acc = (u32)temp;
            d[k] = (u32)temp;
        }
    } break;
    case 0x12: for (int n = 0; n < 4; n++) d[n] = s[n] & t[n]; break;                    // PAND
    case 0x13: for (int n = 0; n < 4; n++) d[n] = s[n] ^ t[n]; break;                    // PXOR
    case 0x1A: {                                                // PEXEH
        static const int ex[8] = {2, 1, 0, 3, 6, 5, 4, 7};
        for (int n = 0; n < 8; n++) sH_(d, n, gUH(t, ex[n]));
    } break;
    case 0x1B: {                                                // PREVH
        static const int rv[8] = {3, 2, 1, 0, 7, 6, 5, 4};
        for (int n = 0; n < 8; n++) sH_(d, n, gUH(t, rv[n]));
    } break;
    case 0x1C: {                                                // PMULTH
        u32* accs[8] = {&c.lo[0], &c.lo[1], &c.hi[0], &c.hi[1],
                        &c.lo[2], &c.lo[3], &c.hi[2], &c.hi[3]};
        for (int k = 0; k < 8; k++) {
            s32 temp = (s32)gSH(s, k) * (s32)gSH(t, k);
            *accs[k] = (u32)temp;
            if ((k & 1) == 0) d[k >> 1] = (u32)temp;
        }
    } break;
    case 0x1D: {                                                // PDIVBW
        if ((s16)gUH(t, 0) == 0) break;
        for (int k = 0; k < 4; k++) {
            s32 q = gSL(s, k) / (s32)gSH(t, 0);
            s32 r = gSL(s, k) % (s32)gSH(t, 0);
            c.lo[k] = (u32)q;
            c.hi[k] = (u32)(s32)(s16)r;
        }
    } break;
    case 0x1E: d[0] = t[2]; d[1] = t[1]; d[2] = t[0]; d[3] = t[3]; break;                // PEXEW
    case 0x1F: d[0] = t[1]; d[1] = t[2]; d[2] = t[0]; d[3] = t[3]; break;                // PROT3W
    default: return false;
    }
    return true;
}

// ---------------- MMI3 (funct 0x29) ----------------
inline bool doMMI3(Cpu& c, const u32* s, const u32* t, u32* d, u32 sa) {
    for (int i = 0; i < 4; i++) d[i] = 0;
    switch (sa) {
    case 0x00: {                                                // PMADDUW
        for (int p = 0; p < 2; p++) {
            u64 acc = (u64)c.lo[p * 2] | ((u64)c.hi[p * 2] << 32);
            u64 temp = acc + (u64)((u64)gUS(s, p * 2) * (u64)gUS(t, p * 2));
            c.lo[p * 2] = (u32)temp; c.lo[p * 2 + 1] = (u32)((s32)(u32)temp);
            c.hi[p * 2] = (u32)(temp >> 32); c.hi[p * 2 + 1] = (u32)((s32)(u32)(temp >> 32));
            lanes64(d, p * 2, temp);
        }
    } break;
    case 0x03: for (int n = 0; n < 2; n++) {                    // PSRAVW
        s32 v = gSL(t, n * 2) >> (gUS(s, n * 2) & 0x1F);
        lanes64(d, n * 2, (u64)(s64)v);
    } break;
    case 0x08: c.hi[0] = s[0]; c.hi[1] = s[1]; c.hi[2] = s[2]; c.hi[3] = s[3]; break;    // PMTHI
    case 0x09: c.lo[0] = s[0]; c.lo[1] = s[1]; c.lo[2] = s[2]; c.lo[3] = s[3]; break;    // PMTLO
    case 0x0A: for (int n = 0; n < 4; n++) { sH_(d, 2 * n, gUH(t, 2 * n)); sH_(d, 2 * n + 1, gUH(s, 2 * n)); } break; // PINTEH
    case 0x0C: {                                                // PMULTUW
        for (int p = 0; p < 2; p++) {
            u64 temp = (u64)gUS(s, p * 2) * (u64)gUS(t, p * 2);
            c.lo[p * 2] = (u32)temp; c.lo[p * 2 + 1] = (u32)(s32)(u32)temp;
            c.hi[p * 2] = (u32)(temp >> 32); c.hi[p * 2 + 1] = (u32)(s32)(u32)(temp >> 32);
            lanes64(d, p * 2, temp);
        }
    } break;
    case 0x0D: {                                                // PDIVUW
        for (int p = 0; p < 2; p++) {
            u32 b = gUS(t, p * 2);
            if (b == 0) continue;
            u64 q = (u64)gUS(s, p * 2) / (u64)b, r = (u64)gUS(s, p * 2) % (u64)b;
            c.lo[p * 2] = (u32)q; c.lo[p * 2 + 1] = (u32)(s32)(u32)q;
            c.hi[p * 2] = (u32)r; c.hi[p * 2 + 1] = (u32)(s32)(u32)r;
            lanes64(d, p * 2, q);
        }
    } break;
    case 0x0E: d[0] = s[2]; d[1] = s[3]; d[2] = t[2]; d[3] = t[3]; break;                // PCPYUD
    case 0x12: for (int n = 0; n < 4; n++) d[n] = s[n] | t[n]; break;                    // POR
    case 0x13: for (int n = 0; n < 4; n++) d[n] = ~(s[n] | t[n]); break;                 // PNOR
    case 0x1A: {                                                // PEXCH
        static const int ex[8] = {0, 2, 1, 3, 4, 6, 5, 7};
        for (int n = 0; n < 8; n++) sH_(d, n, gUH(t, ex[n]));
    } break;
    case 0x1B: for (int n = 0; n < 4; n++) { sH_(d, n, gUH(t, 0)); sH_(d, n + 4, gUH(t, 4)); } break; // PCPYH
    case 0x1E: d[0] = t[0]; d[1] = t[2]; d[2] = t[1]; d[3] = t[3]; break;                // PEXCW
    default: return false;
    }
    return true;
}

// ---------------- entry: opcode 0x1C ----------------
// return false = instruksi MMI tak dikenal (pemanggil WAJIB fault, bukan menebak).
inline bool exec(Cpu& c, u32 inst) {
    u32 f = inst & 0x3F;
    u32 R = (inst >> 21) & 0x1F, T = (inst >> 16) & 0x1F, D = (inst >> 11) & 0x1F;
    u32 sa = (inst >> 6) & 0x1F;
    u32 s[4], t[4], d[4];
    for (int i = 0; i < 4; i++) { s[i] = c.r[R][i]; t[i] = c.r[T][i]; d[i] = 0; }
    c.mmi_fn_hits[f]++;
    if (std::getenv("MMI_DEBUG")) std::fprintf(stderr, "[mmi] f=%02x sa=%02x rs=%u rt=%u rd=%u\n", f, sa, R, T, D);
    auto wr = [&]() { if (D) for (int i = 0; i < 4; i++) c.r[D][i] = d[i]; };

    switch (f) {
    case 0x00: case 0x01: {                                     // MADD / MADDU
        u64 acc = (u64)c.lo[0] | ((u64)c.hi[0] << 32);
        s64 temp;
        if (f == 0x00) temp = (s64)acc + (s64)((s64)gSL(s, 0) * (s64)gSL(t, 0));
        else           temp = (s64)(acc + (u64)((u64)gUS(s, 0) * (u64)gUS(t, 0)));
        s64 lv = (s64)(s32)(u32)temp, hv = (s64)(s32)(u32)(temp >> 32);
        setLoLane(c, 0, (u64)lv);
        setHiLane(c, 0, (u64)hv);
        lanes64(d, 0, (u64)lv);                                 // rd.SD[0] = LO.SD[0]
        wr(); return true;
    }
    case 0x04: {                                                // PLZCW
        for (int n = 0; n < 2; n++) {
            u32 v = gUS(s, n), sign = v >> 31; int i = 30;
            for (; i >= 0; i--) if (((v >> i) & 1) != sign) break;
            d[n] = (u32)(30 - i);
        }
        wr(); return true;
    }
    case 0x08: if (!doMMI0(s, t, d, sa)) return false; c.mmi_sub_hits[0][sa]++; wr(); return true;
    case 0x09: if (!doMMI2(c, s, t, d, sa)) return false; c.mmi_sub_hits[2][sa]++; wr(); return true;
    case 0x28: if (!doMMI1(c, s, t, d, sa)) return false; c.mmi_sub_hits[1][sa]++; wr(); return true;
    case 0x29: if (!doMMI3(c, s, t, d, sa)) return false; c.mmi_sub_hits[3][sa]++; wr(); return true;
    case 0x10: d[0] = c.hi[2]; d[1] = c.hi[3]; wr(); return true;                 // MFHI1
    case 0x11: c.hi[2] = s[0]; c.hi[3] = s[1]; return true;                       // MTHI1
    case 0x12: d[0] = c.lo[2]; d[1] = c.lo[3]; wr(); return true;                 // MFLO1
    case 0x13: c.lo[2] = s[0]; c.lo[3] = s[1]; return true;                       // MTLO1
    case 0x18: case 0x19: {                                     // MULT1 / MULTU1
        s64 temp;
        if (f == 0x18) temp = (s64)gSL(s, 0) * (s64)gSL(t, 0);
        else temp = (s64)((u64)gUS(s, 0) * (u64)gUS(t, 0));
        s64 lv = (s64)(s32)(u32)temp, hv = (s64)(s32)(u32)(temp >> 32);
        c.lo[2] = (u32)lv; c.lo[3] = (u32)(lv >> 32);
        c.hi[2] = (u32)hv; c.hi[3] = (u32)(hv >> 32);
        lanes64(d, 0, (u64)lv);                                 // rd.UD[0] = LO.UD[1]
        wr(); return true;
    }
    case 0x1A: case 0x1B: {                                     // DIV1 / DIVU1
        if (f == 0x1A) {
            s32 b = gSL(t, 0);
            if (b != 0) {
                s32 q = gSL(s, 0) / b, r = gSL(s, 0) % b;
                setLoLane(c, 2, (u64)(s64)q);
                setHiLane(c, 2, (u64)(s64)r);
            }
        } else {
            u32 b = gUS(t, 0);
            if (b != 0) {
                u32 q = gUS(s, 0) / b, r = gUS(s, 0) % b;
                setLoLane(c, 2, (u64)(s64)(s32)q);
                setHiLane(c, 2, (u64)(s64)(s32)r);
            }
        }
        return true;
    }
    case 0x20: case 0x21: {                                     // MADD1 / MADDU1
        u64 acc = (u64)c.lo[2] | ((u64)c.hi[2] << 32);
        s64 temp;
        if (f == 0x20) temp = (s64)acc + (s64)((s64)gSL(s, 0) * (s64)gSL(t, 0));
        else           temp = (s64)(acc + (u64)((u64)gUS(s, 0) * (u64)gUS(t, 0)));
        s64 lv = (s64)(s32)(u32)temp, hv = (s64)(s32)(u32)(temp >> 32);
        c.lo[2] = (u32)lv; c.lo[3] = (u32)(lv >> 32);
        c.hi[2] = (u32)hv; c.hi[3] = (u32)(hv >> 32);
        lanes64(d, 0, (u64)lv);
        wr(); return true;
    }
    case 0x30: {                                                // PMFHL
        switch (sa) {
        case 0x00: d[0] = c.lo[0]; d[1] = c.hi[0]; d[2] = c.lo[2]; d[3] = c.hi[2]; break;
        case 0x01: d[0] = c.lo[1]; d[1] = c.hi[1]; d[2] = c.lo[3]; d[3] = c.hi[3]; break;
        case 0x02: {
            for (int p = 0; p < 2; p++) {
                u64 v = ((u64)c.hi[p * 2] << 32) | (u64)c.lo[p * 2];
                if (v >= 0x000000007FFFFFFFull) { d[p * 2] = 0x7FFFFFFFu; d[p * 2 + 1] = 0u; }
                else if (v <= 0xFFFFFFFF80000000ull) { d[p * 2] = 0x80000000u; d[p * 2 + 1] = 0xFFFFFFFFu; }
                else { s64 sv = (s64)gSL(c.lo, p * 2); d[p * 2] = (u32)sv; d[p * 2 + 1] = (u32)(sv >> 32); }
            }
        } break;
        case 0x03:
            d[0] = (c.lo[0] & 0xFFFFu) | ((c.lo[1] & 0xFFFFu) << 16);
            d[1] = (c.hi[0] & 0xFFFFu) | ((c.hi[1] & 0xFFFFu) << 16);
            d[2] = (c.lo[2] & 0xFFFFu) | ((c.lo[3] & 0xFFFFu) << 16);
            d[3] = (c.hi[2] & 0xFFFFu) | ((c.hi[3] & 0xFFFFu) << 16);
            break;
        case 0x04: {
            u32 vals[8] = {c.lo[0], c.lo[1], c.hi[0], c.hi[1], c.lo[2], c.lo[3], c.hi[2], c.hi[3]};
            for (int n = 0; n < 8; n++) sH_(d, n, (u32)(s32)sat16((s32)vals[n]));
        } break;
        default: return false;
        }
        wr(); return true;
    }
    case 0x31: if (sa == 0) {                                   // PMTHL
        c.lo[0] = s[0]; c.hi[0] = s[1]; c.lo[2] = s[2]; c.hi[2] = s[3];
    } return true;
    case 0x34: for (int n = 0; n < 8; n++) sH_(d, n, gUH(t, n) << (sa & 0xF)); break;            // PSLLH
    case 0x36: for (int n = 0; n < 8; n++) sH_(d, n, gUH(t, n) >> (sa & 0xF)); break;            // PSRLH
    case 0x37: for (int n = 0; n < 8; n++) sH_(d, n, (u32)(s32)(gSH(t, n) >> (sa & 0xF))); break; // PSRAH
    case 0x3C: for (int n = 0; n < 4; n++) d[n] = gUS(t, n) << sa; break;                        // PSLLW
    case 0x3E: for (int n = 0; n < 4; n++) d[n] = gUS(t, n) >> sa; break;                        // PSRLW
    case 0x3F: for (int n = 0; n < 4; n++) d[n] = (u32)(gSL(t, n) >> sa); break;                 // PSRAW
    default: return false;
    }
    wr();
    return true;
}

} // namespace mmi
} // namespace eecpu
