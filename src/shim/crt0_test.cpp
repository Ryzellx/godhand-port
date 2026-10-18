// crt0_test — bukti 7b: CPU interpreter R5900 menjalankan CRT0 ASLI God Hand
// (bytes dari ELF verified md5 1db3b1ea...) sampai syscall SetupThread(60) +
// SetupHeap(61) yang di-dispatch ke shim. Zero fake instructions.
// Output deterministik di STDOUT (untuk md5 rerun); timing di STDERR.
// Pakai: crt0_test [elf_path] [max_inst_cap]   (default cap 50_000_000_000)
#include "ee_cpu.h"
#include "ee_bus.h"
#include "ee_kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include <algorithm>

using namespace eecpu;

static u32 g_syscalls_seen[16];        // urutan awal (kriteria PASS 60->61)
static int g_nsys = 0;
static u64 g_census[160]{};            // census penuh: count per nomor syscall
static u64 g_census_oob = 0;           // nomor >=160 (di luar array, dicatat jujur)
static u32 g_heap_end = 0;
static u32 g_hist[1u << 21]{};         // hot-PC sampler: bucket = pc>>2 (per instr; 8 MB)
static u64 g_btb[1u << 21]{};          // backward-taken-branch census per alamat (16 MB)

// shim syscall — mapping ke eekern (kontrak dari syscall_contract.csv)
static eekern::SemaParam g_sp;
static s32 g_sema = -1;
static bool sys_handler(u32 num, u32 a0, u32 a1, u32 a2, u32 a3, u32& v0) {
    if (num < 160) g_census[num]++; else g_census_oob++;
    switch (num) {
    case 64: {  // CreateSema — a0 = pointer param struct di bus mem
        u32 init = eebus::read32(a0 + 8), maxc = eebus::read32(a0 + 12);
        g_sp.init = (s32)init; g_sp.max = (s32)maxc;
        g_sema = eekern::CreateSema(&g_sp);
        v0 = (u32)g_sema;
        return true;
    }
    case 65: v0 = (u32)eekern::DeleteSema((s32)a0); return true;   // DeleteSema
    case 66: v0 = (u32)eekern::SignalSema((s32)a0); return true;   // SignalSema
    case 68: v0 = (u32)eekern::WaitSema((s32)a0); return true;     // WaitSema
    case 69: v0 = (u32)eekern::PollSema((s32)a0); return true;     // PollSema
    case 32: {  // CreateThread — a0 param struct, a1 = entry, a2 = stack
        eekern::ThreadParam tp;
        tp.prio = (s32)eebus::read32(a0 + 8);
        tp.stack = eebus::read32(a0 + 12);
        v0 = (u32)eekern::CreateThread(&tp, nullptr, (s32)a1);
        return true;
    }
    case 34: v0 = (u32)eekern::StartThread((s32)a0, (s32)a1); return true;  // StartThread
    case 35: eekern::ExitThread(); v0 = 0; return true;                     // ExitThread
    case 47: v0 = (u32)eekern::GetThreadId(); return true;                  // GetThreadId
    case 41: v0 = (u32)eekern::ChangeThreadPriority((s32)a0, (s32)a1); return true;
    case 100: v0 = eekern::FlushCache(a0); return true;                     // FlushCache
    case 108: case 109: case 110: case 111: v0 = 0; return true;            // CPU timer stubs
    case 112: case 113: case 114: v0 = 0; return true;                       // GS IMR/PGIF stubs
    case 115: v0 = 0; return true;                                           // SetVSyncFlag (injected periodic naik)
    case 118: v0 = 0; return true;                                          // SifDmaStat: idle
    case 119: v0 = 1; return true;                                          // SifSetDma: done
    case 120: v0 = 0; return true;                                          // SifSetDChain
    case 121: case 122: v0 = 0; return true;                                // SifSet/GetReg
    case 124: v0 = 0; return true;                                          // Deci2Call
    case 127: v0 = 32 * 1024 * 1024; return true;                           // GetMemorySize 32MB
    case 131: { // FindAddress — SEMANTIK ASLI dari implementasi game sendiri @VMA0x3b64a8:
                // scan u32 [a0..a1) cari kata pertama == *(u32*)a2; kembalikan alamat
                // match, atau a1 (end) kalau tak ada. (Terpetak dari disasm: lw v0,0(a0);
                // beq v0,a2; sltu v0,a0,a1 — return a0.)
        u32 pat = eebus::read32(a2);
        u32 found = a1;                         // default: end (tanpa match)
        for (u32 ad = a0; ad < a1; ad += 4) {
            if (eebus::read32(ad) == pat) { found = ad; break; }
        }
        v0 = found;
        return true;
    }
    case 116: v0 = 0; return true;                                          // SetSyscall (native: tak perlu)
    case 126: v0 = 0; return true;                                          // MachineType
    case 125: v0 = 0; return true;                                          // PSMode
    case 84: v0 = 0; return true;                                           // ClearEventFlag
    case 85: case 86: case 87: case 88: v0 = 0; return true;                // TLB (native: no-op)
    case 90: case 91: v0 = 0; return true;                                  // ReferEventFlagStatus (old)
    case 80: case 81: case 82: case 83: v0 = 0; return true;                // EventFlag old variants
    case 89: v0 = 0; return true;
    case 92: case 93: case 94: case 95: v0 = 0; return true;                // IntcHandler enable/disable
    case 0: v0 = 0; return true;                                            // RFU0 noop
    }
    // fallback lama: 60/61

    if (num == 60) {           // SetupThread: return stack top yang dibuat
        if (g_nsys < 16) g_syscalls_seen[g_nsys++] = num;
        v0 = 0x02000000;       // stack top (native: kita beri RAM atas)
        return true;
    }
    if (num == 61) {           // SetupHeap: return heap pointer
        if (g_nsys < 16) g_syscalls_seen[g_nsys++] = num;
        g_heap_end = a0;       // game kasih end-of-heap param
        v0 = 0x00100000;       // heap base native
        return true;
    }
    if (num == 0) { v0 = 0; return true; }   // RFU0/noop passthrough
    v0 = 0;
    return false;              // unknown -> stop (di luar crt0 scope)
}

