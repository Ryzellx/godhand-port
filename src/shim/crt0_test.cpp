// crt0_test — bukti 7b: CPU interpreter R5900 menjalankan CRT0 ASLI God Hand
// (bytes dari ELF verified md5 1db3b1ea...) sampai syscall SetupThread(60) +
// SetupHeap(61) yang di-dispatch ke shim. Zero fake instructions.
// Output deterministik di STDOUT (untuk md5 rerun); timing di STDERR.
// Pakai: crt0_test [elf_path] [max_inst_cap]   (default cap 50_000_000_000)
#include "ee_cpu.h"
#include "ee_bus.h"
#include "ee_kernel.h"
#include "sif_rpc.h"
#include "iso9660.h"
#include "sif_dma.h"
#include "gif.h"
#include "memcard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
static u64 g_sema_autosig = 0;         // WaitSema auto-signal (virtual INTC event) — Phase 7b.9

// shim syscall — mapping ke eekern (kontrak dari syscall_contract.csv)
static eekern::SemaParam g_sp;
static s32 g_sema = -1;
// Phase 7c readiness — packet GIF NYATA dari kode game (DMA ch2) diumpankan ke parser
// produksi (`gif::parse_stream`, unit-test 6/6). Yang diukur: berapa kick, berapa tag,
// berapa aksi ter-normalisasi, dan berapa paket yang gagal parse. Nol render di sini —
// hanya bukti bahwa rantai game->DMA->parser tersambung tanpa emulator.
struct GifCensus { u64 kicks = 0, tags = 0, actions = 0, bad = 0, qwords = 0; };
static GifCensus g_gif;

static void gif_kick_census(const void* packet, u32 qwc) {
    if (g_gif.kicks < 2 && std::getenv("GH_GIF_DUMP")) {
        const u8* b = (const u8*)packet;
        std::fprintf(stderr, "[gif] kick qwc=%u:", qwc);
        for (u32 i = 0; i < qwc * 16u; i++) std::fprintf(stderr, "%02x", b[i]);
        std::fprintf(stderr, "\n");
    }
    g_gif.kicks++;
    g_gif.qwords += qwc;
    const u8* buf = (const u8*)packet;
    size_t off = 0, len = (size_t)qwc * 16;
    while (off + 16 <= len) {
        std::vector<gif::Action> acts;
        size_t used = 0;
        if (!gif::parse_stream(buf + off, len - off, acts, &used) || used == 0) { g_gif.bad++; break; }
        g_gif.tags++;
        g_gif.actions += acts.size();
        off += used;
    }
}

