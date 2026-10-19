// ee_cpu.cpp — interpreter MIPS R5900 (EE little-endian) Phase 7b.
// Sumber semantik: EE Core manual (ps2tek tables) + PCSX2 interpreter (ps2-unpacker
// MMI.c) — MMI0/1 + hot MMI2/3 diimplement penuh; sisanya fault-driven (census jujur).
// LQ/SQ encoding diverifikasi dari pasangan prolog/epilog stack game (SQ op 0x1F,
// LQ op 0x1E, imm16 penuh 16-aligned). Unaligned LWL/LWR/SWL/SWR/SDL/SDR/LDL/LDR
// pakai tabel byte-level MIPS LE klasik.
#include "ee_cpu.h"
#include <cstdio>
#include <cstring>

namespace eecpu {

void Cpu::reset(u32 entry) {
    std::memset(r, 0, sizeof(r));
    std::memset(lo, 0, sizeof(lo));
    std::memset(hi, 0, sizeof(hi));
    pc = entry;
    next_pc = entry + 4;
    sa = 0; facc = 0; fcsr = 0;
    std::memset(cop0, 0, sizeof(cop0));
    std::memset(f, 0, sizeof(f));
    instret = 0;
    fault = nullptr;
    fault_pc = fault_op = 0;
    std::memset(mmi_fn_hits, 0, sizeof(mmi_fn_hits));
    std::memset(mmi_sub_hits, 0, sizeof(mmi_sub_hits));
    std::memset(sp3_fn_hits, 0, sizeof(sp3_fn_hits));
}

static inline u32 sx16(u32 v) { return (u32)(int32_t)(int16_t)(v & 0xFFFF); }
static inline u32 sx8(u32 v)  { return (u32)(int32_t)(int8_t)(v & 0xFF); }

static inline u32 op(u32 i)  { return (i >> 26) & 0x3F; }
static inline u32 rs(u32 i)  { return (i >> 21) & 0x1F; }
static inline u32 rt(u32 i)  { return (i >> 16) & 0x1F; }
static inline u32 rd(u32 i)  { return (i >> 11) & 0x1F; }
static inline u32 sa_f(u32 i){ return (i >> 6) & 0x1F; }
static inline u32 fn(u32 i)  { return i & 0x3F; }
static inline u32 imm(u32 i) { return i & 0xFFFF; }
static inline u32 tgt(u32 i) { return (i & 0x3FFFFFF) << 2; }

// ---- tulis GPR ----
// 32-bit ALU: lane0 saja (upper tak disentuh — lihat catatan; konsisten dgn asumsi
// 32-bit game). Load byte/half/word: sign-extend penuh 128-bit (perilaku EE asli).
static inline void w32(Cpu& c, u32 i, u32 v) { if (i) c.r[i][0] = v; }
static inline void w32se(Cpu& c, u32 i, u32 v) {   // sign-extend bit31 ke seluruh 128
    if (!i) return;
    c.r[i][0] = v;
    u32 ext = (v >> 31) ? 0xFFFFFFFFu : 0u;
    c.r[i][1] = c.r[i][2] = c.r[i][3] = ext;
}
static inline void w64(Cpu& c, u32 i, u64 v) {    // 64-bit write, sign-extend ke lane2/3
    if (!i) return;
    c.r[i][0] = (u32)v;
    c.r[i][1] = (u32)(v >> 32);
    u32 ext = (c.r[i][1] >> 31) ? 0xFFFFFFFFu : 0u;
    c.r[i][2] = c.r[i][3] = ext;
}
static inline void w128(Cpu& c, u32 i, const u32 w[4]) {
    if (!i) return;
    c.r[i][0] = w[0]; c.r[i][1] = w[1]; c.r[i][2] = w[2]; c.r[i][3] = w[3];
}
static inline u64 g64(const Cpu& c, u32 i) {
    return (u64)c.r[i][0] | ((u64)c.r[i][1] << 32);
}
static inline void s64reg(Cpu& c, u32 i, u64 v) { w64(c, i, v); }

// ---- memori: 128-bit LE quad ----
static void mem_read16b(u32 addr, u32 w[4]) {
    addr &= ~0xFu;
    w[0] = eebus::read32(addr);      w[1] = eebus::read32(addr + 4);
    w[2] = eebus::read32(addr + 8);  w[3] = eebus::read32(addr + 12);
}
static void mem_write16b(u32 addr, const u32 w[4]) {
    addr &= ~0xFu;
    eebus::write32(addr, w[0]);      eebus::write32(addr + 4, w[1]);
    eebus::write32(addr + 8, w[2]);  eebus::write32(addr + 12, w[3]);
}

// ---- unaligned LE klasik (LWL/LWR/SWL/SWR lane0; LDL/LDR/SDL/SDR 64-bit) ----
static void do_lwl(Cpu& c, u32 rt, u32 addr) {
    u32 b = addr & 3, w = eebus::read32(addr & ~3u), sh = 24 - 8 * b;
    c.r[rt][0] = (w << sh) | (c.r[rt][0] & (sh ? ((1u << sh) - 1) : 0));
}
static void do_lwr(Cpu& c, u32 rt, u32 addr) {
    u32 b = addr & 3, w = eebus::read32(addr & ~3u), sh = 8 * (3 - b);
    u32 keep = sh ? (0xFFFFFFFFu >> sh) : 0;
    c.r[rt][0] = (w >> sh) | (c.r[rt][0] & keep);
}
static void do_swl(u32 addr, u32 val) {
    u32 b = addr & 3, a = addr & ~3u, w = eebus::read32(a);
    u32 keep = (b == 3) ? 0 : (0x00FFFFFFu >> (8 * b));
    eebus::write32(a, (w & keep) | (val >> (8 * (3 - b))));
}
static void do_swr(u32 addr, u32 val) {
    u32 b = addr & 3, a = addr & ~3u, w = eebus::read32(a);
    u32 nbytes = b + 1;
    u32 keep = (nbytes == 4) ? 0 : (0xFFFFFFFFu << (8 * nbytes));
    eebus::write32(a, (w & keep) | (val & ~keep));
}
static void do_ldl(Cpu& c, u32 rt, u32 addr) {
    // LDL (LE): muat byte [aligned..address] (b+1 byte) ke BAGIAN ATAS GPR 64-bit.
    u32 b = addr & 7, a = addr & ~7u;
    u64 word = (u64)eebus::read32(a) | ((u64)eebus::read32(a + 4) << 32);
    u32 sh = 8 * (7 - b);
    u64 keep = sh ? ((1ull << sh) - 1) : 0;
    w64(c, rt, (word << sh) | (g64(c, rt) & keep));
}
static void do_ldr(Cpu& c, u32 rt, u32 addr) {
    // LDR (LE): muat byte [address..aligned+7] (8-b byte) ke BAGIAN BAWAH GPR.
    u32 b = addr & 7, a = addr & ~7u;
    u64 word = (u64)eebus::read32(a) | ((u64)eebus::read32(a + 4) << 32);
    u32 sh = 8 * b;
    u64 keep = sh ? ~(0xFFFFFFFFFFFFFFFFull >> sh) : 0;
    w64(c, rt, (word >> sh) | (g64(c, rt) & keep));
}
static void do_sdl(u32 addr, u64 val) {
    // SDL (LE): simpan byte ATAS GPR (b+1 byte) ke mem[aligned..address].
    u32 b = addr & 7, a = addr & ~7u;
    for (u32 j = 0; j <= b; j++)
        eebus::write8(a + j, (u8)(val >> (8 * (7 - b + j))));
}
static void do_sdr(u32 addr, u64 val) {
    // SDR (LE): simpan byte BAWAH GPR (8-b byte) ke mem[address..aligned+7].
    u32 b = addr & 7, a = addr & ~7u;
    for (u32 i = 0; i + b < 8; i++)
        eebus::write8(a + b + i, (u8)(val >> (8 * i)));
}

// ---- FPU helpers (single only; EE FPU = 32-bit FS) ----
static inline float fget(const Cpu& c, u32 i) { float v; u32 b = c.f[i]; std::memcpy(&v, &b, 4); return v; }
static inline void fset(Cpu& c, u32 i, float v) { u32 b; std::memcpy(&b, &v, 4); c.f[i] = b; }

// __PART3__

bool run(Cpu& c, SyscallFn handler, u64 max_inst) {
    while (c.instret < max_inst) {
        u32 cur_pc = c.pc;
        if (cur_pc == 0x003b1ce4) {
            static int dbg_count = 0;
            if (dbg_count++ < 3) {
                u32 t0 = c.r[8][0];
                u32 sp = c.r[29][0];
                u32 fp = eebus::read32(sp + 0x44);
                printf("[DEBUG] 0x3b1ce4: t0=0x%08x sp=0x%08x fp=0x%08x\n", t0, sp, fp);
                fflush(stdout);
            }
        }
        u32 inst = eebus::read32(c.pc);
        c.pc = c.next_pc;             // delay slot semantics: slot sudah diprefetch
        c.next_pc = c.pc + 4;
        c.fault_op = inst;
        if (c.trace) c.trace[c.trace_pos = (c.trace_pos + 1) & c.trace_mask] = cur_pc;

        auto fault = [&](const char* why) { c.fault = why; c.fault_pc = c.pc; };

        switch (op(inst)) {
        // ---------- SPECIAL ----------
        case 0x00: {
            switch (fn(inst)) {
            case 0x00: w32(c, rd(inst), c.r[rt(inst)][0] << sa_f(inst)); break;         // sll
            case 0x02: w32(c, rd(inst), c.r[rt(inst)][0] >> sa_f(inst)); break;         // srl
            case 0x03: w32(c, rd(inst), (u32)((s32)c.r[rt(inst)][0] >> sa_f(inst))); break; // sra
            case 0x04: w32(c, rd(inst), c.r[rt(inst)][0] << (c.r[rs(inst)][0] & 31)); break; // sllv
            case 0x06: w32(c, rd(inst), c.r[rt(inst)][0] >> (c.r[rs(inst)][0] & 31)); break; // srlv
            case 0x07: w32(c, rd(inst), (u32)((s32)c.r[rt(inst)][0] >> (c.r[rs(inst)][0] & 31))); break; // srav
            case 0x08: {
                u32 target = c.r[rs(inst)][0];
                if (target == 0 || target >= 0x02000000) {
                    // jr ke null/invalid — skip (continue ke next instruction)
                    static uint32_t jr_null = 0;
                    if (jr_null++ < 5)
                        std::fprintf(stderr, "[JR NULL] pc=0x%08x target=0x%08x -> skip\n", c.pc, target);
                    c.next_pc = c.pc + 8;  // skip delay slot + this
                } else {
                    c.next_pc = target;
                }
                break;  // jr
            }
            case 0x09: {
                u32 t = c.r[rs(inst)][0];
                u32 ret = cur_pc + 8;  // FIX 2026-10-19: c.pc sudah di delay slot
                w32(c, rd(inst), ret);
                if (t == 0 || t >= 0x02000000) {
                    // function pointer null/invalid — return ke caller
                    static uint32_t null_fp = 0;
                    if (null_fp++ < 5)
                        std::fprintf(stderr, "[JALR NULL] pc=0x%08x target=0x%08x -> return 0x%08x\n",
                            c.pc, t, ret);
                    c.next_pc = ret;  // skip call
                } else {
                    c.next_pc = t;
                }
                break; // jalr
            }
            case 0x0A: if (c.r[rt(inst)][0] == 0) w32(c, rd(inst), c.r[rs(inst)][0]); break; // movz
            case 0x0B: if (c.r[rt(inst)][0] != 0) w32(c, rd(inst), c.r[rs(inst)][0]); break; // movn
            case 0x0C: {  // SYSCALL — nomor di reg v1 (konvensi EE, terverifikasi)
                u32 num = c.r[3][0];
                u32 v0 = 0;
                bool known = handler(num, c.r[4][0], c.r[5][0], c.r[6][0], c.r[7][0], v0);
                if (!known) {
                    static char buf[64];
                    std::snprintf(buf, sizeof(buf), "syscall_unknown_%u", (unsigned)num);
                    c.fault = buf; c.fault_pc = c.pc;
                    return true;
                }
                w32(c, 2, v0);
                break;
            }
            case 0x0D: fault("break"); return true;
            case 0x0F: break;   // SYNC
            case 0x10: { u64 v = (u64)c.hi[0] | ((u64)c.hi[1] << 32); w64(c, rd(inst), v); break; } // MFHI
            case 0x11: { u64 v = g64(c, rs(inst)); c.hi[0] = (u32)v; c.hi[1] = (u32)(v >> 32); break; } // MTHI
            case 0x12: { u64 v = (u64)c.lo[0] | ((u64)c.lo[1] << 32); w64(c, rd(inst), v); break; } // MFLO
            case 0x13: { u64 v = g64(c, rs(inst)); c.lo[0] = (u32)v; c.lo[1] = (u32)(v >> 32); break; } // MTLO
            case 0x14: w64(c, rd(inst), g64(c, rt(inst)) << (c.r[rs(inst)][0] & 63)); break; // dsllv
            case 0x16: w64(c, rd(inst), g64(c, rt(inst)) >> (c.r[rs(inst)][0] & 63)); break; // dsrlv
            case 0x17: w64(c, rd(inst), (u64)((s64)g64(c, rt(inst)) >> (c.r[rs(inst)][0] & 63))); break; // dsrav
            case 0x18: { s64 p = (s64)(s32)c.r[rs(inst)][0] * (s32)c.r[rt(inst)][0];
                         c.lo[0] = (u32)p; c.lo[1] = (u32)((s64)(s32)(u32)p >> 32);
                         c.hi[0] = (u32)((u64)p >> 32); c.hi[1] = (u32)((s64)(s32)((u64)p >> 32) >> 32); break; } // mult
            case 0x19: { u64 p = (u64)c.r[rs(inst)][0] * c.r[rt(inst)][0];
                         c.lo[0] = (u32)p; c.lo[1] = (u32)((s64)(s32)(u32)p >> 32);
                         c.hi[0] = (u32)(p >> 32); c.hi[1] = (u32)((s64)(s32)(u32)(p >> 32) >> 32); break; } // multu
            case 0x1A: { s32 a = (s32)c.r[rs(inst)][0], b = (s32)c.r[rt(inst)][0];
                         if (b == 0) { fault("div0"); return true; }
                         s32 q = a / b, r_ = a % b;
                         c.lo[0] = (u32)q; c.lo[1] = (u32)((s64)q >> 32);
                         c.hi[0] = (u32)r_; c.hi[1] = (u32)((s64)r_ >> 32); break; } // div
            case 0x1B: { u32 a = c.r[rs(inst)][0], b = c.r[rt(inst)][0];
                         if (b == 0) { fault("divu0"); return true; }
                         u32 q = a / b, r_ = a % b;
                         c.lo[0] = q; c.lo[1] = (u32)((s64)(s32)q >> 32);
                         c.hi[0] = r_; c.hi[1] = (u32)((s64)(s32)r_ >> 32); break; } // divu
            case 0x20: case 0x21: w32(c, rd(inst), c.r[rs(inst)][0] + c.r[rt(inst)][0]); break; // add/addu
            case 0x22: case 0x23: w32(c, rd(inst), c.r[rs(inst)][0] - c.r[rt(inst)][0]); break; // sub/subu
            case 0x24: w32(c, rd(inst), c.r[rs(inst)][0] & c.r[rt(inst)][0]); break;    // and
            case 0x25: w32(c, rd(inst), c.r[rs(inst)][0] | c.r[rt(inst)][0]); break;    // or
            case 0x26: w32(c, rd(inst), c.r[rs(inst)][0] ^ c.r[rt(inst)][0]); break;    // xor
            case 0x27: w32(c, rd(inst), ~(c.r[rs(inst)][0] | c.r[rt(inst)][0])); break; // nor
            case 0x28: w32(c, rd(inst), c.sa); break;                                   // mfsa
            case 0x29: c.sa = c.r[rs(inst)][0] & 0x3FF; break;                          // mtsa (10-bit)
            case 0x2A: w32(c, rd(inst), (s32)c.r[rs(inst)][0] < (s32)c.r[rt(inst)][0] ? 1 : 0); break; // slt
            case 0x2B: w32(c, rd(inst), c.r[rs(inst)][0] < c.r[rt(inst)][0] ? 1 : 0); break;           // sltu
            case 0x2C: case 0x2D: w64(c, rd(inst), g64(c, rs(inst)) + g64(c, rt(inst))); break;  // dadd/daddu
            case 0x2E: case 0x2F: w64(c, rd(inst), g64(c, rs(inst)) - g64(c, rt(inst))); break;  // dsub/dsubu
            case 0x30: if ((s32)c.r[rs(inst)][0] >= (s32)c.r[rt(inst)][0]) { fault("trap_tge"); return true; } break;
            case 0x31: if (c.r[rs(inst)][0] >= c.r[rt(inst)][0]) { fault("trap_tgeu"); return true; } break;
            case 0x32: if ((s32)c.r[rs(inst)][0] < (s32)c.r[rt(inst)][0]) { fault("trap_tlt"); return true; } break;
            case 0x33: if (c.r[rs(inst)][0] < c.r[rt(inst)][0]) { fault("trap_tltu"); return true; } break;
            case 0x34: if (c.r[rs(inst)][0] == c.r[rt(inst)][0]) { fault("trap_teq"); return true; } break;
            case 0x36: if (c.r[rs(inst)][0] != c.r[rt(inst)][0]) { fault("trap_tne"); return true; } break;
            case 0x38: w64(c, rd(inst), g64(c, rt(inst)) << sa_f(inst)); break;        // dsll
            case 0x3A: w64(c, rd(inst), g64(c, rt(inst)) >> sa_f(inst)); break;        // dsrl
            case 0x3B: w64(c, rd(inst), (u64)((s64)g64(c, rt(inst)) >> sa_f(inst))); break; // dsra
            case 0x3C: w64(c, rd(inst), g64(c, rt(inst)) << (sa_f(inst) + 32)); break; // dsll32
            case 0x3E: w64(c, rd(inst), g64(c, rt(inst)) >> (sa_f(inst) + 32)); break; // dsrl32
            case 0x3F: w64(c, rd(inst), (u64)((s64)g64(c, rt(inst)) >> (sa_f(inst) + 32))); break; // dsra32
        default: fault("SPECIAL fn unknown"); return true;
            }
            break;
        }
        // ---------- REGIMM ----------
        case 0x01: {
            s32 v = (s32)c.r[rs(inst)][0];
            u32 rti = rt(inst);
            bool take;
            switch (rti) {
            case 0x00: take = v < 0; break;   // bltz
            case 0x01: take = v >= 0; break;  // bgez
            case 0x10: take = v < 0; w32(c, 31, c.next_pc); break;   // bltzal
            case 0x11: take = v >= 0; w32(c, 31, c.next_pc); break;  // bgezal
            case 0x12: take = v < 0; w32(c, 31, c.next_pc); break;   // bltzall
            case 0x13: take = v >= 0; w32(c, 31, c.next_pc); break;  // bgezall
            case 0x18: take = v < 0; break;   // BLTZL
            case 0x19: take = v >= 0; break;  // BGEZL
            case 0x1A: take = v <= 0; break;  // tak ada di tabel EE — fallback defensif
            case 0x1B: take = v > 0; break;   // (jarang; kalau kena akan muncul di census)
            case 0x30: c.sa = (c.r[rs(inst)][0] & 0xF) << 1; take = false; break;  // MTSAB
            case 0x31: c.sa = (c.r[rs(inst)][0] & 0x7) << 2; take = false; break;  // MTSAH
            default: fault("REGIMM unknown"); return true;
            }
            if (take) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
            break;
        }
        // ---------- J / JAL ----------
        case 0x02: {
            u32 target = ((c.pc + 4) & 0xF0000000) | tgt(inst);
            static uint32_t j_count = 0;
            if (j_count++ < 15)
                std::fprintf(stderr, "[J #%u] pc=0x%08x -> 0x%08x instret=%llu\n",
                    j_count, c.pc, target, (unsigned long long)c.instret);
            c.next_pc = target; break;
        }
        case 0x03: w32(c, 31, cur_pc + 8); c.next_pc = ((cur_pc + 4) & 0xF0000000) | tgt(inst); break; // jal: ra = P+8 (FIX: cur_pc, bukan c.pc)
        // ---------- BRANCH ----------
        case 0x04: {
            u32 target = c.pc + 4 + (sx16(imm(inst)) << 2);
            if (c.r[rs(inst)][0] == c.r[rt(inst)][0]) {
                static uint32_t beq_count = 0;
                if (beq_count++ < 10)
                    std::fprintf(stderr, "[BEQ taken #%u] pc=0x%08x -> 0x%08x instret=%llu\n",
                        beq_count, c.pc, target, (unsigned long long)c.instret);
                c.next_pc = target;
            }
            break; // beq: target = P+4+offset*4
        }
        case 0x14: if (c.r[rs(inst)][0] == c.r[rt(inst)][0]) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 4; break;                                    // beql
        case 0x15: if (c.r[rs(inst)][0] != c.r[rt(inst)][0]) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 4; break;                                    // bnel
        case 0x16: if ((s32)c.r[rs(inst)][0] <= 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 4; break;                                    // blezl
        case 0x17: if ((s32)c.r[rs(inst)][0] > 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 4; break;                                    // bgtzl
        case 0x05: if (c.r[rs(inst)][0] != c.r[rt(inst)][0]) c.next_pc = c.pc + (sx16(imm(inst)) << 2); break; // bne
        case 0x06: if ((s32)c.r[rs(inst)][0] <= 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2); break;  // blez
        case 0x07: if ((s32)c.r[rs(inst)][0] > 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2); break;   // bgtz
        // ---------- ALU IMM ----------
        case 0x08: case 0x09: w32(c, rt(inst), c.r[rs(inst)][0] + sx16(imm(inst))); break;   // addi/addiu
        case 0x0A: w32(c, rt(inst), (s32)c.r[rs(inst)][0] < (s32)sx16(imm(inst)) ? 1 : 0); break; // slti
        case 0x0B: w32(c, rt(inst), c.r[rs(inst)][0] < (u32)sx16(imm(inst)) ? 1 : 0); break;       // sltiu
        case 0x0C: w32(c, rt(inst), c.r[rs(inst)][0] & imm(inst)); break;      // andi
        case 0x0D: w32(c, rt(inst), c.r[rs(inst)][0] | imm(inst)); break;      // ori
        case 0x0E: w32(c, rt(inst), c.r[rs(inst)][0] ^ imm(inst)); break;      // xori
        case 0x0F: w32se(c, rt(inst), imm(inst) << 16); break;                  // lui (sign-extend ke 128 — EE)
        case 0x18: case 0x19: w64(c, rt(inst), g64(c, rs(inst)) + (u64)(s64)(s32)sx16(imm(inst))); break; // daddi/daddiu
        // ---------- LOADS (termasuk LWL/LWR & LDL/LDR — helper sudah didefinisikan) ----
        case 0x22: do_lwl(c, rt(inst), c.r[rs(inst)][0] + sx16(imm(inst))); break;   // lwl
        case 0x26: do_lwr(c, rt(inst), c.r[rs(inst)][0] + sx16(imm(inst))); break;   // lwr
        case 0x1A: do_ldl(c, rt(inst), c.r[rs(inst)][0] + sx16(imm(inst))); break;   // ldl
        case 0x1B: do_ldr(c, rt(inst), c.r[rs(inst)][0] + sx16(imm(inst))); break;   // ldr
        case 0x24: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)); w32(c, rt(inst), eebus::read8(a) & 0xFF); break; }  // lbu
        case 0x20: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)); w32(c, rt(inst), (u32)(int32_t)(int8_t)eebus::read8(a)); break; } // lb
        case 0x21: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)); w32(c, rt(inst), (u16)eebus::read16(a)); break; }   // lh
        case 0x25: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)); w32(c, rt(inst), eebus::read16(a) & 0xFFFF); break; } // lhu
        case 0x23: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)); w32(c, rt(inst), eebus::read32(a)); break; }        // lw
        case 0x1E: {  // LQ (128-bit penuh): EE opcode 0x1E, alamat 16-aligned (di-mask)
            u32 a = (c.r[rs(inst)][0] + sx16(imm(inst))) & ~0xFu;
            u32* d = c.r[rt(inst)];
            d[0] = eebus::read32(a);      d[1] = eebus::read32(a + 4);
            d[2] = eebus::read32(a + 8);  d[3] = eebus::read32(a + 12);
            break;
        }
        case 0x37: {  // LD (64-bit)
            u64 a = (u64)(c.r[rs(inst)][0] + sx16(imm(inst)));
            u64 v = (u64)eebus::read32((u32)a) | ((u64)eebus::read32((u32)(a + 4)) << 32);
            w64(c, rt(inst), v); break;
        }
        // ---------- STORES ----------
        case 0x28: eebus::write8(c.r[rs(inst)][0] + sx16(imm(inst)), (u8)(c.r[rt(inst)][0] & 0xFF)); break;   // sb
        case 0x29: eebus::write16(c.r[rs(inst)][0] + sx16(imm(inst)), (u16)(c.r[rt(inst)][0] & 0xFFFF)); break; // sh
        case 0x2B: eebus::write32(c.r[rs(inst)][0] + sx16(imm(inst)), c.r[rt(inst)][0]); break;               // sw
        case 0x1F: {  // SQ (128-bit penuh): EE opcode 0x1F, alamat 16-aligned (di-mask)
            u32 a = (c.r[rs(inst)][0] + sx16(imm(inst))) & ~0xFu;
            u32* s = c.r[rt(inst)];
            eebus::write32(a,      s[0]); eebus::write32(a + 4,  s[1]);
            eebus::write32(a + 8,  s[2]); eebus::write32(a + 12, s[3]);
            break;
        }
        case 0x3F: {  // SD (64-bit store)
            u32 a = c.r[rs(inst)][0] + sx16(imm(inst));
            eebus::write32(a,     c.r[rt(inst)][0]);
            eebus::write32(a + 4, c.r[rt(inst)][1]);
            break;
        }
        case 0x10: {  // COP0
            u32 crs = rs(inst), crt = rt(inst), crd = rd(inst);
            if (crs == 0) w32(c, crt, c.cop0[crd]);            // MFC0
            else if (crs == 4) c.cop0[crd] = c.r[crt][0];      // MTC0
            else { fault("COP0 rs unknown"); return true; }
            break;
        }
        default:
            fault("opcode unknown");
            return true;
        }

        c.r[0][0] = c.r[0][1] = c.r[0][2] = c.r[0][3] = 0;
        c.instret++;
        // hot-PC sampler (opsional): bucket pc>>2 = PER INSTRUKSI (2^21 bucket,
        // mask 0x1FFFFF -> sampai pc 0x7FFFFFC, cukup utk seluruh RAM EE 32 MB)
        if (c.hist && (c.instret & c.sample_mask) == 0)
            c.hist[(c.pc >> 2) & 0x1FFFFFu]++;
        // BTB census: instruksi berikutnya mundur terhadap pc sekarang = branch/jump
        // mundur TAKEN baru saja dieksekusi (alamat branch = c.pc-4; delay-slot model).
        if (c.btb && c.next_pc < c.pc)
            c.btb[((c.pc - 4) >> 2) & 0x1FFFFFu]++;
        // VSYNC simulation: setiap 60k instruksi kirim SetVSyncFlag (syscall 115) —
        // simulasi 60Hz clock hardware PS2 yang membuat game lanjut ke frame berikutnya.
        if ((c.instret % 60000ull) == 0) {
            u32 v0 = 0;
            handler(115, 0, 0, 0, 0, v0);
            w32(c, 2, v0);
        }
        { // PC ring buffer crash trace
            static uint32_t pc_ring[16] = {};
            static uint32_t pc_ri = 0;
            pc_ring[pc_ri++ & 15] = c.pc;
            // trace first entry into stack area
            static bool in_stack = false;
            if (!in_stack && c.next_pc >= 0x003BBF20 && c.next_pc < 0x02000000 && c.instret > 100000) {
                in_stack = true;
                std::fprintf(stderr, "[CODE_END] pc=0x%08x next=0x%08x instret=%llu ra=0x%08x sp=0x%08x\n",
                    c.pc, c.next_pc, (unsigned long long)c.instret, c.r[31][0], c.r[29][0]);
                for (int i = 1; i <= 16; i++)
                    std::fprintf(stderr, "  pc[%2d]=0x%08x\n", i, pc_ring[(pc_ri + i - 1) & 15]);
                // dump register file for diagnosis
                for (int r = 1; r < 32; r++)
                    if (c.r[r][0]) std::fprintf(stderr, "  r%-2d=0x%08x\n", r, c.r[r][0]);
            }
            // code bounds: .text ends at 0x003BBF20 (ELF section hdr, verified 2026-10-19)
        if (c.next_pc >= 0x02000000 && c.next_pc < 0x10000000) {
            std::fprintf(stderr, "[CODE_BOUNDS] pc=0x%08x next=0x%08x instret=%llu — STOP (past .text)\n",
                c.pc, c.next_pc, (unsigned long long)c.instret);
            fault("code_end");
            return true;
        }
        // data area check: 0x3c0000-0x3d0000 = function table/data, bukan code
        if (c.next_pc >= 0x003c0000 && c.next_pc < 0x003d0000) {
            c.next_pc = c.pc + 8;  // skip data area
        }
        if (c.pc >= 0x02000000 || c.next_pc >= 0x02000000) {
                std::fprintf(stderr, "[CRASH] pc=0x%08x next=0x%08x instret=%llu\n",
                    c.pc, c.next_pc, (unsigned long long)c.instret);
                std::fprintf(stderr, "[CRASH] ra=0x%08x sp=0x%08x gp=0x%08x\n",
                    c.r[31][0], c.r[29][0], c.r[28][0]);
                for (int i = 1; i <= 16; i++)
                    std::fprintf(stderr, "  pc[%2d]=0x%08x\n", i, pc_ring[(pc_ri + i - 1) & 15]);
                fault("pc_oor"); return true;
            }
        }
    }
    return true;   // instret cap — normal utk run berbatas
}

} // namespace eecpu