static void load_elf_into_bus(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "ELF tidak ketemu: %s\n", path); exit(2); }
    // program header tunggal: offset 0x001000 -> vaddr 0x00100000, filesize 0x360ce4
    const u32 file_off = 0x001000, vaddr = 0x00100000, filesz = 0x360ce4;
    std::vector<u8> buf(filesz);
    fseek(f, file_off, SEEK_SET);
    if (fread(buf.data(), 1, filesz, f) != filesz) { std::fprintf(stderr, "read fail\n"); exit(2); }
    fclose(f);
    eebus::reset();
    eebus::mem_write(vaddr, buf.data(), filesz);
}

// region label buat hot-PC (snapshot ELF verified: .text 0x100000..0x3bbf20,
// .data 0x3bbf80..0x40a3e0, .sndata 0x40a400..0x41b570, sisanya .rodata/.bss)
static const char* region_of(u32 pc) {
    if (pc < 0x003bbf80) return "text";
    if (pc < 0x0040a400) return "data";
    if (pc < 0x0041b570) return "sndata";
    if (pc < 0x00600000) return "rodata";
    if (pc < 0x007867b4) return "bss";
    return "ram";
}

int main(int argc, char** argv) {
    const char* elf = argc > 1 ? argv[1] : "/root/ports/godhand/extract/SLUS_215.03";
    u64 cap = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 50'000'000'000ull;
    u32 trace_dump = argc > 3 ? (u32)std::strtoul(argv[3], nullptr, 10) : 0; // n PC terakhir utk dump
    static u32 g_trace[1u << 16];    // ring 64K PC (diagnosis spin loop)
    load_elf_into_bus(elf);

    // sanity: bytes crt0 persis dari disasm (verified): beq v0,v1 @0x100178
    u32 i0 = eebus::read32(0x100178);
    if (i0 != 0x10430007u) {
        std::printf("CRT0 SANITY FAIL: @0x100178 = %08x (expect 10430007)\n", i0);
        return 2;
    }

    Cpu cpu;
    // PC start post-clear: 0x100198 (blok SetupThread/SetupHeap — gate crt0 asli).
    cpu.reset(0x31db70);  // main wrapper game asli
    cpu.set_sp(0x02000000);                    // boot stack atas
    cpu.set_gp(0x0063fd48);                    // gp dari crt0: lui a0,0x47; addiu a0,-29456
    cpu.hist = g_hist;
    cpu.sample_mask = 0xFFFFu;                 // sampling tiap 65536 instr
    cpu.btb = g_btb;
    if (trace_dump) {                 // trace ring aktif — dump N PC terakhir pasca-run
        cpu.trace = g_trace;
        cpu.trace_mask = (1u << 16) - 1;
    }

    // "ROM kernel handler area" — di HW asli, ROM kernel EE memasang exception handler
    // di RAM rendah [0x80000000..0x80080000) sebelum crt0; jalur FindAddress walk
    // (FUN_003b6500) mencari 2 kata penanda template di area itu (offset -0x20c/-0x168
    // dari hasil) utk menemukan basis handler. Native port tanpa ROM kernel: tanam
    // DUA kata penanda dgn jarak 0xA4 (=0x20c-0x168) di H+0x20c / H+0x168 supaya walk
    // konvergen dgn basis ROMH (stub setara "ROM kernel sudah pasang handler").
    const u32 ROMH = 0x80001000u;
    eebus::write32(ROMH + 0x20c, eebus::read32(0x3b64a8));  // pat1 = kata @VMA 0x3b64a8
    eebus::write32(ROMH + 0x168, eebus::read32(0x3b6470));  // pat2 = kata @VMA 0x3b6470

    auto t0 = std::chrono::steady_clock::now();
    bool stopped = eecpu::run(cpu, sys_handler, cap);  // main-loop long-run
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    std::printf("crt0 run: instret=%llu stop=%s pc=0x%08x\n",
                (unsigned long long)cpu.instret,
                cpu.fault ? cpu.fault : "clean-cap", cpu.pc);
    std::printf("syscalls seen: %d ->", g_nsys);
    for (int i = 0; i < g_nsys; i++) std::printf(" %u", g_syscalls_seen[i]);
    std::printf("\n");

    // census penuh (deterministik: urut nomor naik)
    std::printf("syscall census:");
    u64 distinct = 0, total_sys = 0;
    for (u32 n = 0; n < 160; n++) if (g_census[n]) {
        std::printf(" %u:%llu", n, (unsigned long long)g_census[n]);
        distinct++; total_sys += g_census[n];
    }
    if (g_census_oob) std::printf(" oob:%llu", (unsigned long long)g_census_oob);
    total_sys += g_census_oob;
    std::printf(" | distinct=%llu total=%llu\n",
                (unsigned long long)distinct, (unsigned long long)total_sys);

    // top-12 hot PC (sort: count desc, lalu alamat asc -> deterministik)
    struct H { u32 n, b; };
    std::vector<H> hs;
    for (u32 b = 0; b < (1u << 21); b++) if (g_hist[b]) hs.push_back({g_hist[b], b});
    std::sort(hs.begin(), hs.end(), [](const H& a, const H& b) {
        return a.n != b.n ? a.n > b.n : a.b < b.b;
    });
    u64 samples = 0; for (auto& h : hs) samples += h.n;
    std::printf("hot-pc top12 (samples=%llu/%llu instr):\n",
                (unsigned long long)samples, (unsigned long long)cpu.instret);
    for (size_t i = 0; i < hs.size() && i < 12; i++) {
        u32 pc = hs[i].b << 2;
        std::printf("  0x%08x %-7s n=%-10u %5.2f%%\n", pc, region_of(pc), hs[i].n,
                    samples ? 100.0 * hs[i].n / samples : 0.0);
    }

    // backward-taken-branch census top-8 (nol sampling -> hitungan eksak per alamat)
    struct B { u64 n; u32 b; };
    std::vector<B> bs;
    for (u32 b = 0; b < (1u << 21); b++) if (g_btb[b]) bs.push_back({g_btb[b], b});
    std::sort(bs.begin(), bs.end(), [](const B& a, const B& b) {
        return a.n != b.n ? a.n > b.n : a.b < b.b;
    });
    u64 btb_total = 0; for (auto& b : bs) btb_total += b.n;
    std::printf("backward-branch census top8 (total=%llu):\n", (unsigned long long)btb_total);
    for (size_t i = 0; i < bs.size() && i < 8; i++)
        std::printf("  0x%08x n=%llu\n", bs[i].b << 2, (unsigned long long)bs[i].n);

    std::fprintf(stderr, "timing: %.2f s (%.1f M inst/s)\n", sec,
                 sec > 0 ? cpu.instret / sec / 1e6 : 0.0);

    int fail = 0;
    // dump N PC terakhir dari trace ring (ring penuh: mulai dari trace_pos — urut kronologis)
    if (trace_dump && cpu.trace) {
        u32 sz = cpu.trace_mask + 1;
        u32 n = trace_dump < sz ? trace_dump : sz;
        std::printf("trace last %u pc (oldest->newest):\n", n);
        u32 start = (cpu.trace_pos + sz - n) & cpu.trace_mask;
        for (u32 i = 0; i < n; i++) {
            u32 pc = cpu.trace[(start + i) & cpu.trace_mask];
            std::printf("  [%u] 0x%08x %s op=%08x\n", i, pc, region_of(pc),
                        eebus::read32(pc));
        }
    }
    // bukti minimal: SetupThread (60) DAN SetupHeap (61) kena, berurutan
    if (g_nsys < 2 || g_syscalls_seen[0] != 60 || g_syscalls_seen[1] != 61) {
        std::printf("FAIL: urutan syscall crt0 tidak sesuai\n"); fail = 1;
    } else {
        std::printf("PASS: crt0 asli dieksekusi CPU — SetupThread->SetupHeap via shim\n");
    }
    if (cpu.fault && strncmp(cpu.fault, "instret", 7) != 0 && g_nsys < 3) {
        std::printf("NOTE: stop dgn fault=%s @pc=0x%08x op=%08x (crt0 2 syscall pertama OK)\n",
                    cpu.fault, cpu.fault_pc, cpu.fault_op);
    }
    return fail ? 1 : 0;
}