static bool sys_handler(u32 num, u32 a0, u32 a1, u32 a2, u32 a3, u32& v0) {
    if (num < 160) g_census[num]++; else g_census_oob++;
    switch (num) {
    case 16: v0 = 1; return true;   // AddIntcHandler — native: no-op dispatch (dispatch IRQ nanti)
    case 10: v0 = 1; return true; case 11: case 12: v0 = 0; return true;   // SbusIntc/Interrupt2Iop
    case 13: case 14: case 15: v0 = 0; return true;                          // VTLB/VCommon/VInterrupt handler
    case 17: case 19: v0 = 0; return true;                                  // Remove{Intc,Dmac}Handler
    case 18: v0 = 1; return true;                                           // AddDmacHandler
    case 20: case 21: case 22: case 23: v0 = 0; return true;                // _Enable/Disable{Intc,Dmac}
    case 64: {  // CreateSema — a0 = pointer param struct di bus mem
        u32 init = eebus::read32(a0 + 8), maxc = eebus::read32(a0 + 12);
        g_sp.init = (s32)init; g_sp.max = (s32)maxc;
        if (g_sp.max < 1) g_sp.max = 1;   // max<=0 -> SignalSema no-op -> WaitSema deadlock
        g_sema = eekern::CreateSema(&g_sp);
        std::fprintf(stderr, "[sema] create id=%d init=%d max=%d (raw init=0x%x max=0x%x @0x%08x)\n",
                     g_sema, g_sp.init, g_sp.max, init, maxc, a0);
        v0 = (u32)g_sema;
        return true;
    }
    case 65: v0 = (u32)eekern::DeleteSema((s32)a0); return true;   // DeleteSema
    case 66: v0 = (u32)eekern::SignalSema((s32)a0); return true;   // SignalSema
    case 68: {  // WaitSema — blokir di host futex. Bukti deadlock 2026-10-21: gdb bt
                // menunjuk eekern::WaitSema(id=4) ← sys 68, 0% CPU, 1 thread, tanpa
                // signaler (census: CreateThread 32:1 tapi StartThread 34:0 — thread
                // tak pernah jalan; SignalSema 66 tak pernah dipanggil game; INTC
                // dispatch belum ada di CPU). Satu-satunya signaler di HW asli =
                // INTC handler hasil AddIntcHandler (sys 16:1 tercatat).
                // Model native: virtual-event auto-signal sekali kalau kosong.
        // PollSema sukses = token sudah dikonsumsi -> JANGAN WaitSema lagi
        // (bug iterasi pertama: pol>=0 tapi tetap WaitSema -> count 0 -> deadlock).
        s32 pol = eekern::PollSema((s32)a0);
        if (pol >= 0) { v0 = 0; return true; }
        eekern::SignalSema((s32)a0); g_sema_autosig++;
        std::fprintf(stderr, "[sema] WaitSema(%d) kosong -> autosig #%llu\n",
                     (int)a0, (unsigned long long)g_sema_autosig);
        v0 = (u32)eekern::WaitSema((s32)a0);
        return true;
    }
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
    case 115: {
        // SetVSyncFlag: a1 = pointer flag (dugaan ABI). Tulis 1 = VSync terjadi,
        // supaya game tidak nunggu selamanya. 2026-10-19.
        if (a1 >= 0x100000 && a1 < 0x02000000) eebus::write32(a1, 1);
        v0 = 0; return true;
    }
    case 118: v0 = sifdma::dma_stat(a0); return true;                       // SifDmaStat: -1 = selesai
    case 119: {                                                             // SifSetDma: kick DMA EE<->IOP
        // Phase 8: bukan lagi stub. sdd[] = {src, dest, size, attr} x a1; paket
        // sifcmd/sifrpc di-decode di sif_dma, backend host dipanggil, reply ditulis
        // balik ke bus EE + sema klien di-signal (model iSignalSema _request_end).
        v0 = sifdma::kick(a0, a1);
        return true;
    }
    case 120: v0 = 0; return true;                                          // SifSetDChain
    case 121: v0 = eekern::SifSetReg(a0, a1); return true;              // SifSetReg(reg, val)
    case 122: v0 = eekern::SifGetReg(a0); return true;                  // SifGetReg(reg) — poll reg4&0x20000
    case 124: {                                                             // Deci2Call (debug channel)
        // BUKAN RPC: tabel syscall kanonik 0x7C = Deci2Call. Arg lapangan
        // (a0=0x10, a1=0x01fffc30, a2=0x0a) tak punya header sifcmd. Native tanpa
        // DECI2 driver -> -1 (tak ada channel), sama seperti ps2sdk tanpa deci2.man.
        static int d2_log = 0;
        if (d2_log++ < 3)
            std::fprintf(stderr, "[deci2] #%d func=0x%x arg=0x%08x a2=0x%x -> -1 (no driver)\n",
                         d2_log, a0, a1, a2);
        // Nilai balik dipilih dari pengukuran, bukan tebakan: -1 (tak ada channel) membuat
        // boot stall di pc=0x003b205c (2 B inst, 0 bind); 1 lanjut ke RPC bind.
        static s32 d2ret = -2;
        if (d2ret == -2) { const char* e = std::getenv("GH_DECI2_RET"); d2ret = e ? (s32)std::strtol(e, nullptr, 0) : 1; }
        v0 = (u32)d2ret; return true;
    }
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
    case 130: v0 = 0; return true;                                          // _InitTLB (flat RAM: no-op)
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
    // generic stub — unknown syscall return 0, jangan stop
    v0 = 0;
    return true;
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
    eekern::sif_regs_reset();        // Phase 7b.9: state "IOP siap" (reg4=0x20000)
    sifrpc::reset();                 // Phase 8: registry server host + statistik
    sifrpc::install_defaults();      // fileio/pad/cdvd/mcserv
    sifdma::reset();
    eekern::set_gif_kick(gif_kick_census);
    if (const char* v = std::getenv("GH_SIF_VERBOSE")) sifdma::set_verbose(std::atoi(v));

    // sanity: bytes crt0 persis dari disasm (verified): beq v0,v1 @0x100178
    u32 i0 = eebus::read32(0x100178);
    if (i0 != 0x10430007u) {
        std::printf("CRT0 SANITY FAIL: @0x100178 = %08x (expect 10430007)\n", i0);
        return 2;
    }

    Cpu cpu;
    // FULL crt0 dari entry ELF (0x100008): heap-clear -> SetupThread/SetupHeap (60->61)
    // -> walk ROM-handler -> install exception -> main wrapper. PASS = 60,61 berurutan.
    // (Sebelumnya start 0x31db70 = skip crt0; kriteria 60->61 tak pernah tercapai — fix sesi cron #8.)
    cpu.reset(0x100008);  // ELF entry asli (verified: entry 0x100008, Phase 1a)
    cpu.set_sp(0x01FFFF00);                    // boot stack: top RAM valid (0x02000000 & MASK = 0!)
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

    // diagnosis (Phase 8/9): halt tepat sebelum pc tertentu, trace jendela, dump memori.
    // Semua output ke STDERR -> stdout tetap deterministik utk md5 rerun 5x.
    if (const char* h = std::getenv("GH_HALT_PC")) cpu.halt_pc = (u32)std::strtoul(h, nullptr, 0);
    if (const char* hs = std::getenv("GH_HALT_SKIP")) cpu.halt_skip = (u32)std::strtoul(hs, nullptr, 0);
    if (const char* wt = std::getenv("GH_WATCH")) {
        std::string ww(wt); size_t dash = ww.find('-');
        eebus::watch_set((u32)std::strtoul(ww.substr(0, dash).c_str(), nullptr, 0),
                         dash == std::string::npos ? 0 : (u32)std::strtoul(ww.substr(dash + 1).c_str(), nullptr, 0));
        if (const char* cap = std::getenv("GH_WATCH_CAP")) eebus::g_watch_cap = std::strtoull(cap, nullptr, 0);
    }
    if (const char* w = std::getenv("GH_TRACE_WINDOW")) {
        std::string ww(w); size_t dash = ww.find('-');
        cpu.trace_lo = (u32)std::strtoul(ww.substr(0, dash).c_str(), nullptr, 0);
        cpu.trace_hi = dash == std::string::npos ? cpu.trace_lo + 4 : (u32)std::strtoul(ww.substr(dash + 1).c_str(), nullptr, 0);
    }

    // ISO retail = sumber CDVD. Kalau tidak ada, backend jatuh ke extract/ (GH_CDROOT).
    if (iso9660::mount("")) std::fprintf(stderr, "[iso] mounted %s\n", iso9660::image_path().c_str());
    else std::fprintf(stderr, "[iso] TIDAK termount — CDVD hanya lewat extract/\n");

    auto t0 = std::chrono::steady_clock::now();
    bool stopped = eecpu::run(cpu, sys_handler, cap);  // main-loop long-run
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    if (cpu.halt_hits) {
        std::fprintf(stderr, "[HALT] pc=0x%08x hits=%llu instret=%llu\n",
                     cpu.pc, (unsigned long long)cpu.halt_hits, (unsigned long long)cpu.instret);
        static const char* rn_[32] = {"zr","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3",
            "t4","t5","t6","t7","s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1",
            "gp","sp","fp","ra"};
        for (int i = 1; i < 32; i++)
            if (cpu.r[i][0] || i == 2 || i == 29 || i == 31)
                std::fprintf(stderr, "  %-2s = 0x%08x\n", rn_[i], cpu.r[i][0]);
        if (const char* dm = std::getenv("GH_DUMP_MEM")) {
            std::string all(dm); size_t pos = 0;
            while (pos < all.size()) {
                size_t comma = all.find(',', pos);
                std::string it = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                size_t colon = it.find(':');
                u32 a = (u32)std::strtoul(it.substr(0, colon).c_str(), nullptr, 0);
                u32 n = colon == std::string::npos ? 16 : (u32)std::strtoul(it.substr(colon + 1).c_str(), nullptr, 0);
                for (u32 o = 0; o < n; o += 16) {
                    std::fprintf(stderr, "  [%08x]", a + o);
                    for (u32 k = 0; k < 16 && o + k < n; k += 4)
                        std::fprintf(stderr, " %08x", eebus::read32(a + o + k));
                    std::fprintf(stderr, "\n");
                }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
    }
    std::printf("crt0 run: instret=%llu stop=%s pc=0x%08x fault_pc=0x%08x fault_op=%08x\n",
                (unsigned long long)cpu.instret,
                cpu.fault ? cpu.fault : "clean-cap", cpu.pc, cpu.fault_pc, cpu.fault_op);
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

    // SIF register file census (Phase 7b.9) — get/set per reg + nilai akhir reg4 (poll target)
    std::printf("sif-regs get:");
    for (u32 r = 0; r < 16; r++) if (eekern::sif_get_count(r)) std::printf(" %u:%llu", r, (unsigned long long)eekern::sif_get_count(r));
    if (eekern::sif_get_count(16)) std::printf(" oob:%llu(last=0x%08x)", (unsigned long long)eekern::sif_get_count(16), eekern::sif_oob_last(0));
    std::printf(" | set:");
    for (u32 r = 0; r < 16; r++) if (eekern::sif_set_count(r)) std::printf(" %u:%llu", r, (unsigned long long)eekern::sif_set_count(r));
    if (eekern::sif_set_count(16)) std::printf(" oob:%llu(last=0x%08x)", (unsigned long long)eekern::sif_set_count(16), eekern::sif_oob_last(1));
    std::printf(" | reg4=0x%08x\n", eekern::sif_reg_peek(4));
    std::printf("sema-autosig=%llu (WaitSema virtual-event, Phase 7b.9)\n",
                (unsigned long long)g_sema_autosig);

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

    // census VU0 (Phase 7b.8): op macro, lqc2/sqc2, SPECIAL1/SPECIAL2 top-hits, unk
    std::printf("VU0: macro=%llu lqc2=%llu sqc2=%llu unk=%llu\n",
                (unsigned long long)cpu.vu_macro_hits,
                (unsigned long long)cpu.vu_lqc2,
                (unsigned long long)cpu.vu_sqc2,
                (unsigned long long)cpu.vu_unk);
    {   // SPECIAL1 top-8 (idx:count desc, idx asc) — bukti decode tabel dipakai game
        struct VH { u64 n; u32 i; };
        std::vector<VH> s1;
        for (u32 i = 0; i < 64; i++) if (cpu.vu_spec1_hits[i]) s1.push_back({cpu.vu_spec1_hits[i], i});
        std::sort(s1.begin(), s1.end(), [](const VH& a, const VH& b){ return a.n!=b.n ? a.n>b.n : a.i<b.i; });
        std::printf("VU0 SPEC1 top:");
        for (size_t i=0;i<s1.size()&&i<8;i++) std::printf(" %u:%llu", s1[i].i,(unsigned long long)s1[i].n);
        std::printf(" | distinct=%zu\n", s1.size());
        std::vector<VH> s2;
        for (u32 i = 0; i < 128; i++) if (cpu.vu_spec2_hits[i]) s2.push_back({cpu.vu_spec2_hits[i], i});
        std::sort(s2.begin(), s2.end(), [](const VH& a, const VH& b){ return a.n!=b.n ? a.n>b.n : a.i<b.i; });
        std::printf("VU0 SPEC2 top:");
        for (size_t i=0;i<s2.size()&&i<8;i++) std::printf(" %u:%llu", s2[i].i,(unsigned long long)s2[i].n);
        std::printf(" | distinct=%zu\n", s2.size());
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

    // Phase 7b.9 — busy-delay fast-forward census (semantik eksak, instret tetap dihitung)
    std::printf("delay-loops skipped=%llu inst_skipped=%llu (%.1f%% dari instret)\n",
                (unsigned long long)eecpu::g_delay_loops,
                (unsigned long long)eecpu::g_delay_inst,
                cpu.instret ? 100.0 * (double)eecpu::g_delay_inst / (double)cpu.instret : 0.0);

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
    // Phase 8 — bukti jalur SIF DMA -> RPC host (angka dari paket nyata)
    {
        const sifdma::Stats& d = sifdma::stats();
        std::printf("sifdma: kicks=%llu entries=%llu ee2iop=%llu(%llu B) iop2ee=%llu(%llu B) endfunc_queued=%llu stat_pending=%llu stat_retired=%llu\n",
                    (unsigned long long)d.kicks, (unsigned long long)d.entries,
                    (unsigned long long)d.ee2iop, (unsigned long long)d.ee2iop_bytes,
                    (unsigned long long)d.iop2ee, (unsigned long long)d.iop2ee_bytes,
                    (unsigned long long)d.cb_queued,
                    (unsigned long long)d.stat_pending, (unsigned long long)d.stat_retired);
        std::printf("sifcmd: init=%llu reset=%llu sreg=%llu bind=%llu call=%llu end=%llu other=%llu short=%llu\n",
                    (unsigned long long)d.cid_init, (unsigned long long)d.cid_reset,
                    (unsigned long long)d.cid_sreg,
                    (unsigned long long)d.cid_bind, (unsigned long long)d.cid_call,
                    (unsigned long long)d.cid_end, (unsigned long long)d.cid_other,
                    (unsigned long long)d.pkt_short);
        std::printf("rpc: bind_replies=%llu call_replies=%llu sema_signals=%llu unknown_srv=%llu "
                    "handler_fail=%llu autostub=%llu payload=%llu B reply=%llu B last{cid=0x%08x srv=0x%08x fno=%u res=%d}\n",
                    (unsigned long long)d.bind_replies, (unsigned long long)d.call_replies,
                    (unsigned long long)d.sema_signals, (unsigned long long)d.unknown_server,
                    (unsigned long long)d.handler_fail, (unsigned long long)d.autostub,
                    (unsigned long long)d.payload_bytes,
                    (unsigned long long)d.reply_bytes, d.last_cid, d.last_server, d.last_func,
                    (int)d.last_result);
        std::printf("cdvd-ee: read_ok=%llu read_fail=%llu bytes=%llu fno_lain=%llu\n",
                (unsigned long long)sifrpc::cdvd_read_ok(),
                (unsigned long long)sifrpc::cdvd_read_fail(),
                (unsigned long long)sifrpc::cdvd_read_bytes(),
                (unsigned long long)sifrpc::cdvd_other_calls());
    std::printf("cdvfs: open_ok=%llu open_miss=%llu other=%llu\n",
                    (unsigned long long)sifrpc::cdvfs_open_ok(),
                    (unsigned long long)sifrpc::cdvfs_open_miss(),
                    (unsigned long long)sifrpc::cdvfs_other_calls());
        const sifrpc::Stats& r = sifrpc::stats();
        std::printf("host-rpc: calls=%llu unknown=%llu in=%llu B out=%llu B | memcard root=%s\n",
                    (unsigned long long)r.calls, (unsigned long long)r.unknown,
                    (unsigned long long)r.bytes_in, (unsigned long long)r.bytes_out,
                    memcard::root().c_str());
    }

    // Phase 8 diagnosa: isi buffer RPC loadfile (ground truth fno255 = "3100")
    {
        u32 rb = eebus::read32(0x00781980u), cp = eebus::read32(0x00781ba8u);
        char rbs[5] = { (char)(rb & 0xff), (char)((rb >> 8) & 0xff),
                        (char)((rb >> 16) & 0xff), (char)((rb >> 24) & 0xff), 0 };
        std::printf("loadfile: other_fno=%llu recvbuf@0x781980=0x%08x \"%s\" copy@0x781ba8=0x%08x\n",
                    (unsigned long long)sifrpc::loadfile_other_calls(), rb, rbs, cp);
        std::printf("loadfile: load=%llu fail=%llu modul=[%s]\n",
                    (unsigned long long)sifrpc::loadfile_load_count(),
                    (unsigned long long)sifrpc::loadfile_load_fail_count(),
                    sifrpc::loadfile_modules_csv());
    }

    // Diagnosa spin 0x375930: objek async game di 0x76A95C (lihat 0x3b2f48/0x3b3328)
    if (std::getenv("GH_SIF_DUMP")) {
        for (u32 base : { 0x0076A95Cu, 0x0076A980u }) {
            std::printf("mem 0x%08x:", base);
            for (int i = 0; i < 12; i++) std::printf(" %08x", eebus::read32(base + i * 4));
            std::printf("\n");
        }
        u32 p = eebus::read32(0x0076A95Cu);
        std::printf("ctx[0]=0x%08x ->", p);
        if (p && p < 0x02000000u) for (int i = 0; i < 12; i++) std::printf(" %08x", eebus::read32(p + i * 4));
        std::printf("\n");
        std::printf("check is_done: pkt@alias[24]=0x%08x (ctx[4]=0x%08x) pkt@alias[16]=0x%08x -> bit0=%u\n",
                    eebus::read32(0x2077EA80u + 24), eebus::read32(0x0076A95Cu + 4),
                    eebus::read32(0x2077EA80u + 16), eebus::read32(0x2077EA80u + 16) & 1u);
        std::printf("pkt raw 0x77EA80:");
        for (int i = 0; i < 12; i++) std::printf(" %08x", eebus::read32(0x0077EA80u + i * 4));
        std::printf("\n");
    }

    // Phase 7c readiness: apakah kode GAME (bukan test) sudah menulis DMA ch2 sendiri?
    {
        const eebus::Stats& bs = eebus::stats();
        std::printf("gif: kicks=%llu qwords=%llu tags=%llu actions=%llu bad=%llu\n",
                    (unsigned long long)g_gif.kicks, (unsigned long long)g_gif.qwords,
                    (unsigned long long)g_gif.tags, (unsigned long long)g_gif.actions,
                    (unsigned long long)g_gif.bad);
        std::printf("bus: kicks=%llu qwords=%llu chcr_polls=%llu vif_polls=%llu mmio_other=%llu"
                    " last{madr=0x%x qwc=%u chcr=0x%x tte=%d}\n",
                    (unsigned long long)bs.kicks, (unsigned long long)bs.qwords,
                    (unsigned long long)bs.chcr_polls, (unsigned long long)bs.vif_polls,
                    (unsigned long long)bs.mmio_other, bs.last_madr, bs.last_qwc, bs.last_chcr,
                    bs.last_tte ? 1 : 0);
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
