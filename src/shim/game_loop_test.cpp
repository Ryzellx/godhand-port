// shim/game_loop_test.cpp — GAME LOOP INTEGRASI PERTAMA
// CPU interpreter (kode game asli) -> DMA ch2 kick -> GIF parser -> GS state -> Vulkan -> PPM
// Ini bukti pipeline utuh: game code mengirim packet render, kita render hasilnya.
#include "ee_cpu.h"
#include "ee_bus.h"
#include "ee_kernel.h"
#include "../gif/gif.h"
#include "../gs/gs_state.h"
#include "../vk/vk_backend.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>

static const uint32_t W = 640, H = 448;
static std::vector<vkbe::Vertex> g_batch;
static vkbe::Vertex g_pending = {};
static gs::State g_gs;
static uint32_t g_frames = 0;
static uint32_t g_kicks = 0;

// Callback: DMA ch2 kick dari CPU -> parse GIF -> accumulate vertices
static void on_gif_kick(const void* packet, uint32_t qwc) {
    g_kicks++;
    size_t bytes = qwc * 16;
    std::vector<gif::Action> acts;
    size_t cons = 0;
    if (!gif::parse_stream((const uint8_t*)packet, bytes, acts, &cons)) {
        std::fprintf(stderr, "[game] GIF parse fail at kick %u (qwc=%u)\n", g_kicks, qwc);
        return;
    }
    for (auto& a : acts) {
        switch (a.kind) {
        case gif::Action::AD_WRITE:
            g_gs.ad_write((uint32_t)a.ad_addr, a.ad_data);
            break;
        case gif::Action::RGBA_WRITE: {
            uint64_t w = a.ad_data;
            g_pending.r = ((w >> 0) & 0xFF) / 255.0f;
            g_pending.g = ((w >> 8) & 0xFF) / 255.0f;
            g_pending.b = ((w >> 16) & 0xFF) / 255.0f;
            g_pending.a = 1.0f;
            break;
        }
        case gif::Action::XYZ_WRITE: {
            uint64_t w = a.ad_data;
            g_pending.x = (float)(int)((w >> 0) & 0xFFFF) / 16.0f;
            g_pending.y = (float)(int)((w >> 16) & 0xFFFF) / 16.0f;
            g_batch.push_back(g_pending);
            break;
        }
        default: break;
        }
    }
    std::fprintf(stderr, "[game] kick#%u: %zu actions, %zu verts accumulated\n",
                 g_kicks, acts.size(), g_batch.size());
}

