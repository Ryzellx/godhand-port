// ee_cpu.h — interpreter subset MIPS R5900 (little-endian, EE) buat Phase 7b
// Menjalankan crt0 asli God Hand: entry 0x100008 -> SetupThread/SetupHeap (syscall
// di reg v1) -> heap. Subset instruksi diturunkan dari disasm crt0 (0x100008-0x100250).
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
    u32 r[32]{};        // GPR (r0 selalu 0)
    u32 pc = 0, next_pc = 0;
    u64 lo = 0, hi = 0; // LO/HI buat mult/div
    u32 f[32]{};        // FPR 32-bit (cukup buat crt0; FPU penuh menyusul)
    u32 fcsr = 0;
    u64 instret = 0;    // instruksi dieksekusi (bukti)
    u32 fault_pc = 0, fault_op = 0;   // diagnosis crash
    const char* fault = nullptr;      // nullptr = sehat

    void reset(u32 entry);
    void set_sp(u32 sp) { r[29] = sp; }
    void set_gp(u32 gp) { r[28] = gp; }
};

// jalankan sampai stop (syscall tak dikenal / fault / PC di luar RAM / instret cap).
// handler dipanggil tiap syscall. return true = berhenti dgn fault (lihat cpu.fault).
bool run(Cpu& cpu, SyscallFn handler, u64 max_inst);

} // namespace eecpu
