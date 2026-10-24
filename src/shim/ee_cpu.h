// ee_cpu.h — interpreter subset MIPS R5900 (little-endian, EE) buat Phase 7b
// Menjalankan crt0 asli God Hand: entry 0x100008 -> SetupThread/SetupHeap (syscall
// di reg v1) -> init libkernel -> ... Subset instruksi diturunkan dari disasm +
// fault-driven iteration (opcode tak dikenal -> ditambah, bukan di-skip).
// GPR 128-bit (4 lane x u32) + HI/LO 128-bit persis R5900 — kritis utk LQ/SQ
// (encoding: LQ = op 0x1E, SQ = op 0x1F, imm16 penuh, 16-aligned — BUKTID dari
// pasangan prolog/epilog stack game).
#pragma once
#include "ee_bus.h"
#include <cstdint>

namespace eecpu {

using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s32=int32_t; using s64=int64_t;

// Callback syscall: nomor di reg v1 (konvensi EE — terverifikasi syscall_contract.csv).
// a0..a3 = argumen. return value di v0. false = tak dikenal -> hentikan CPU (trace).
using SyscallFn = bool (*)(u32 num, u32 a0, u32 a1, u32 a2, u32 a3, u32& v0);

struct Cpu {
    // GPR 128-bit per lane: lane0 = low 32 (alamat/arith 32-bit game); lane1 = bits
    // 32..63 (SD/LD/64-bit MMI); lane2/3 = bits 64..127 (LQ/SQ penuh — memcpy 16B).
    u32 r[32][4]{};
    u32 pc = 0, next_pc = 0;
    u32 lo[4]{}, hi[4]{};     // HI/LO 128-bit per ELEMEN u32 (UL[0..3]; persis layout PCSX2:
                              // lo[0..1] = pipe0 low64, lo[2..3] = pipe1 high64 — MMI mult pipeline)
    u32 sa = 0;              // Save Address (MTSA/MFSA/MTSAB — 10 bit di EE)
    u32 f[32]{};             // FPR 32-bit single (EE FPU = single only)
    u32 facc = 0;            // FPU accumulator (ADDA/MULA/MADD.S family)
    u32 fcsr = 0;
    // VU0 (7b.8): VF[32] 128-bit (4 lane u32) + accumulator 128-bit + status.
    // VF0 di-hw = zero register baca (tulis = no-op di mode macro; di sini: tulis
    // dijaga tetap boleh utk kesederhanaan — game tidak menulis vf0 utk data).
    u32 vf[32][4]{};         // vector registers (LE: [0]=bits0..31)
    u32 vacc[4]{};           // MAC accumulator 128-bit (untuk MULA/MSUB family)
    u32 vcctl[16]{};         // control regs: 0=status 1=macflag 2=clipflag 3=R 4=I
    // --- VU0 register file penuh (7b.8) — decode SPECIAL1/SPECIAL2 9-bit ---
    u32 vi[32]{};            // VI0..VI31 integer regs (VI0 = 0 on read)
    float vq = 0.f;          // Q register (DIV/SQRT result, broadcast src)
    float v_i_reg = 0.f;     // I register snapshot (diisi dari vi[fd] per op Vxxi)
    u32 vtop = 0;            // TOP register (VLQD/VSQD decrement mode)
    u64 vu_macro_hits = 0;   // census: instruksi VU0 macro dieksekusi
    u64 vu_lqc2 = 0, vu_sqc2 = 0;  // census load/store VF
    u64 vu_spec1_hits[64]{};        // census per SPECIAL1 func (idx = func&0x3F)
    u64 vu_spec2_hits[128]{};       // census per SPECIAL2 (idx = (i&3)|((i>>4)&0x7C))
    u64 vu_unk = 0;                 // census instruksi VU0 tak dikenal
    u32 cop0[32]{};  // COP0: Status/Cause/EPC/Count/... (MFC0/MTC0)
    u64 instret = 0;         // instruksi dieksekusi (bukti)
    u32 fault_pc = 0, fault_op = 0;   // diagnosis crash (fault_pc = ALAMAT SETELAH
                                      // instruksi faulting — konvensi lama dipertahankan)
    const char* fault = nullptr;      // nullptr = sehat
    // visibilitas jujur: MMI/SPECIAL3 fn census (untuk lapor mana yang belum kena)
    u64 mmi_fn_hits[64]{};            // op 0x1C low-6-bit hit count
    u64 mmi_sub_hits[4][32]{};        // per-subclass (MMI0/2/1/3 -> idx 0..3) hit count
    u64 sp3_fn_hits[64]{};            // op 0x1E (LQ) / 0x1F (SQ) & fn lain census
    // sampler hot-PC (opsional; crt0_test aktifkan) — bukti empiris "runtime
    // menghabiskan waktu di mana": bucket = pc>>2 (per instruksi), perioda = sample_mask+1.
    u32* hist = nullptr;              // nullptr = sampler mati
    u32  sample_mask = 0;             // contoh: 0xFFFF -> sampling tiap 65536 instr
    // backward-taken-branch census: hitung SETIAP kali branch/jump mundur dieksekusi
    // (tanpa sampling — nol aliasing). Bucket = alamat instruksi branch (pc-4)>>2.
    u64* btb = nullptr;               // nullptr = BTB census mati; 2^21 bucket u64
    // trace ring PC (opsional): catat pc tiap instruksi ke ring buffer 2^k entry
    // (dipakai crt0_test utk diagnosis spin loop — dump N PC terakhir pasca-run).
    u32* trace = nullptr;             // nullptr = trace mati
    u32  trace_mask = 0;              // size-1; indeks = instret & mask
    u32  trace_pos = 0;               // posisi tulis berikutnya (wraps)
    // --- diagnosis Phase 8/9: halt + execution window trace -------------------
    // halt_pc != 0 -> run() berhenti TEPAT sebelum eksekusi instruksi di alamat itu
    // (dipakai probe: lihat register hasil satu call tanpa mengubah semantik).
    u32  halt_pc = 0;
    u32  prev_inst = 0;      // opcode instruksi terakhir yg dieksekusi (deteksi delay-slot)
    u32  halt_skip = 0;      // lewati N hit pertama (utk membedakan panggilan ke-2 dst)
    u32  halt_skipped = 0;
    u64  halt_hits = 0;
    // --- SIF RPC end-function callback (Phase 9) -------------------------------
    // Di HW asli _request_end() (interrupt SIF0) memanggil end_func(end_param, result)
    // di konteks EE setelah transfer selesai. Tanpa ini klien async (mis. libcdvd
    // 0x3998A0 -> end_func 0x3A8580) menunggu flag-nya selamanya: terukur spin di
    // 0x398DD8 karena [0x3F8674] tak pernah dinolkan.
    u32  cb_func = 0, cb_arg = 0, cb_res = 0, cb_resume = 0;
    bool cb_active = false;
    // Konteks yang diselamatkan saat end_func dipanggil (interrupt SIF0 di HW asli
    // menyimpan seluruh register file thread; tanpa ini register pemanggil rusak —
    // terukur: sp jadi 0x023F86F0 = di luar RAM 32 MB).
    struct CbCtx { u32 r[32][4]; u32 lo[4], hi[4], sa, facc, fcsr, f[32]; u32 resume; };
    CbCtx cb_save[4];
    int   cb_depth = 0;
    // trace_lo < pc < trace_hi -> cetak tiap instruksi di jendela itu ke stderr
    // (pc, word, GPR kunci). Nol efek pada state; hanya observasi.
    u32  trace_lo = 0, trace_hi = 0;
    u64  trace_win_hits = 0, trace_win_cap = 4000;

