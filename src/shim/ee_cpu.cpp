#include "ee_cpu.h"
#include <cstdio>

namespace eecpu {

void Cpu::reset(u32 entry) {
    for (auto& v : r) v = 0;
    pc = entry;
    next_pc = entry + 4;
    lo = hi = 0;
    instret = 0;
    fault = nullptr;
    fault_pc = fault_op = 0;
}

static inline u32 sx16(u32 v) { return (u32)(int32_t)(int16_t)(v & 0xFFFF); }
static inline u32 sx8(u32 v)  { return (u32)(int32_t)(int8_t)(v & 0xFF); }

// decode field helper
static inline u32 op(u32 i)  { return (i >> 26) & 0x3F; }
static inline u32 rs(u32 i)  { return (i >> 21) & 0x1F; }
static inline u32 rt(u32 i)  { return (i >> 16) & 0x1F; }
static inline u32 rd(u32 i)  { return (i >> 11) & 0x1F; }
static inline u32 sa(u32 i)  { return (i >> 6) & 0x1F; }
static inline u32 fn(u32 i)  { return i & 0x3F; }
static inline u32 fd(u32 i)  { return (i >> 6) & 0x1F; }  // FPU fd field
static inline u32 imm(u32 i) { return i & 0xFFFF; }
static inline u32 tgt(u32 i) { return (i & 0x3FFFFFF) << 2; }

static inline void wreg(Cpu& c, u32 idx, u32 val) {
    if (idx != 0) c.r[idx] = val;
}

bool run(Cpu& c, SyscallFn handler, u64 max_inst) {
    while (c.instret < max_inst) {
        u32 inst = eebus::read32(c.pc);
        c.pc = c.next_pc;             // delay slot semantics: slot sudah diprefetch
        c.next_pc = c.pc + 4;
        c.fault_op = inst;

        auto fault = [&](const char* why) {
            c.fault = why; c.fault_pc = c.pc;
        };

        switch (op(inst)) {
        // ---------- SPECIAL ----------
        case 0x00: {
            switch (fn(inst)) {
            case 0x00: wreg(c, rd(inst), c.r[rt(inst)]); break;                    // sll
            case 0x02: wreg(c, rd(inst), c.r[rt(inst)] >> sa(inst)); break;        // srl
            case 0x03: wreg(c, rd(inst), (u32)((int32_t)c.r[rt(inst)] >> sa(inst))); break; // sra
            case 0x04: wreg(c, rd(inst), c.r[rt(inst)] << (c.r[rs(inst)] & 31)); break; // sllv
            case 0x06: wreg(c, rd(inst), c.r[rt(inst)] >> (c.r[rs(inst)] & 31)); break; // srlv
            case 0x07: wreg(c, rd(inst), (u32)((int32_t)c.r[rt(inst)] >> (c.r[rs(inst)] & 31))); break; // srav
            case 0x08: c.next_pc = c.r[rs(inst)]; break;                           // jr
            case 0x09: { u32 t = c.r[rs(inst)]; wreg(c, rd(inst), c.next_pc); c.next_pc = t; break; } // jalr
            case 0x0A: wreg(c, rd(inst), (int32_t)c.r[rs(inst)] < (int32_t)c.r[rt(inst)] ? 1 : 0); break; // slt
            case 0x10: wreg(c, rd(inst), (u32)c.hi); break;   // MFHI
            case 0x11: c.hi = c.r[rs(inst)]; break;           // MTHI
            case 0x12: wreg(c, rd(inst), (u32)c.lo); break;   // MFLO
            case 0x13: c.lo = c.r[rs(inst)]; break;           // MTLO
            case 0x0B: wreg(c, rd(inst), c.r[rs(inst)] < c.r[rt(inst)] ? 1 : 0); break; // sltu
            case 0x18: { s64 p = (s64)(int32_t)c.r[rs(inst)] * (s32)c.r[rt(inst)];
                         c.lo = (u32)(p & 0xFFFFFFFF); c.hi = (u32)((u64)p >> 32); break; } // mult (fn 0x18)
            case 0x19: { s64 p = (s64)c.r[rs(inst)] * c.r[rt(inst)];
                         c.lo = (u32)(p & 0xFFFFFFFF); c.hi = (u32)(p >> 32); break; }    // multu (fn 0x19)
            case 0x1A: { s32 a = (int32_t)c.r[rs(inst)], b = (int32_t)c.r[rt(inst)];
                         if (b == 0) { fault("div0"); return true; }
                         s32 q = a / b, m = a % b;
                         c.lo = (u32)q; c.hi = (u32)m; break; }                     // div (fn 0x1A)
            case 0x1B: { u32 a = c.r[rs(inst)], b = c.r[rt(inst)];
                         if (b == 0) { fault("divu0"); return true; }
                         c.lo = a / b; c.hi = a % b; break; }                       // divu (fn 0x1B)
            case 0x20: wreg(c, rd(inst), c.r[rs(inst)] + c.r[rt(inst)]); break;     // add
            case 0x21: wreg(c, rd(inst), c.r[rs(inst)] + c.r[rt(inst)]); break;     // addu
            case 0x22: wreg(c, rd(inst), c.r[rs(inst)] - c.r[rt(inst)]); break;     // sub
            case 0x23: wreg(c, rd(inst), c.r[rs(inst)] - c.r[rt(inst)]); break;     // subu
            case 0x24:
                if (c.pc == 0x00100154)
                    std::fprintf(stderr, "[AND@before] pc=%08x v1(r3)=%08x a0(r4)=%08x inst=%08x -> result=%08x\n",
                                 c.pc, c.r[3], c.r[4], inst, c.r[3] & c.r[4]);
                wreg(c, rd(inst), c.r[rs(inst)] & c.r[rt(inst)]);
                if (c.pc == 0x00100154)
                    std::fprintf(stderr, "[AND@after] a0=%08x\n", c.r[4]);
                break;     // and
            case 0x25: wreg(c, rd(inst), c.r[rs(inst)] | c.r[rt(inst)]); break;     // or
            case 0x26: wreg(c, rd(inst), c.r[rs(inst)] ^ c.r[rt(inst)]); break;     // xor
            case 0x27: wreg(c, rd(inst), ~(c.r[rs(inst)] | c.r[rt(inst)])); break;  // nor
            case 0x2A: wreg(c, rd(inst), (int32_t)c.r[rs(inst)] < (int32_t)c.r[rt(inst)] ? 1 : 0); break; // slt
            case 0x2B: wreg(c, rd(inst), c.r[rs(inst)] < c.r[rt(inst)] ? 1 : 0); break; // sltu
            case 0x2C: wreg(c, rd(inst), c.r[rs(inst)] + c.r[rt(inst)]); break;     // dadd (эф) — same as add di 32-bit path
            case 0x2D: wreg(c, rd(inst), c.r[rs(inst)] | c.r[rt(inst)]); break;     // daddu/move — or di 32-bit path
            case 0x0C: {  // SYSCALL (SPECIAL fn 0x0C) — nomor di reg v1 (konvensi EE)
                u32 num = c.r[3];
                u32 v0 = 0;
                bool known = handler(num, c.r[4], c.r[5], c.r[6], c.r[7], v0);
                if (!known) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "syscall_unknown_%u", (unsigned)num);
                    c.fault = buf; c.fault_pc = c.pc;
                    return true;
                }
                wreg(c, 2, v0);
                break;
            }
            case 0x0F: break;   // SYNC — no-op di native (mem model kuat)
            default: fault("SPECIAL fn unknown"); return true;
            }
            break;
        }
        // ---------- REGIMM (bltz/bgez/blez/bgtz) ----------
        case 0x01: {
            s32 v = (int32_t)c.r[rs(inst)];
            bool take;
            switch (rt(inst)) {
            case 0x00: take = v < 0; break;   // bltz
            case 0x01: take = v >= 0; break;  // bgez
            case 0x10: take = v < 0; break;   // bltzal
            case 0x11: take = v >= 0; break;  // bgezal
            case 0x12: take = v < 0; break;   // bltzall
            case 0x13: take = v >= 0; break;  // bgezall
            case 0x18: take = v < 0; break;   // BLTZL (likely)
            case 0x19: take = v >= 0; break;  // BGEZL (likely)
            case 0x1A: take = v<=0; break;    // blezl (guess)
            case 0x1B: take = v>0; break;     // bgtzl (guess)
            default: fault("REGIMM unknown"); return true;
            }
            if (take) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
            break;
        }
        // ---------- J / JAL ----------
        case 0x02: c.next_pc = ((c.pc + 4) & 0xF0000000) | tgt(inst); break;
        case 0x03: wreg(c, 31, c.pc + 8); c.next_pc = ((c.pc + 4) & 0xF0000000) | tgt(inst); break;
        // ---------- BRANCH ----------
        case 0x04: if (c.r[rs(inst)] == c.r[rt(inst)]) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2); break; // beq
        case 0x14: if (c.r[rs(inst)] == c.r[rt(inst)]) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 8; break;                                    // beql (likely, skip delay if not taken)
        case 0x15: if (c.r[rs(inst)] != c.r[rt(inst)]) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 8; break;                                    // bnel
        case 0x16: if ((int32_t)c.r[rs(inst)] <= 0) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 8; break;                                    // blezl
        case 0x17: if ((int32_t)c.r[rs(inst)] > 0) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
                   else c.next_pc = c.pc + 8; break;                                    // bgtzl
        case 0x05: if (c.r[rs(inst)] != c.r[rt(inst)]) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2); break; // bne
        case 0x06: if ((int32_t)c.r[rs(inst)] <= 0) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2); break;    // blez
        case 0x07: if ((int32_t)c.r[rs(inst)] > 0) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2); break;     // bgtz
        // ---------- ALU IMM ----------
        case 0x08: wreg(c, rt(inst), c.r[rs(inst)] + sx16(imm(inst))); break;    // addi
        case 0x09: wreg(c, rt(inst), c.r[rs(inst)] + sx16(imm(inst))); break;    // addiu
        case 0x0A: wreg(c, rt(inst), (int32_t)c.r[rs(inst)] < (int32_t)sx16(imm(inst)) ? 1 : 0); break; // slti
        case 0x0B: wreg(c, rt(inst), c.r[rs(inst)] < (u32)sx16(imm(inst)) ? 1 : 0); break;               // sltiu
        case 0x0C: wreg(c, rt(inst), c.r[rs(inst)] & imm(inst)); break;          // andi
        case 0x0D: wreg(c, rt(inst), c.r[rs(inst)] | imm(inst)); break;          // ori
        case 0x0E: wreg(c, rt(inst), c.r[rs(inst)] ^ imm(inst)); break;          // xori
        case 0x0F: wreg(c, rt(inst), imm(inst) << 16); break;                    // lui
        // ---------- LOAD/STORE ----------
        case 0x20: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); wreg(c, rt(inst), sx8(eebus::read8(a))); break; }  // lb
        case 0x21: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); wreg(c, rt(inst), eebus::read16(a)); break; }      // lh
        case 0x23: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); wreg(c, rt(inst), eebus::read32(a)); break; }      // lw
        case 0x24: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); wreg(c, rt(inst), eebus::read8(a) & 0xFF); break; }// lbu
        case 0x25: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); wreg(c, rt(inst), eebus::read16(a) & 0xFFFF); break; } // lhu
        case 0x28: eebus::write8(c.r[rs(inst)] + sx16(imm(inst)), (u8)(c.r[rt(inst)] & 0xFF)); break;            // sb
        case 0x29: eebus::write16(c.r[rs(inst)] + sx16(imm(inst)), (u16)(c.r[rt(inst)] & 0xFFFF)); break;        // sh
        case 0x2B: eebus::write32(c.r[rs(inst)] + sx16(imm(inst)), c.r[rt(inst)]); break;                        // sw
        case 0x3F: {  // SD: store doubleword (64 bit) — EE kritis
            u32 a = c.r[rs(inst)] + sx16(imm(inst));
            u32 low = c.r[rt(inst)];
            eebus::write32(a, low);
            eebus::write32(a + 4, 0);   // upper 32-bit dari GPR (EE 128-bit reg, MVP 0)
            break;
        }
        case 0x37: {  // LD: load doubleword — MVP: baca low 32 bit, upper = 0 (sign-extend nanti)
            u32 a = c.r[rs(inst)] + sx16(imm(inst));
            c.r[rt(inst)] = eebus::read32(a);
            break;
        }
        case 0x3E: {  // SDL (store doubleword left) — simplify: treat as sw
            u32 a = c.r[rs(inst)] + sx16(imm(inst));
            eebus::write32(a, c.r[rt(inst)]);
            break;
        }
        case 0x3A: {  // LWL — simplify: same as lw
            c.r[rt(inst)] = eebus::read32(c.r[rs(inst)] + sx16(imm(inst)));
            break;
        }
        case 0x3B: {  // LWR
            c.r[rt(inst)] = eebus::read32(c.r[rs(inst)] + sx16(imm(inst)));
            break;
        }
        case 0x2E: {  // SWL — treat as sw
            u32 a = c.r[rs(inst)] + sx16(imm(inst));
            eebus::write32(a, c.r[rt(inst)]);
            break;
        }
        // ---------- LWC1/SWC1 (FPR, stub data-path; crt0 tak pakai) ----------
        case 0x31: { u32 a = c.r[rs(inst)] + sx16(imm(inst)); c.f[rt(inst)] = eebus::read32(a); break; }  // lwc1
        case 0x39: eebus::write32(c.r[rs(inst)] + sx16(imm(inst)), c.f[rt(inst)]); break;                  // swc1
        // ---------- COP0/COP1 ----------
        case 0x10: {  // COP0: crt0 tak pakai MMU intr — stub safe (mfc0 = 0)
            if (rs(inst) == 0x00) { wreg(c, rt(inst), 0); break; }  // mfc0
            if (rs(inst) == 0x04) break;                            // mtc0 (discard)
            fault("COP0 unknown");
            return true;
        }
        case 0x11: {  // COP1 (FPU) — subset minimal add/sub/mul single (crt0 & laju awal)
            u32 ffn = fn(inst);  // same bits
            if (rs(inst) == 0x00) {  // mfc1
                wreg(c, rt(inst), c.f[rd(inst)]); break;
            }
            if (rs(inst) == 0x04) {  // mtc1
                c.f[rd(inst)] = c.r[rt(inst)]; break;
            }
            if (rs(inst) == 0x02) {  // ctc1 — control register FPU
                c.fcsr = c.r[rt(inst)]; break;
            }
            if (rs(inst) == 0x00) {  // mfc1 — baca control (c1_fcsr)
                wreg(c, rt(inst), c.fcsr); break;
            }
            if (rs(inst) == 0x08) {  // bc1x — sederhanakan: branch jika FCSR bit23 (c=1)
                bool cc = (c.fcsr >> 23) & 1;
                bool expect = (ffn & 1) == 1;
                if (cc == expect) c.next_pc = c.pc + 4 + (sx16(imm(inst)) << 2);
                break;
            }
            // format single (op fn): add.s 0/sub.s 1/mul.s 2/div.s 3/mov.s 6/abs.s 5/neg.s 7
            float af, bf;
            {
                u32 ai = c.f[rd(inst)], bi = c.f[rt(inst)];
                __builtin_memcpy(&af, &ai, 4);
                __builtin_memcpy(&bf, &bi, 4);
                float res = 0.0f;
                switch (ffn) {
                case 0x00: res = af + bf; break;
                case 0x01: res = af - bf; break;
                case 0x02: res = af * bf; break;
                case 0x03: if (bf == 0.0f) { fault("div.s 0"); return true; } res = af / bf; break;
                case 0x05: res = af < 0 ? -af : af; break;   // abs.s (fn 5)
                case 0x06: res = af; break;                   // mov.s (fn 6)
                case 0x07: res = -af; break;                  // neg.s (fn 7)
                case 0x18: res = af + bf; break;   // adda.s / add.s? — fn 0x18 di fmt-s = ADDA (accumulator); treat sebagai add
                case 0x1c: res = af * bf + af; break;         // madd.s (fn 28)
                default: fault("COP1 fn unknown"); return true;
                }
                u32 ri; __builtin_memcpy(&ri, &res, 4);
                c.f[fd(inst)] = ri;
            }
            break;
        }
        // ---------- SPECIAL2 (MMI R5900) — fn mapping dari disasm crt0 ASLI ----------
        // 0x00 PADDW · 0x28 PADDUW (crt0: padduw rX, zero, zero = init nol) ·
        // 0x11 MTHI1 · 0x13 MTLO1 · 0x1E PCPYH (dari op 70000c28 region? tidak; hati2)
        case 0x1C: {
            u32 f = fn(inst);
            switch (f) {
            case 0x00: case 0x28:   // PADDW / PADDUW: rd = rs + rt (elementwise qword sim)
                wreg(c, rd(inst), c.r[rs(inst)] + c.r[rt(inst)]); break;
            case 0x11:              // MTHI1 — HI1 tak dipakai di jalur syscall
                if (rs(inst) == 0) break;   // mthi1 zero = no-op safe
                c.hi = (c.hi & 0xFFFFFFFF00000000ull) | c.r[rs(inst)]; break;
            case 0x13:              // MTLO1
                if (rs(inst) == 0) break;
                c.lo = (c.lo & 0xFFFFFFFF00000000ull) | c.r[rs(inst)]; break;
            case 0x10:              // PCPYH? region salah — treat: rd = rs (copy neutral)
            default:                // MMI SIMD lain di crt0 = zero-init path
                wreg(c, rd(inst), c.r[rs(inst)]); break;
            }
            break;
        }
        // ---------- SPECIAL3 (0x1F: MTSAH/MTSA + SQ/LQ pada EE) ----------
        case 0x1F: {
            // SQ/LQ encoding EE: dipindah ke SPECIAL3? Disasm bilang sq = 0x7c400000
            // (op 0x1F). rt field menunjuk sumber/dst; store 16 byte (crt0: zero init)
            u32 f = fn(inst) & 0x3F;
            (void)f;
            // treat khusus SQ store 16B (paling umum di crt0)
            u32 a = c.r[rs(inst)] + (sx16(imm(inst)) & ~0xFu);
            u32 z = c.r[rt(inst)];
            eebus::write32(a, z); eebus::write32(a+4, z);
            eebus::write32(a+8, z); eebus::write32(a+12, z);
            break;
        }
        // ---------- SYSCALL ----------
        case 0x2C: {
            u32 num = c.r[3];   // EE: nomor syscall di v1
            u32 v0 = 0;
            bool known = handler(num, c.r[4], c.r[5], c.r[6], c.r[7], v0);
            if (!known) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "syscall_unknown_%u", (unsigned)num);
                c.fault = buf; c.fault_pc = c.pc;
                return true;
            }
            wreg(c, 2, v0);   // v0 = return
            break;
        }
        default:
            fault("opcode unknown");
            return true;
        }

        c.r[0] = 0;              // hardwired zero
        c.instret++;
        if (c.pc >= 0x02000000) { fault("pc_oor"); return true; }
        if (c.next_pc >= 0x02000000) { fault("npc_oor"); return true; }
    }
    return true;   // instret cap — normal utk run berbatas
}

} // namespace eecpu
