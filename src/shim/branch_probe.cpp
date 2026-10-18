// scratch probe: uji semantik branch/jal delay-slot ee_cpu vs MIPS kanonik.
// Layout di RAM 0x1000:
//  1000: beq zero,zero,off=3 -> kanonik target 0x100C (jal), overshoot (+4) -> 0x1010
//  1004: addiu t0,zero,0x111   (delay slot — selalu dieksekusi)
//  1008: addiu t0,zero,0x222   (harus SKIPPED)
//  100C: jal 0x1020            (ra kanonik = 0x1014; overshoot = 0x1018)
//  1010: nop                   (delay jal)
//  1014: addiu t1,zero,0x333   (return site kanonik)
//  1018: addiu t1,zero,0x444   (execute HANYA kalau ra salah +4)
//  101C: syscall (v1=200 -> unknown -> stop)
//  1020: addiu t2,zero,0x555   (callee)
//  1024: nop
//  1028: jr ra                 (delay 102C nop)
#include "ee_cpu.h"
#include "ee_bus.h"
#include <cstdio>
using namespace eecpu;
static bool h(u32 n,u32,u32,u32,u32,u32& v0){ v0=0; return n==200?false:true; }
int main(){
    eebus::reset();
    auto put=[&](u32 a,u32 w){ eebus::write32(a,w); };
    put(0x1000,0x10400002u);            // beq zero,zero,+2 -> kanonik target 0x1000+4+8=0x100C
    put(0x1004,0x24080111u);            // li t0,0x111
    put(0x1008,0x24080222u);            // li t0,0x222  (skip)
    put(0x100C,0x0C000408u);            // jal 0x1020
    put(0x1010,0x00000000u);            // nop (delay)
    put(0x1014,0x24090333u);            // li t1,0x333 — return site kanonik (ra=0x1014)
    put(0x1018,0x0000000Cu);            // syscall -> stop (v1=200); DILEWATI kalau ra salah
    put(0x1020,0x240A0555u);            // li t2,0x555 (callee)
    put(0x1024,0x00000000u);
    put(0x1028,0x03E00008u);            // jr ra
    put(0x102C,0x00000000u);            // nop (delay)
    Cpu c; c.reset(0x1000); c.set_sp(0x200000); c.set(3,200);
    run(c,h,1000);
    u32 t0=c.get(8), t1=c.get(9), t2=c.get(10);
    std::printf("stop=%s fault=%s pc=0x%08x instret=%llu\n", c.fault?"fault":"cap",
                c.fault?c.fault:"-", c.pc,(unsigned long long)c.instret);
    std::printf("t0=0x%03x t1=0x%03x t2=0x%03x\n",t0,t1,t2);
    bool ok = (t0==0x111) && (t1==0x333) && (t2==0x555);
    std::printf("%s: branch-target + delay-slot + jal-ra + jr-return %s\n",
                ok?"PASS":"FAIL", ok?"kanonik MIPS":"DEVIASI");

    // FASE 2 — BTB census pada loop mundur terkontrol:
    //  1000 li t0,3 · 1004 addiu t0,-1 · 1008 bne t0,zero -> 0x1004 · 100C nop · 1010 syscall
    eebus::reset();
    put(0x1000,0x24080003u);
    put(0x1004,0x2508FFFFu);
    put(0x1008,0x1500FFFEu);            // bne t0,zero off=-2 -> kanonik 0x1004
    put(0x100C,0x00000000u);
    put(0x1010,0x0000000Cu);            // syscall stop
    static u64 btb[1u<<21]{};
    Cpu c2; c2.reset(0x1000); c2.set(3,200); c2.btb = btb;
    run(c2,h,1000);
    u64 n8=btb[0x1008>>2], nC=btb[0x100C>>2], n4=btb[0x1004>>2];
    std::printf("BTB: @0x1008(branch)=%llu @0x100C(delay)=%llu @0x1004(other)=%llu instret=%llu\n",
                (unsigned long long)n8,(unsigned long long)nC,(unsigned long long)n4,
                (unsigned long long)c2.instret);
    bool ok2 = (n8==2) && (nC==0) && (c2.instret==10);
    std::printf("%s: backward-branch census %s\n", ok2?"PASS":"FAIL",
                ok2?"tercatat persis di alamat branch (2 taken, instret=10)":"salah atribusi/jumlah");
    return (ok && ok2)?0:1;
}
