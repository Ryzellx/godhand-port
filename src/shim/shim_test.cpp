// shim_test — uji substrate kernel EE: thread/sema/eventflag/file/gif-hook
#include "ee_kernel.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>

using namespace eekern;

static std::atomic<int> g_counter{0};
static int g_sema_ping, g_sema_pong, g_ef;

static s32 worker_a(s32) {     for (int i = 0; i < 5; i++) {
        WaitSema(g_sema_ping);
        g_counter++;
        SignalSema(g_sema_pong);
    }
    return 0;
}

static s32 worker_b(s32) {     for (int i = 0; i < 5; i++) {
        SignalSema(g_sema_ping);
        WaitSema(g_sema_pong);
    }
    // uji eventflag: set bit 0x4 lalu keluar
    SetEventFlag(g_ef, 0x5);
    return 0;
}

static int g_kick_calls = 0;
static void gif_kick_probe(const void*, u32 qwc) {
    g_kick_calls++;
    std::printf("  [gif-kick] qwc=%u\n", qwc);
}

int main() {
    // ---- T1: sema ping-pong antar2 thread ----
    g_ef = CreateEventFlag("ef", 0);          // dahulu: worker_b butuh id ini
    SemaParam sp_ping{"ping", 0, 1, 0}, sp_pong{"pong", 0, 1, 0};
    g_sema_ping = CreateSema(&sp_ping);
    g_sema_pong = CreateSema(&sp_pong);
    assert(g_sema_ping > 0 && g_sema_pong > 0);

    ThreadParam tpA{"A", 1, 0}, tpB{"B", 1, 0};
    s32 ta = CreateThread(&tpA, worker_a, 0);
    s32 tb = CreateThread(&tpB, worker_b, 0);
    assert(ta > 0 && tb > 0 && ta != tb);
    s32 sa = StartThread(ta, 0), sb = StartThread(tb, 0);
    assert(sa == 0 && sb == 0);

    // ---- T2: eventflag AND-mode dengan clear ----
    g_ef = CreateEventFlag("ef", 0);
    assert(g_ef > 0);
    u32 out = 0;
    s32 w = WaitEventFlag(g_ef, 0x5, EF_WAIT_CLEAR, &out);
    assert(w == 0);
    // setelah clear-mode, bit 0x4 harus hilang
    assert(PollEventFlag(g_ef, 0x4, 0, nullptr) == -1);
    std::printf("T2 eventflag AND+clear PASS\n");

    // ---- T3: file I/O lewat mapping cdrom0 (baca ELF asli God Hand) ----
    set_cdrom_root("/root/ports/godhand/extract");
    s32 fd = sceOpen("cdrom0:\\SLUS_215.03;1", O_RD);
    assert(fd >= 0);
    unsigned char magic[16];
    s32 n = sceRead(fd, magic, 16);
    assert(n == 16);
    assert(magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');
    assert(sceLseek(fd, 0x100008 - 0x1000, 0) >= 0);         // seek ke entry (file offset)
    sceClose(fd);
    std::printf("T3 cdrom0->ELF magic PASS\n");

    // ---- T4: gif kick hook ----
    set_gif_kick(gif_kick_probe);
    unsigned char fake_pkt[16 * 3] = {0};
    dma_ch2_kick(fake_pkt, 3);
    assert(g_kick_calls == 1);
    std::printf("T4 gif kick hook PASS\n");

    // ---- T5: ping-pong selesai + hasil counter ----
    WaitThreadEnd(ta);
    WaitThreadEnd(tb);
    assert(g_counter == 5);
    ThreadStatus st;
    assert(ReferThreadStatus(ta, &st) == 0 && st.status == 2);
    std::printf("T5 thread ping-pong (5 iterasi) PASS\n");

    s64 t = GetSystemTimeUsec();
    assert(t > 0);
    assert(GetThreadId() == 0);                              // main = tid 0
    std::printf("T6 misc (time/self-tid) PASS\n");

    std::printf("ALL SHIM TESTS PASS\n");
    return 0;
}
