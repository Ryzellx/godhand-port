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

    void reset(u32 entry);
    u32  get(u32 i) const { return r[i][0]; }
    void set(u32 i, u32 v) { if (i) { r[i][0] = v; r[i][1] = r[i][2] = r[i][3] = 0; } }
    void set_sp(u32 sp) { if (sp) { r[29][0] = sp; r[29][1] = r[29][2] = r[29][3] = 0; } }
    void set_gp(u32 gp) { if (gp) { r[28][0] = gp; r[28][1] = r[28][2] = r[28][3] = 0; } }
};

// jalankan sampai stop (syscall tak dikenal / fault / PC di luar RAM / instret cap).
// handler dipanggil tiap syscall. return true = berhenti (lihat cpu.fault / instret).
bool run(Cpu& cpu, SyscallFn handler, u64 max_inst);

} // namespace eecpu
