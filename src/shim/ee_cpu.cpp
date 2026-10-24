// ee_cpu.cpp — interpreter MIPS R5900 (EE little-endian) Phase 7b.
// Sumber semantik: EE Core manual (ps2tek tables) + PCSX2 interpreter (ps2-unpacker
// MMI.c) — MMI0/1 + hot MMI2/3 diimplement penuh; sisanya fault-driven (census jujur).
// LQ/SQ encoding diverifikasi dari pasangan prolog/epilog stack game (SQ op 0x1F,
// LQ op 0x1E, imm16 penuh 16-aligned). Unaligned LWL/LWR/SWL/SWR/SDL/SDR/LDL/LDR
// pakai tabel byte-level MIPS LE klasik.
#include "ee_cpu.h"
#include "ee_mmi.h"
#include <cstdio>
#include <cmath>
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
// VU0 micro operand fields (MIPS pos): fd=dest (inst[10:6]=sa), fs=src1 (inst[15:11]=rd),
// ft=src2 (inst[20:16]=rt). Dipakai decode COP2 SPECIAL1/SPECIAL2.
static inline u32 fd(u32 i)  { return (i >> 6) & 0x1F; }
static inline u32 fs(u32 i)  { return (i >> 11) & 0x1F; }
static inline u32 ft(u32 i)  { return (i >> 16) & 0x1F; }
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
    // LWR (LE): muat byte [addr..akhir word] ke bagian BAWAH rt.
    // b=0 -> seluruh word; b=1 -> (rt & 0xFF000000) | (w>>8); ... b=3 -> (rt & 0xFFFFFF00) | (w>>24).
    // Bug lama: keep/shisft terbalik -> lwl 3(x) + lwr 0(x) menghasilkan 0x00000033
    // (terukur di crt0: copy@0x781ba8=0x00000033 padahal sumber 0x30303133).
    u32 b = addr & 3, w = eebus::read32(addr & ~3u), sh = 8 * b;
    u32 keep = b ? (0xFFFFFFFFu << (8 * (4 - b))) : 0;
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

u64 g_delay_loops = 0;
u64 g_delay_inst  = 0;

// end-func callback = model interrupt SIF0: di HW asli _request_end() memanggil
// end_func(end_param, result) di konteks interrupt thread pemanggil (stack thread itu).
// Default HIDUP sejak 2026-10-24: tanpa ini libcdvd menggantung di flag busy 0x3F8674
// (terukur: 7,1 M iterasi spin, 0 modul IOP). GH_RPC_ENDFUNC=0 mematikannya.
static bool g_endfunc_on = [] {
    const char* v = std::getenv("GH_RPC_ENDFUNC");
    return !(v && v[0] == '0' && v[1] == 0);
}();

// antrean callback end-func — ukuran kecil, FIFO, nol alokasi
namespace {
struct CbReq { u32 f, a, r; };
CbReq g_cb[8];
int g_cb_head = 0, g_cb_n = 0;
}
void request_callback(u32 func, u32 arg, u32 result) {
    if (!func) return;
    if (g_cb_n >= 8) { g_cb_head = (g_cb_head + 1) % 8; g_cb_n--; }   // buang tertua
    g_cb[(g_cb_head + g_cb_n) % 8] = CbReq{func, arg, result};
    g_cb_n++;
}
static bool take_callback(u32& f, u32& a, u32& r) {
    if (g_cb_n <= 0) return false;
    CbReq q = g_cb[g_cb_head];
    g_cb_head = (g_cb_head + 1) % 8; g_cb_n--;
    f = q.f; a = q.a; r = q.r;
    return true;
}
u32 pending_callbacks() { return (u32)g_cb_n; }