    void reset(u32 entry);
    u32  get(u32 i) const { return r[i][0]; }
    void set(u32 i, u32 v) { if (i) { r[i][0] = v; r[i][1] = r[i][2] = r[i][3] = 0; } }
    void set_sp(u32 sp) { if (sp) { r[29][0] = sp; r[29][1] = r[29][2] = r[29][3] = 0; } }
    void set_gp(u32 gp) { if (gp) { r[28][0] = gp; r[28][1] = r[28][2] = r[28][3] = 0; } }
};

// jalankan sampai stop (syscall tak dikenal / fault / PC di luar RAM / instret cap).
// handler dipanggil tiap syscall. return true = berhenti (lihat cpu.fault / instret).
// Phase 7b.9: census busy-delay loop yang di-fast-forward (idiom
// addiu rt,rt,-1 / 4x nop / bne rt,rs,self). Semantik tetap eksak.
inline u32 g_syscall_pc = 0;   // pc instruksi SYSCALL terakhir (diagnosa provenance)
// antrean callback end-func (maks 8; dipakai sif_dma saat RPC non-block selesai)
inline constexpr u32 CB_SENTINEL = 0xFFFFFFFCu;
void request_callback(u32 func, u32 arg, u32 result);
u32  pending_callbacks();
extern u64 g_delay_loops;
extern u64 g_delay_inst;

bool run(Cpu& cpu, SyscallFn handler, u64 max_inst);

} // namespace eecpu