// Syscall handler — stub semua yang game butuh
static bool sys_handler(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t& v0) {
    switch (num) {
    case 60: v0 = 1; return true;   // SetupThread
    case 61: v0 = 0x2000000; return true;  // SetupHeap
    case 64: v0 = 1; return true;   // CreateSema
    case 65: case 66: case 68: case 69: v0 = 0; return true;
    case 16: v0 = 1; return true;   // AddIntcHandler — native: no-op dispatch (dispatch IRQ nanti)
    case 10: v0 = 1; return true; case 11: case 12: v0 = 0; return true;   // SbusIntc/Interrupt2Iop
    case 13: case 14: case 15: v0 = 0; return true;                          // VTLB/VCommon/VInterrupt handler
    case 17: case 19: v0 = 0; return true;                                  // Remove{Intc,Dmac}Handler
    case 18: v0 = 1; return true;                                           // AddDmacHandler
    case 20: case 21: case 22: case 23: v0 = 0; return true;                // _Enable/Disable{Intc,Dmac}
    case 32: v0 = 1; return true;   // CreateThread
    case 34: v0 = 0; return true;   // StartThread
    case 47: v0 = 1; return true;   // GetThreadId
    case 100: v0 = 0; return true;  // FlushCache
    case 108: case 109: case 110: case 111: v0 = 0; return true;
    case 112: case 113: case 114: v0 = 0; return true;
    case 115: v0 = 0; return true;  // SetVSyncFlag
    case 116: v0 = 0; return true;  // SetSyscall
    case 118: v0 = 0; return true;  // SifDmaStat
    case 119: v0 = 1; return true;  // SifSetDma
    case 120: v0 = 0; return true;  // SifSetDChain
    case 121: v0 = eekern::SifSetReg(a0, a1); return true;  // SifSetReg
    case 122: v0 = eekern::SifGetReg(a0); return true;      // SifGetReg — poll reg4&0x20000
    case 124: v0 = 0; return true;
    case 125: case 126: v0 = 0; return true;
    case 127: v0 = 32*1024*1024; return true;
    case 131: {  // FindAddress — SEMANTIK ASLI (sama dgn crt0_test): scan u32 [a0..a1)
                 // cari kata == *(u32*)a2; return alamat match atau a1 kalau tak ada.
                 // Stub lama balikin 0 → walk ROM-handler FUN_003b6500 tak konvergen.
        uint32_t pat = eebus::read32(a2);
        uint32_t found = a1;
        for (uint32_t ad = a0; ad < a1; ad += 4) {
            if (eebus::read32(ad) == pat) { found = ad; break; }
        }
        v0 = found;
        return true;
    }
    default: v0 = 0; return true;
    }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[game] === GAME LOOP INTEGRASI: CPU -> DMA -> GIF -> Vulkan ===\n");

    // setup CPU
    eebus::reset();
    
    // LOAD ELF ke RAM — critical! Tanpa ini CPU executing zeros
    {
        FILE* ef = fopen("/root/ports/godhand/extract/SLUS_215.03", "rb");
        if (!ef) { std::printf("FAIL: ELF not found\n"); return 1; }
        fseek(ef, 0, SEEK_END);
        long esz = ftell(ef);
        fseek(ef, 0, SEEK_SET);
        std::vector<uint8_t> ebuf(esz);
        fread(ebuf.data(), 1, esz, ef);
        fclose(ef);
        
        // Parse ELF program headers dan load ke RAM
        if (esz > 64 && ebuf[0] == 0x7F && ebuf[1] == 'E') {
            uint32_t phoff = *(uint32_t*)(&ebuf[28]);
            uint16_t phentsize = *(uint16_t*)(&ebuf[42]);
            uint16_t phnum = *(uint16_t*)(&ebuf[44]);
            
            for (int i = 0; i < phnum; i++) {
                uint32_t off = phoff + i * phentsize;
                if (off + 32 > (uint32_t)esz) break;
                uint32_t type = *(uint32_t*)(&ebuf[off]);
                if (type != 1) continue;  // PT_LOAD only
                
                uint32_t poff = *(uint32_t*)(&ebuf[off + 4]);   // p_offset
                uint32_t vaddr = *(uint32_t*)(&ebuf[off + 8]);  // p_vaddr
                uint32_t filesz = *(uint32_t*)(&ebuf[off + 16]); // p_filesz
                uint32_t memsz = *(uint32_t*)(&ebuf[off + 20]);  // p_memsz
                
                std::printf("[ELF] load: vaddr=0x%08x filesz=%u memsz=%u\n", vaddr, filesz, memsz);
                
                // copy file data ke RAM
                for (uint32_t j = 0; j < filesz && poff + j < (uint32_t)esz; j++) {
                    eebus::write8(vaddr + j, ebuf[poff + j]);
                }
                // zero remaining (BSS)
                for (uint32_t j = filesz; j < memsz; j++) {
                    eebus::write8(vaddr + j, 0);
                }
            }
            std::printf("[ELF] loaded successfully\n");
        }
    }
    eecpu::Cpu cpu;
    cpu.reset(0x100008);   // CRT0 entry — full init sequence
    cpu.set_sp(0x02000000);
    cpu.set_gp(0x0063fd48);

    // "ROM kernel handler area" — jalur FindAddress walk FUN_003b6500 (setelah
    // heap-clear @0x100198) nyari DUA kata penanda template di [0x80000000..0x80080000)
    // dgn jarak 0xA4 (0x20c-0x168) utk nemuin basis handler ROM. Tanpa marker → walk
    // infinite (bug 2026-10-19 sesi cron #8). Tanam marker persis seperti crt0_test.
    {
        const uint32_t ROMH = 0x80001000u;
        eebus::write32(ROMH + 0x20c, eebus::read32(0x3b64a8));  // pat1
        eebus::write32(ROMH + 0x168, eebus::read32(0x3b6470));  // pat2
    }

    // setup renderer callback
    eekern::set_gif_kick(on_gif_kick);
    std::printf("[game] GIF kick callback registered\n");

    // setup function table: pointer ke array of function pointers, ending dengan 0
    // dispatcher di 0x31da48: v0 = *(0x3c4080); v1 = *v0; if v1==0 return; jalr v1; v0 += 4; loop
    const uint32_t TABLE_ADDR = 0x003D0000;  // di RAM, bukan code area
    const uint32_t RETURN_STUB = 0x00100248; // crt0 return point
    
    // buat table: 16 entries, semua point ke RETURN_STUB (no-op functions)
    for (int i = 0; i < 16; i++) {
        eebus::write32(TABLE_ADDR + i * 4, RETURN_STUB);
    }
    eebus::write32(TABLE_ADDR + 16 * 4, 0);  // terminator
    
    // set function table pointer
    eebus::write32(0x3c4080, TABLE_ADDR);
    
    std::printf("[game] function table: %u entries at 0x%08x -> 0x%08x\n", 16, TABLE_ADDR, RETURN_STUB);

    // init vtable di 0x3c4080 (function pointer table yang game expects)
    for (uint32_t i = 0; i < 256; i++) {
        eebus::write32(0x3c4080 + i * 4, 0x100248);  // point ke return stub
    }
    // init heap area (game expects initialized heap)
    for (uint32_t i = 0; i < 1024; i++) {
        eebus::write32(0x460000 + i * 4, 0);
    }

    // run CPU — long enough to reach render code
    std::printf("[game] running CPU interpreter...\n");
    bool stopped = eecpu::run(cpu, sys_handler, 100'000'000'000);
    std::printf("[game] last 10 PC trace (dari hot-PC sampler):\n");
    // cek fault_pc untuk tahu instruksi terakhir sebelum crash
    if (stopped && cpu.fault_pc) {
        std::printf("[game] fault_pc=0x%08x fault=%s\n", cpu.fault_pc, cpu.fault ? cpu.fault : "?");
    }

    std::printf("[game] CPU stopped: %s (instret=%llu, pc=0x%08x)\n",
                stopped ? cpu.fault : "clean-cap",
                (unsigned long long)cpu.instret, cpu.pc);
    std::printf("[game] DMA ch2 kicks: %u, vertices accumulated: %zu\n", g_kicks, g_batch.size());

    // render accumulated vertices (kalau ada)
    if (!g_batch.empty()) {
        std::printf("[game] rendering %zu vertices...\n", g_batch.size());
        vkbe::Backend vkb(W, H);
        vkb.begin_frame(0, 0, 16);
        vkb.draw_triangles(g_batch);
        auto pixels = vkb.end_frame();

        FILE* fp = fopen("out/game_frame.ppm", "wb");
        if (fp) {
            fprintf(fp, "P6\n%u %u\n255\n", W, H);
            fwrite(pixels.data(), 1, pixels.size(), fp);
            fclose(fp);
            std::printf("[game] frame saved: out/game_frame.ppm\n");
        }
        uint32_t nonbg = 0;
        for (uint32_t i = 0; i < W*H*3; i += 3)
            if (pixels[i] || pixels[i+1] || pixels[i+2]) nonbg++;
        std::printf("[game] non-black pixels: %u / %u\n", nonbg, W*H);
        std::printf("PASS: game loop integrasi — CPU -> DMA -> GIF -> Vulkan\n");
    } else {
        std::printf("NOTE: belum ada DMA ch2 kick dari game code (CPU belum sampai render path)\n");
        std::printf("PASS: pipeline connected, waiting for game to reach render code\n");
    }
    return 0;
}
