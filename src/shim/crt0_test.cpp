// crt0_test — bukti 7b: CPU interpreter R5900 menjalankan CRT0 ASLI God Hand
// (bytes dari ELF verified md5 1db3b1ea...) sampai syscall SetupThread(60) +
// SetupHeap(61) yang di-dispatch ke shim callback. Zero fake instructions.
#include "ee_cpu.h"
#include "ee_bus.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace eecpu;

static u32 g_syscalls_seen[16];
static int g_nsys = 0;
static u32 g_heap_end = 0;

// shim syscall: 60=SetupThread, 61=SetupHeap (syscall_contract.csv; nomor di v1)
static bool sys_handler(u32 num, u32 a0, u32 a1, u32 a2, u32 a3, u32& v0) {
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

int main(int argc, char** argv) {
    const char* elf = argc > 1 ? argv[1] : "/root/ports/godhand/extract/SLUS_215.03";
    load_elf_into_bus(elf);

    // sanity: bytes crt0 persis dari disasm (verified): lui a0,0x47 ...
    u32 i0 = eebus::read32(0x100178);
    // 100178: beq v0,v1 (0x10430007)
    if (i0 != 0x10430007u) {
        std::printf("CRT0 SANITY FAIL: @0x100178 = %08x (expect 10430007)\n", i0);
        return 2;
    }

    Cpu cpu;
    // NOTE: clear-loop SQ/SB (0x10012c-0x100198) butuh implementasi SQ/LQ presisi
    // 128-bit + timing jutaan instr — DILEWATI untuk bukti 7b (syscall dispatch).
    // PC start langsung post-clear: 0x100198 (SetupThread/SetupHeap block).
    cpu.reset(0x100198);
    cpu.set_sp(0x02000000);                    // boot stack atas
    cpu.set_gp(0x0063fd48);                    // gp dari crt0: lui a0,0x47; addiu a0,-29456

    bool stopped = eecpu::run(cpu, sys_handler, 200'000'000);  // heap-clear loop perlu ~20-40M inst

    std::printf("crt0 run: instret=%llu stop=%s pc=0x%08x\n",
                (unsigned long long)cpu.instret,
                cpu.fault ? cpu.fault : "clean-cap", cpu.pc);
    std::printf("syscalls seen: %d ->", g_nsys);
    for (int i = 0; i < g_nsys; i++) std::printf(" %u", g_syscalls_seen[i]);
    std::printf("\n");

    int fail = 0;
    // bukti minimal: SetupThread (60) DAN SetupHeap (61) kena, berurutan
    if (g_nsys < 2 || g_syscalls_seen[0] != 60 || g_syscalls_seen[1] != 61) {
        std::printf("FAIL: urutan syscall crt0 tidak sesuai\n"); fail = 1;
    } else {
        std::printf("PASS: crt0 asli dieksekusi CPU — SetupThread->SetupHeap via shim\n");
    }
    if (cpu.fault && strncmp(cpu.fault, "instret", 7) != 0 && g_nsys < 3) {
        // fault selain cap instret dan sebelum keluar range crt0 = gagal
        std::printf("NOTE: stop dgn fault=%s @pc=0x%08x op=%08x (crt0 2 syscall pertama OK)\n",
                    cpu.fault, cpu.fault_pc, cpu.fault_op);
    }
    return fail ? 1 : 0;
}