bool run(Cpu& c, SyscallFn handler, u64 max_inst) {
    while (c.instret < max_inst) {
        u32 cur_pc = c.pc;

        if (eebus::g_watch_lo) eebus::g_cur_pc = cur_pc;
        // ---- halt + execution-window trace (observasi murni, nol ubah state) ----
        if (c.halt_pc && cur_pc == c.halt_pc) {
            if (c.halt_skipped < c.halt_skip) { c.halt_skipped++; }
            else { c.halt_hits++; return false; }
        }
        if (c.trace_lo && cur_pc >= c.trace_lo && cur_pc < c.trace_hi) {
            c.trace_win_hits++;
            if (c.trace_win_hits <= c.trace_win_cap)
                std::fprintf(stderr,
                    "[W]%llu pc=%08x inst=%08x v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
                    "s0=%08x s1=%08x sp=%08x ra=%08x\n",
                    (unsigned long long)c.instret, cur_pc, eebus::read32(cur_pc),
                    c.r[2][0], c.r[3][0], c.r[4][0], c.r[5][0], c.r[6][0], c.r[7][0],
                    c.r[16][0], c.r[17][0], c.r[29][0], c.r[31][0]);
        }

        // ---- Phase 7b.9: busy-delay fast-forward -------------------------
        // Idiom game (contoh 0x399190): addiu rt,rt,-1 ; 4x nop ; bne rt,rs,<self>
        // Body cuma decrement + compare, tak ada efek samping dan tak ada IRQ di
        // interpreter ini -> hasil akhirnya eksak: rt = rs, lompat keluar.
        {
            u32 w0 = eebus::read32(c.pc);
            if (op(w0) == 0x09 && rs(w0) == rt(w0) && rt(w0) != 0 && (w0 & 0xFFFFu) == 0xFFFFu) {
                bool plain = true;
                for (int i = 1; i <= 4 && plain; ++i)
                    if (eebus::read32(c.pc + i * 4) != 0) plain = false;
                if (plain) {
                    u32 w5 = eebus::read32(c.pc + 20);   // bne
                    u32 w6 = eebus::read32(c.pc + 24);   // delay slot
                    if (op(w5) == 0x05 && rs(w5) == rt(w0) && w6 == 0) {
                        u32 br_pc = c.pc + 20;                     // alamat bne
                        u32 target = br_pc + 4 + ((u32)(s32)(int16_t)(w5 & 0xFFFFu) << 2);
                        if (target == c.pc) {
                            u32 cur = c.r[rt(w0)][0];
                            u32 other = c.r[rt(w5)][0];
                            u64 n = (u32)(cur - other);            // jumlah decrement sampai == other
                            if (n == 0) n = 0x100000000ull;      // wrap penuh
                            if (n >= 4096 && n <= 200000000ull) {
                                c.r[rt(w0)][0] = other;
                                c.instret += n * 7;               // 7 instr/iterasi
                                c.pc = c.pc + 0x1c;               // setelah delay slot
                                c.next_pc = c.pc + 4;
                                g_delay_loops++;
                                g_delay_inst += n * 7;
                                continue;
                            }
                        }
                    }
                }
            }
        }

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
        // --- end-func callback: kembali ke titik resume kalau end_func sudah `jr ra` ---
        // WAJIB dievaluasi SEBELUM fetch: `inst = read32(c.pc)` dulu dijalankan lebih
        // awal, jadi sentinel (0xFFFFFFFC) sempat dibaca sebagai alamat instruksi dan
        // restore konteks tidak pernah terjadi (terukur: callback 0x398580 `jr ra`
        // jatuh ke 0x39857C, lalu dipanggil ulang dgn a0=6 -> loop salin 35 M).
        if (g_endfunc_on && c.cb_active && c.pc == CB_SENTINEL) {
            if (c.cb_depth > 0) {
                Cpu::CbCtx& s = c.cb_save[--c.cb_depth];
                std::memcpy(c.r, s.r, sizeof(s.r));
                std::memcpy(c.lo, s.lo, sizeof(s.lo));
                std::memcpy(c.hi, s.hi, sizeof(s.hi));
                std::memcpy(c.f, s.f, sizeof(s.f));
                c.sa = s.sa; c.facc = s.facc; c.fcsr = s.fcsr;
                c.pc = s.resume;
            } else c.pc = c.cb_resume;
            c.next_pc = c.pc + 4;
            c.cb_active = false;
            cur_pc = c.pc;
        }
        const u32 prev_inst = c.prev_inst;
        u32 inst = eebus::read32(c.pc);
        c.pc = c.next_pc;             // delay slot semantics: slot sudah diprefetch
        c.next_pc = c.pc + 4;
        // --- end-func callback: panggil end_func(end_param, result) seperti _request_end
        // Titik aman WAJIB: jangan ambil callback saat sedang di delay-slot branch
        // (next_pc != pc+4 = ada target lompatan tertunda). Kalau diambil di situ,
        // target `jr ra` hilang -> eksekusi jatuh ke instruksi berikutnya (terukur:
        // resume di 0x3b006c menghapus return syscall 119 -> lari ke wrapper -119
        // dengan a0=6, lalu masuk 0x398580 sebagai "deskriptor" -> tulis liar ke .text).
        // Branch = op 0x01/0x02/0x03/0x04..0x07/0x14..0x17, atau SPECIAL funct 8/9.
        // Instruksi tepat sesudah branch secara fisik = delay slot-nya.
        auto is_branch_inst = [](u32 w) -> bool {
            u32 o = w >> 26;
            if (o == 1 || (o >= 2 && o <= 7) || (o >= 0x14 && o <= 0x17)) return true;
            if (o == 0) { u32 f = w & 0x3F; return f == 8 || f == 9; }
            return false;
        };
        const bool at_safe_point = (c.next_pc == c.pc + 4) && !is_branch_inst(prev_inst);
        if (!c.cb_active && g_endfunc_on && at_safe_point) {
            u32 f, a, r;
            if (take_callback(f, a, r)) {
                c.cb_resume = c.pc;
                if (c.cb_depth < 4) {
                    Cpu::CbCtx& s = c.cb_save[c.cb_depth++];
                    std::memcpy(s.r, c.r, sizeof(s.r));
                    std::memcpy(s.lo, c.lo, sizeof(s.lo));
                    std::memcpy(s.hi, c.hi, sizeof(s.hi));
                    std::memcpy(s.f, c.f, sizeof(s.f));
                    s.sa = c.sa; s.facc = c.facc; s.fcsr = c.fcsr; s.resume = c.cb_resume;
                }
                if (std::getenv("GH_CB_DUMP")) {
                    std::fprintf(stderr,
                        "[cb] arg=0x%08x n1=%u n2=%u d1=0x%08x d2=0x%08x s1=0x%08x s2=0x%08x sp=0x%08x\n",
                        a, eebus::read32(a), eebus::read32(a + 4), eebus::read32(a + 8),
                        eebus::read32(a + 12), eebus::read32(a + 16), eebus::read32(a + 20),
                        c.r[29][0]);
                }
                c.r[31][0] = CB_SENTINEL;   // ra = penanda kembali
                c.r[4][0] = a;              // a0 = end_param
                c.r[5][0] = r;              // a1 = result
                c.pc = f; c.next_pc = f + 4;
                c.cb_active = true;
                std::fprintf(stderr, "[cb] stub 3b0060..70: %08x %08x %08x %08x %08x\n",
                    eebus::read32(0x3b0060), eebus::read32(0x3b0064), eebus::read32(0x3b0068),
                    eebus::read32(0x3b006c), eebus::read32(0x3b0070));
                std::fprintf(stderr, "[cb] end_func=0x%08x arg=0x%08x res=%d (resume=0x%08x next=0x%08x prev=0x%08x instret=%llu)\n",
                             f, a, (int)r, c.cb_resume, c.next_pc, prev_inst,
                             (unsigned long long)c.instret);
            }
        }
        c.fault_op = inst;
        c.prev_inst = inst;
        if (c.trace) c.trace[c.trace_pos = (c.trace_pos + 1) & c.trace_mask] = cur_pc;

        auto fault = [&](const char* why) { c.fault = why; c.fault_pc = c.pc; };

        static uint32_t op_trace = 0;
        if (op_trace++ < 20) {
            std::fprintf(stderr, "[OP] pc=0x%08x op=0x%02x inst=0x%08x instret=%llu\n",
                c.pc, op(inst), inst, (unsigned long long)c.instret);
        }
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
                // sentinel return-dari-callback: 0xFFFFFFFC sengaja di atas RAM supaya
                // tidak bisa disamai kode game; jangan dianggap "jr null".
                if (g_endfunc_on && c.cb_active && target == CB_SENTINEL) { c.next_pc = target; break; }
                if (target == 0 || target >= 0x02000000) {
                    // jr ke null/invalid — skip (continue ke next instruction)
                    static uint32_t jr_null = 0;
                    if (jr_null++ < 5)
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
                if (g_endfunc_on && c.cb_active && t == CB_SENTINEL) { c.next_pc = t; break; }
                if (t == 0 || t >= 0x02000000) {
                    // function pointer null/invalid — return ke caller
                    static uint32_t null_fp = 0;
                    if (null_fp++ < 5)
                        std::fprintf(stderr, "[JALR NULL] pc=0x%08x target=0x%08x -> return 0x%08x\n", c.pc, t, ret);
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
                g_syscall_pc = c.pc;
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
        // ---------- VU0 (COP2) — LQC2/SQC2 ----------
        case 0x36: {  // LQC2 — Load Quadword COP2 (VU0) — addr 16-aligned (verified PCSX2)
            u32 a = (c.r[rs(inst)][0] + sx16(imm(inst))) & ~0xFu;
            u32 vf = rt(inst);
            for (int i = 0; i < 4; i++) c.vf[vf][i] = eebus::read32(a + i * 4);
            c.vu_lqc2++;
            break;
        }
        case 0x3E: {  // SQC2 — Store Quadword COP2 (VU0) — addr 16-aligned
            u32 a = (c.r[rs(inst)][0] + sx16(imm(inst))) & ~0xFu;
            u32 vf = rt(inst);
            for (int i = 0; i < 4; i++) eebus::write32(a + i * 4, c.vf[vf][i]);
            c.vu_sqc2++;
            break;
        }
        case 0x12: {  // COP2 — VU0 micro/macro (decode tabel — Phase 7b.8)
            // Field (MIPS pos, verified PCSX2 microVU): fd=dest (inst[10:6]),
            // fs=src1 (inst[15:11]), ft=src2/bcast (inst[20:16]), func6=inst[5:0].
            // Dispatch: rs(inst)>=0x10 -> micro. func6 0x00-0x3B = SPECIAL1 (64 tbl);
            // func6 0x3C-0x3F = SPECIAL2 (idx2 = (inst&3)|((inst>>4)&0x7C), 128 tbl).
            u32 rsf = rs(inst);
            auto vfl = [&](u32 reg, int lane) -> float {   // baca VF (VF0 = 0 di hw)
                if (reg == 0) return 0.f;
                float v; std::memcpy(&v, &c.vf[reg][lane & 3], 4); return v;
            };
            auto vfs = [&](u32 reg, int lane, float v) {   // tulis VF (VF0 no-op)
                if (reg == 0) return;
                std::memcpy(&c.vf[reg][lane & 3], &v, 4);
            };
            auto accget = [&](int lane) -> float { float v; std::memcpy(&v, &c.vacc[lane & 3], 4); return v; };
            auto accset = [&](int lane, float v) { std::memcpy(&c.vacc[lane & 3], &v, 4); };
            auto vfl_bits = [&](u32 reg, int lane) -> u32 { return reg == 0 ? 0u : c.vf[reg][lane & 3]; };
            auto vfs_bits = [&](u32 reg, int lane, u32 b) { if (reg) c.vf[reg][lane & 3] = b; };
            auto vig = [&](u32 reg) -> u32 { return reg == 0 ? 0u : c.vi[reg]; };
            auto vis = [&](u32 reg, u32 v) { if (reg) c.vi[reg] = v; };
            // I register = vi[fd] dipakai broadcast utk varian ".i"; Q utk ".q".
            auto bcast_ft = [&](u32 ft, u32 fs_, int comp) -> float { return vfl(ft, comp); };
            // sinkron I-reg snapshot dari vi[fd] utk varian .i (I = vi[fd] dlm VU)
            c.v_i_reg = (float)(s32)vig(fd(inst));

            if (rsf < 0x10) {
                // macro moves: QMFC2/CFC2/MTC2/CTC2/BC2
                if (rsf == 0x01) { w32(c, rt(inst), c.vf[rd(inst)][0]); }        // QMFC2 (low32)
                else if (rsf == 0x02) { w32(c, rt(inst), c.vcctl[rd(inst) & 15]); } // CFC2
                else if (rsf == 0x04) { for (int i=0;i<4;i++) c.vf[rd(inst)][i]=0;  // MTC2 (write low32, upper cleared)
                    std::memcpy(&c.vf[rd(inst)][0], &c.r[rt(inst)][0], 4); }
                else if (rsf == 0x05) { for (int i=0;i<4;i++) c.vf[rd(inst)][i]=c.r[rt(inst)][i]; } // QMTC2 (128b)
                else if (rsf == 0x06) { c.vcctl[rd(inst) & 15] = c.r[rt(inst)][0]; } // CTC2
                else if (rsf == 0x08) { // BC2F/BC2T (cond flag di vcctl[1])
                    u32 tf = rt(inst); bool flag = c.vcctl[1] != 0; bool take = (tf == 1) ? flag : !flag;
                    if (take) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                    else { c.pc += 4; c.next_pc = c.pc + 4; } // likely-ish skip
                }
                c.vu_macro_hits++;
                break;
            }

            u32 fd_ = fd(inst), fs_ = fs(inst), ft_ = ft(inst);
            u32 func6 = inst & 0x3F;
            c.vu_macro_hits++;

            if (func6 < 0x3C) {  // ---- SPECIAL1 (64-entry table) ----
                c.vu_spec1_hits[func6]++;
                u32 comp = func6 & 3;              // x/y/z/w selector utk varian broadcast
                bool bc = (func6 < 0x28);          // varian x/y/z/w + q/i pakai broadcast
                // Kelompok fungsi utk 4-varian (x,y,z,w) di 0x00-0x1B:
                switch (func6 >> 2) {
                case 0x00: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); vfs(fd_,l, vfl(fs_,l)+b);} break; // VADDx
                case 0x01: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); vfs(fd_,l, vfl(fs_,l)-b);} break; // VSUBx
                case 0x02: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); float t=vfl(fs_,l)*b; accset(l, accget(l)+t); vfs(fd_,l, accget(l));} break; // VMADDx
                case 0x03: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); float t=vfl(fs_,l)*b; accset(l, accget(l)-t); vfs(fd_,l, accget(l));} break; // VMSUBx
                case 0x04: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); vfs(fd_,l, std::fmax(vfl(fs_,l),b));} break; // VMAXx
                case 0x05: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); vfs(fd_,l, std::fmin(vfl(fs_,l),b));} break; // VMINIx
                case 0x06: for (int l=0;l<4;l++){ float b = bc? bcast_ft(ft_,fs_,comp):vfl(ft_,l); float t=vfl(fs_,l)*b; accset(l,t); vfs(fd_,l,t);} break; // VMULx
                default: break;
                }
                // Spesial 0x1C-0x2F (non 4-varian)
                if (func6 == 0x1C) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,t); vfs(fd_,l,t);} }        // VMULq
                else if (func6 == 0x1D) { for (int l=0;l<4;l++) vfs(fd_,l, std::fmax(vfl(fs_,l), c.v_i_reg)); }          // VMAXi
                else if (func6 == 0x1E) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,t); vfs(fd_,l,t);} } // VMULi
                else if (func6 == 0x1F) { for (int l=0;l<4;l++) vfs(fd_,l, std::fmin(vfl(fs_,l), c.v_i_reg)); }          // VMINIi
                else if (func6 == 0x20) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)+c.vq); }                            // VADDq
                else if (func6 == 0x21) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,accget(l)+t); vfs(fd_,l,accget(l));} } // VMADDq
                else if (func6 == 0x22) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)+c.v_i_reg); }                       // VADDi
                else if (func6 == 0x23) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,accget(l)+t); vfs(fd_,l,accget(l));} } // VMADDi
                else if (func6 == 0x24) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)-c.vq); }                            // VSUBq
                else if (func6 == 0x25) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,accget(l)-t); vfs(fd_,l,accget(l));} } // VMSUBq
                else if (func6 == 0x26) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)-c.v_i_reg); }                       // VSUBi
                else if (func6 == 0x27) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,accget(l)-t); vfs(fd_,l,accget(l));} } // VMSUBi
                else if (func6 == 0x28) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)+vfl(ft_,l)); }                       // VADD
                else if (func6 == 0x29) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,accget(l)+t); vfs(fd_,l,accget(l));} } // VMADD
                else if (func6 == 0x2A) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,t); vfs(fd_,l,t);} } // VMUL
                else if (func6 == 0x2B) { for (int l=0;l<4;l++) vfs(fd_,l, std::fmax(vfl(fs_,l),vfl(ft_,l))); }           // VMAX
                else if (func6 == 0x2C) { for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)-vfl(ft_,l)); }                      // VSUB
                else if (func6 == 0x2D) { for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,accget(l)-t); vfs(fd_,l,accget(l));} } // VMSUB
                else if (func6 == 0x2E) { // VOPMSUB (outer-product sub; pakai pola ACC lane)
                    float ax=vfl(fs_,1)*vfl(ft_,2)-vfl(fs_,2)*vfl(ft_,1);
                    float ay=vfl(fs_,2)*vfl(ft_,0)-vfl(fs_,0)*vfl(ft_,2);
                    float az=vfl(fs_,0)*vfl(ft_,1)-vfl(fs_,1)*vfl(ft_,0);
                    accset(0,accget(0)-ax); accset(1,accget(1)-ay); accset(2,accget(2)-az);
                    vfs(fd_,0,accget(0)); vfs(fd_,1,accget(1)); vfs(fd_,2,accget(2)); vfs(fd_,3,accget(3));
                }
                else if (func6 == 0x2F) { for (int l=0;l<4;l++) vfs(fd_,l, std::fmin(vfl(fs_,l),vfl(ft_,l))); }          // VMINI
                else if (func6 == 0x30) { vis(fd_, vig(fs_) + vig(ft_)); }                                                // VIADD
                else if (func6 == 0x31) { vis(fd_, vig(fs_) - vig(ft_)); }                                                // VISUB
                else if (func6 == 0x32) { vis(fd_, vig(fs_) + (fd(inst) /*imm5 dlm fd pos*/ & 0x1F)); }                    // VIADDI
                else if (func6 == 0x34) { vis(fd_, vig(fs_) & vig(ft_)); }                                                // VIAND
                else if (func6 == 0x35) { vis(fd_, vig(fs_) | vig(ft_)); }                                                // VIOR
                else if (func6 == 0x38 || func6 == 0x39) { /* VCALLMS/VCALLMSR — microprogram call; census saja */ }
                // func6 0x3C-0x3F tidak masuk sini
            } else {
                // ---- SPECIAL2 (128-entry table) ----
                u32 idx2 = (inst & 3) | ((inst >> 4) & 0x7C);
                c.vu_spec2_hits[idx2]++;
                switch (idx2) {
                case 0x00: case 0x01: case 0x02: case 0x03: // VADDAx: ACC = fs + bcast(ft,comp)
                    for (int l=0;l<4;l++) accset(l, vfl(fs_,l)+vfl(ft_,idx2&3)); break;
                case 0x04: case 0x05: case 0x06: case 0x07: // VSUBAx
                    for (int l=0;l<4;l++) accset(l, vfl(fs_,l)-vfl(ft_,idx2&3)); break;
                case 0x08: case 0x09: case 0x0A: case 0x0B: // VMADDAx: ACC += fs*bcast
                    for (int l=0;l<4;l++) accset(l, accget(l)+vfl(fs_,l)*vfl(ft_,idx2&3)); break;
                case 0x0C: case 0x0D: case 0x0E: case 0x0F: // VMSUBAx
                    for (int l=0;l<4;l++) accset(l, accget(l)-vfl(fs_,l)*vfl(ft_,idx2&3)); break;
                case 0x10: case 0x11: case 0x12: case 0x13: { // VITOF0/4/12/15
                    static const int sh[4]={0,4,12,15}; int s=sh[idx2&3];
                    for (int l=0;l<4;l++) vfs(fd_,l, (float)((s32)vfl_bits(fs_,l) >> 0) / (float)(1<<s));
                    break; }
                case 0x14: case 0x15: case 0x16: case 0x17: { // VFTOI0/4/12/15
                    static const int sh[4]={0,4,12,15}; int s=sh[idx2&3];
                    for (int l=0;l<4;l++){ s32 q=(s32)(vfl(fs_,l)*(float)(1<<s)); u32 b=(u32)q; vfs_bits(fd_,l,b);} break; }
                case 0x18: case 0x19: case 0x1A: case 0x1B: // VMULAx
                    for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,idx2&3); accset(l,t);} break;
                case 0x1C: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,t);} break;   // VMULAq
                case 0x1D: for (int l=0;l<4;l++) vfs(fd_,l, std::fabs(vfl(fs_,l))); break;        // VABS
                case 0x1E: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,t);} break; // VMULAi
                case 0x1F: break;                                                                   // VCLIP (flag; census)
                case 0x20: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)+c.vq); break;                // VADDAq
                case 0x21: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,accget(l)+t);} break; // VMADDAq
                case 0x22: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)+c.v_i_reg); break;           // VADDAi
                case 0x23: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,accget(l)+t);} break; // VMADDAi
                case 0x24: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)-c.vq); break;                // VSUBAq
                case 0x25: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.vq; accset(l,accget(l)-t);} break; // VMSUBAq
                case 0x26: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)-c.v_i_reg); break;           // VSUBAi
                case 0x27: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*c.v_i_reg; accset(l,accget(l)-t);} break; // VMSUBAi
                case 0x28: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)+vfl(ft_,l)); break;           // VADDA
                case 0x29: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,accget(l)+t);} break; // VMADDA
                case 0x2A: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,t);} break; // VMULA
                case 0x2C: for (int l=0;l<4;l++) accset(l, vfl(fs_,l)-vfl(ft_,l)); break;           // VSUBA
                case 0x2D: for (int l=0;l<4;l++){ float t=vfl(fs_,l)*vfl(ft_,l); accset(l,accget(l)-t);} break; // VMSUBA
                case 0x2E: { // VOPMULA (outer product set ACC)
                    accset(0, vfl(fs_,1)*vfl(ft_,2)); accset(1, vfl(fs_,2)*vfl(ft_,0)); accset(2, vfl(fs_,0)*vfl(ft_,1));
                    break; }
                case 0x2F: break;                                                                   // VNOP
                case 0x30: for (int l=0;l<4;l++) vfs(fd_,l, vfl(fs_,l)); break;                     // VMOVE
                case 0x31: { // VMR32: rotate components right (x<-w,y<-x,z<-y,w<-z)
                    float x=vfl(fs_,3), y=vfl(fs_,0), z=vfl(fs_,1), w=vfl(fs_,2);
                    vfs(fd_,0,x); vfs(fd_,1,y); vfs(fd_,2,z); vfs(fd_,3,w); break; }
                case 0x34: { // VLQI: load quad indexed, base vi[fs], +I disp; inc vi[fs] by 16
                    u32 addr = vig(fs_) + (u32)(s32)c.v_i_reg; addr &= ~0xFu;
                    for (int l=0;l<4;l++) vfs(fd_,l, /*bits*/ (float)0); // placeholder replaced below
                    // proper: copy raw bits
                    for (int l=0;l<4;l++){ u32 b=eebus::read32(addr+l*4); vfs_bits(fd_,l,b); }
                    vis(fs_, vig(fs_) + 16); break; }
                case 0x35: { // VSQI: store quad indexed, inc vi[fs]
                    u32 addr = vig(fs_) + (u32)(s32)c.v_i_reg; addr &= ~0xFu;
                    for (int l=0;l<4;l++) eebus::write32(addr+l*4, c.vf[fs_][l]);
                    vis(fs_, vig(fs_) + 16); break; }
                case 0x38: c.vq = vfl(fs_,0) / vfl(ft_,0); break;                                    // VDIV (Q)
                case 0x39: c.vq = std::sqrt(std::fabs(vfl(fs_,0))); break;                            // VSQRT
                case 0x3A: c.vq = vfl(fs_,0) / std::sqrt(std::fabs(vfl(ft_,0))); break;               // VRSQRT
                case 0x3B: break;                                                                   // VWAITQ (nop)
                case 0x3C: vis(ft_, (u32)(s32)vfl(fs_,0)); break;                                    // VMTIR (vi[ft]=fs.x int)
                case 0x3D: for (int l=0;l<4;l++) vfs(fd_,l, (float)(s32)vig(fs_)); break;            // VMFIR (vf=vi int->flt)
                case 0x3E: vis(fd_, c.vf[fs_][0]); break;                                            // VILWR (vi[fd]=fs.x bits)
                case 0x3F: c.vf[fs_][0] = vig(fd_); break;                                           // VISWR (fs.x=vi[fd])
                case 0x40: case 0x41: case 0x42: case 0x43: break;                                   // VR* (R-reg; census)
                default: c.vu_unk++; break;                                                          // belum diimplement
                }
            }
            break;
        }
        // ---------- COP1 (FPU) — top-level ----------
        case 0x11: {
            u32 rs_f = (inst >> 21) & 0x1F;
            switch (rs_f) {
            case 0x00: w32(c, rt(inst), c.f[rd(inst)]); break;  // mfc1
            case 0x04: c.f[rd(inst)] = c.r[rt(inst)][0]; break;  // mtc1
            case 0x02: w32(c, rt(inst), 0); break;  // cfc1
            case 0x06: break;  // ctc1
            default: break;
            }
            break;
        }
        // ---------- REGIMM ----------
        case 0x01: {
            s32 v = (s32)c.r[rs(inst)][0];
            u32 rti = rt(inst);
            bool take;
            bool likely = false;   // branch-LIKELY: tak diambil → delay slot DISIPAN
            switch (rti) {
            case 0x00: take = v < 0; break;   // bltz
            case 0x01: take = v >= 0; break;  // bgez
            case 0x02: take = v < 0; likely = true; break;   // bltzl (static census .text: 127 situs)
            case 0x03: {                     // bgezl (FAULT pertama 0x3b6ae4 sesi cron #8; 152 situs)
                take = v >= 0; likely = true;
                static uint32_t bgezl_c = 0;
                if (bgezl_c++ < 3)
                    std::fprintf(stderr, "[BGEZL] pc=0x%08x v=0x%08x take=%d instret=%llu\n",
                        cur_pc, (u32)v, (int)take, (unsigned long long)c.instret);
                break;
            }
            case 0x10: take = v < 0; w32(c, 31, c.next_pc); break;   // bltzal
            case 0x11: take = v >= 0; w32(c, 31, c.next_pc); break;  // bgezal
            case 0x12: take = v < 0; w32(c, 31, c.next_pc); likely = true; break;  // bltzall
            case 0x13: take = v >= 0; w32(c, 31, c.next_pc); likely = true; break; // bgezall
            case 0x18: take = v < 0; break;   // (label lama "BLTZL" salah posisi — posisi asli 0x02)
            case 0x19: take = v >= 0; break;  // (label lama "BGEZL" — posisi asli 0x03)
            case 0x1A: take = v <= 0; break;  // tak ada di tabel EE — fallback defensif
            case 0x1B: take = v > 0; break;   // (jarang; kalau kena akan muncul di census)
            case 0x30: c.sa = (c.r[rs(inst)][0] & 0xF) << 1; take = false; break;  // MTSAB
            case 0x31: c.sa = (c.r[rs(inst)][0] & 0x7) << 2; take = false; break;  // MTSAH
            case 0x1C: {  // rt=0x1C tak ada di tabel EE REGIMM — dulu paste MMI salah tempat
                          // (bikin `take` uninitialized = UB). Deterministik: tak branch + census.
                static uint32_t rimm1c = 0;
                if (rimm1c++ < 5)
                    std::fprintf(stderr, "[REGIMM 1C] pc=0x%08x inst=0x%08x instret=%llu\n",
                        cur_pc, inst, (unsigned long long)c.instret);
                take = false; break;
            }
            default: fault("REGIMM unknown"); return true;
                }
            if (take) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
            else if (likely) { c.pc += 4; c.next_pc = c.pc + 4; }  // likely not-taken → skip delay slot
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
        case 0x03: {
            u32 target = ((cur_pc + 4) & 0xF0000000) | tgt(inst);
            static uint32_t jal_count = 0; if (jal_count++ < 15) {
                std::fprintf(stderr, "[JAL KEY] pc=0x%08x -> 0x%08x instret=%llu\n",
                    cur_pc, target, (unsigned long long)c.instret);
            }
            w32(c, 31, cur_pc + 8); c.next_pc = target; break;
        }
        // ---------- BRANCH ----------
        case 0x04: {
            u32 target = c.pc + (sx16(imm(inst)) << 2);  // FIX: tanpa +4 (verified vs objdump)
            // trace heap-clear loop crt0 (v0 pointer, v1 end — disasm @0x100178): bounded
            if (c.pc >= 0x100178 && c.pc <= 0x100194) {
                static uint32_t clear_cnt = 0;
                if (clear_cnt++ < 3)
                    std::fprintf(stderr, "[CLEAR] pc=0x%08x v0=0x%08x v1=0x%08x instret=%llu\n",
                        c.pc, c.r[2][0], c.r[3][0], (unsigned long long)c.instret);
            }
            // detektor RE-ENTRY sq-loop (0x100158) pasca-eret — seharusnya cuma 1x dari crt0
            if (c.pc == 0x10015c && c.instret > 1'000'000ull) {
                static uint32_t sqre = 0;
                if (sqre++ < 3)
                    std::fprintf(stderr, "[SQ-REENTER] pc=0x%08x v0=0x%08x a0=0x%08x ra=0x%08x sp=0x%08x instret=%llu\n",
                        c.pc, c.r[2][0], c.r[4][0], c.r[31][0], c.r[29][0], (unsigned long long)c.instret);
            }

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
                   else { c.pc += 4; c.next_pc = c.pc + 4; } break;                    // beql (likely: skip delay)
        case 0x15: if (c.r[rs(inst)][0] != c.r[rt(inst)][0]) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else { c.pc += 4; c.next_pc = c.pc + 4; } break;                    // bnel
        case 0x16: if ((s32)c.r[rs(inst)][0] <= 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else { c.pc += 4; c.next_pc = c.pc + 4; } break;                    // blezl
        case 0x17: if ((s32)c.r[rs(inst)][0] > 0) c.next_pc = c.pc + (sx16(imm(inst)) << 2);
                   else { c.pc += 4; c.next_pc = c.pc + 4; } break;                    // bgtzl
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
        case 0x27: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst));                                // lwu (FAULT 0x3b713c; zero-extend 64)
                     w64(c, rt(inst), (u64)eebus::read32(a)); break; }
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
        // ---------- FLOAT MEM + UNALIGNED 64 (census statik: swc1 23303 · lwc1 19602 situs) ----
        case 0x31: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst));                        // lwc1
                     c.f[rt(inst)] = eebus::read32(a); break; }
        case 0x39: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst));                        // swc1
                     eebus::write32(a, c.f[rt(inst)]); break; }
        case 0x35: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)), r = rt(inst);          // ldc1 pair
                     c.f[r] = eebus::read32(a); c.f[(r + 1) & 31] = eebus::read32(a + 4); break; }
        case 0x3D: { u32 a = c.r[rs(inst)][0] + sx16(imm(inst)), r = rt(inst);          // sdc1 pair
                     eebus::write32(a, c.f[r]); eebus::write32(a + 4, c.f[(r + 1) & 31]); break; }
        case 0x2C: do_sdl(c.r[rs(inst)][0] + sx16(imm(inst)), g64(c, rt(inst))); break;  // sdl (FIX: op=0x2C bukan 0x2A)
        case 0x2D: do_sdr(c.r[rs(inst)][0] + sx16(imm(inst)), g64(c, rt(inst))); break;  // sdr
        case 0x2A: do_swl(c.r[rs(inst)][0] + sx16(imm(inst)), c.r[rt(inst)][0]); break;  // swl
        case 0x2E: do_swr(c.r[rs(inst)][0] + sx16(imm(inst)), c.r[rt(inst)][0]); break;  // swr
        case 0x2F: break;   // CACHE — native: tak ada cache koheren perlu disentuh
        case 0x33: break;   // PREF — prefetch: no-op native
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
            else if (crs == 0x10) {                            // CO ops (tlb/eret)
                u32 f = fn(inst);
                if (f == 0x38) {                               // EI (Enable Interrupt)
                    c.cop0[12] |= 1;  // set IE bit in Status
                    break;
                }
                if (f == 0x39) {                               // DI (Disable Interrupt)
                    c.cop0[12] &= ~1u;  // clear IE bit
                    break;
                }
                if (f == 0x18) {                               // ERET: pc = ErrorEPC (cop0 r31)
                    static uint32_t eret_n = 0;
                    if (eret_n++ < 5)
                        std::fprintf(stderr, "[ERET] pc=0x%08x ra=0x%08x errpc30=0x%08x desave31=0x%08x sr=0x%08x instret=%llu\n",
                            cur_pc, c.r[31][0], c.cop0[30], c.cop0[31], c.cop0[12],
                            (unsigned long long)c.instret);
                    c.pc = c.cop0[30] & ~3u; c.next_pc = c.pc + 4;  // ErrorEPC = cop0 r30 (R4000; r31=DESAVE)
                    c.cop0[12] &= ~2u;                         // clear SR.EXL
                } else {                                       // TLB ops: flat map = no-op
                    static uint32_t tlbop = 0;
                    if (tlbop++ < 5)
                        std::fprintf(stderr, "[COP0 CO fn=0x%02x] pc=0x%08x instret=%llu\n",
                            f, cur_pc, (unsigned long long)c.instret);
                }
                break;
            }
            else { fault("COP0 rs unknown"); return true; }
            break;
        }
        
        case 0x1C: {  // MMI (R5900 multimedia) — decode + eksekusi penuh di ee_mmi.h
            // 2026-10-24: blok ini dulu stub ("tulis rs ke rd") -> SELURUH op MMI salah.
            // .text game punya 2256 situs MMI statis: pcpyld 426, paddsh 384, pmaxh 272,
            // pminh 272, psrah 260, ppacb 136, ... Bukti kerusakan: memset libkernel
            // (0x3a52f0, pcpyh+pcpyld) menulis 0x01010101 ke tabel SJUNI 0x3e7ff0 ->
            // semua 64 slot "sibuk" -> SJUNI_Create=0 -> hang fatal 0x337180.
            if (!mmi::exec(c, inst)) { fault("MMI unknown"); return true; }
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
        // FIX 2026-10-19 (bug "v0 cycling"): injeksi PERIODIK ini dulu juga nulis hasil ke
        // r[2] (v0) — nolak GPR di tengah jalan. v0 = pointer heap-clear crt0 (@0x100180,
        // nambah +1 per byte) di-reset ke 0 tiap 60000 inst → loop clear tak pernah sampai
        // v1 → infinite loop (bukti: 55,8 M kali iterasi tanpa keluar). Sekarang: panggil
        // handler untuk side-effect saja; return-value syscall cuma ditulis jalur SYSCALL
        // instruksi (case 0x0C) yang memang menaruh hasil di v0 sesuai konvensi EE.
        if ((c.instret % 60000ull) == 0) {
            u32 v0 = 0;
            handler(115, 0, 0, 0, 0, v0);
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
        // trace: PC masuk data area
        static bool in_data = false;
        if (!in_data && c.next_pc >= 0x003c0000 && c.next_pc < 0x003d0000) {
            in_data = true;
            std::fprintf(stderr, "[DATA_ENTRY] pc=0x%08x next=0x%08x instret=%llu\n",
                c.pc, c.next_pc, (unsigned long long)c.instret);
        }
        // .data section (0x003BBF80-0x0041B580) = executable di PS2 (WAX flag)
        // .rodata/.bss = NOT executable
        if (c.next_pc >= 0x0041B580 && c.next_pc < 0x00460E00) {
            std::fprintf(stderr, "[RODATA_STOP] pc=0x%08x next=0x%08x instret=%llu\n",
                c.pc, c.next_pc, (unsigned long long)c.instret);
            fault("rodata");
            return true;
        }
        // CB_SENTINEL (0xFFFFFFFC) sengaja di atas RAM: itu alamat "kembali dari
        // end_func" (lihat jr/jalr). Jangan dianggap pc_oor — kalau kena, boot mati
        // tepat saat callback RPC pertama (terukur: fault pc_oor @0x398580, op jr ra).
        // Dua bentuk: pc == sentinel (delay-slot baru selesai, restore terjadi di
        // puncak loop berikutnya) atau next_pc == sentinel (jr ke penanda, delay slot
        // belum jalan). Keduanya sah selama callback aktif.
        const bool cb_ret = g_endfunc_on && c.cb_active &&
                            (c.pc == CB_SENTINEL || c.next_pc == CB_SENTINEL);
        if (!cb_ret && (c.pc >= 0x02000000 || c.next_pc >= 0x02000000)) {
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
