// sif_dma_test — Phase 8: uji jalur SifSetDma -> sifcmd/sifrpc -> backend host.
// Paket dibangun byte-exact mengikuti layout kanonik ps2sdk (SifCmdHeader_t,
// SifRpcBindPkt_t, SifRpcCallPkt_t) di RAM EE, lalu di-kick lewat syscall path.
#include "sif_dma.h"
#include "sif_rpc.h"
#include "ee_bus.h"
#include "ee_kernel.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sifdma;

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok, const char* detail = "") {
    std::printf("[%s] %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (ok) g_pass++; else g_fail++;
}

static void put(u32 addr, u32 v) { eebus::write32(addr, v); }
static u32 get(u32 addr) { return eebus::read32(addr); }

int main() {
    eebus::reset();
    sifrpc::reset();
    sifrpc::install_defaults();
    reset();
    set_verbose(4);

    const u32 PKT = 0x00100000, CD = 0x00110000, SDD = 0x00120000, RBUF = 0x00130000;
    eekern::SemaParam sp; sp.init = 0; sp.max = 1;
    s32 sema = eekern::CreateSema(&sp);

    // ---- BIND ke server fileio (cid 0x80000009)
    put(PKT + 0, 64);                 // psize = 64, dsize = 0
    put(PKT + 4, 0);                  // dest (IOP)
    put(PKT + 8, CID_RPC_BIND);
    put(PKT + 12, 0);                 // opt
    put(PKT + 16, 5);                 // rec_id = (rid<<16)|0x04|ALLOC
    put(PKT + 20, PKT | 0x20000000u); // pkt_addr (alias uncached, seperti game)
    put(PKT + 24, 2);                 // rpc_id
    put(PKT + 28, CD);
    put(PKT + 32, sifrpc::SRV_FILEIO);
    put(CD + 0, PKT | 0x20000000u);   // cd->hdr.pkt_addr
    put(CD + 4, 2);                   // cd->hdr.rpc_id
    put(CD + 8, (u32)sema);           // cd->hdr.sema_id
    put(CD + 12, 0);

    put(SDD + 0, PKT);                // src (EE)
    put(SDD + 4, 0);                  // dest (IOP 0)
    put(SDD + 8, 64);                 // size
    put(SDD + 12, 0x44);              // attr
    u32 id = kick(SDD, 1);
    check("kick bind -> dma id", id == 1, "");
    check("bind reply: rec_id bit1 (complete)", (get(PKT + 16) & 2u) != 0);
    check("bind reply: sema klien di-signal", eekern::PollSema(sema) >= 0);
    check("stats bind_replies=1", stats().bind_replies == 1);
    // Kontrak nyata SifDmaStat (dibuktikan kode game 0x2be3d8 `while (stat < 0)`):
    // poll pertama setelah kick masih melihat entri di antrean (>=0), poll berikutnya
    // -1 = entri pensiun/transfer selesai. -1 mutlak bikin loop itu tak pernah keluar
    // (terukur 15.693.109 syscall 118 di satu run 200 M).
    check("dma_stat poll-1 = entri masih antre (0)", dma_stat(id) == 0);
    check("dma_stat poll-2 = -1 (pensiun)", dma_stat(id) == 0xFFFFFFFFu);

    // ---- CALL fileio.open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY)
    const char* path = "cdrom0:\\SYSTEM.CNF;1";
    u32 plen = 4 + (u32)std::strlen(path) + 1 + 4;
    std::vector<u8> payload(plen);
    auto w32 = [&](size_t off, u32 v) { std::memcpy(&payload[off], &v, 4); };
    w32(0, (u32)std::strlen(path) + 1);
    std::memcpy(&payload[4], path, std::strlen(path) + 1);
    w32(4 + std::strlen(path) + 1, 1);        // flags: RDONLY

    eebus::mem_write(PKT + 64, payload.data(), (u32)payload.size());
    put(PKT + 0, 64);                          // psize (payload = extra DMA di +64)
    put(PKT + 8, CID_RPC_CALL);
    put(PKT + 16, 5);
    put(PKT + 20, PKT | 0x20000000u);
    put(PKT + 24, 3);
    put(PKT + 28, CD);
    put(PKT + 32, 0);                          // rpc_number: open
    put(PKT + 36, (u32)payload.size());        // send_size
    put(PKT + 40, RBUF);                       // recvbuf (EE)
    put(PKT + 44, 64);                         // recv_size
    put(PKT + 48, 0);
    put(PKT + 52, 0);
    // dua entry: paket 64 B, lalu payload
    put(SDD + 16, PKT + 64);
    put(SDD + 20, 64);
    put(SDD + 24, (u32)payload.size());
    put(SDD + 28, 0x44);
    kick(SDD, 2);
    s32 fd = (s32)get(RBUF);
    char d[96]; std::snprintf(d, sizeof(d), "fd=%d", fd);
    check("CALL open via DMA+RPC", fd >= 0, d);
    check("stats call_replies>=1", stats().call_replies >= 1);

    // ---- CALL fileio.read(fd, 57)
    put(PKT + 32, 2);                          // rpc_number: read
    put(PKT + 36, 8);                          // send_size
    put(PKT + 40, RBUF);
    put(PKT + 44, 64);
    put(PKT + 16, 5 | 0u);                     // rec_id reset (klien baru saja alloc)
    put(SDD + 0, PKT); put(SDD + 8, 64);
    put(SDD + 16, PKT + 64); put(SDD + 24, 8);
    put(PKT + 64, (u32)fd);
    put(PKT + 68, 57);
    kick(SDD, 2);
    u32 got = get(RBUF);
    std::vector<u8> rd(got < 256 ? got : 256);
    eebus::mem_read(RBUF + 4, rd.data(), (u32)rd.size());
    std::string text((const char*)rd.data(), rd.size());
    std::snprintf(d, sizeof(d), "got=%u bytes", got);
    check("read via DMA+RPC", got == 57, d);
    check("isi file = BOOT2 (dari ISO extract)", text.find("BOOT2") != std::string::npos);
    check("rec_id bit1 setelah call", (get(PKT + 16) & 2u) != 0);

    std::printf("stats: kicks=%llu entries=%llu bind=%llu call=%llu sema=%llu reply=%llu B\n",
                (unsigned long long)stats().kicks, (unsigned long long)stats().entries,
                (unsigned long long)stats().bind_replies, (unsigned long long)stats().call_replies,
                (unsigned long long)stats().sema_signals, (unsigned long long)stats().reply_bytes);
    std::printf("%s: %d/%d PASS\n", g_fail ? "FAIL" : "PASS", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
