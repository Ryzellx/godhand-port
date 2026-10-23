// sif_dma.cpp — Phase 8: SifSetDma (syscall 119) -> packet sifcmd/sifrpc -> backend host.
// Semua angka di Stats berasal dari paket nyata yang lewat; tidak ada reply sintetis
// yang tidak diminta protokol. DMA dianggap selesai seketika (IOP native in-process).
#include "sif_dma.h"
#include "sif_rpc.h"
#include "ee_bus.h"
#include "ee_kernel.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace sifdma {
namespace {

constexpr u32 IOP_RAM_SIZE = 2u * 1024u * 1024u;   // IOP asli: 2 MB
constexpr u32 MAX_XFER = 8u * 1024u * 1024u;       // guard alokasi

std::vector<u8> g_iop;
Stats g_st;
u32 g_last_id = 0;
int g_verbose = 0;
int g_dumped = 0;
// SifRpcServerData palsu (dipakai balasan bind) di RAM EE bebas: bss game berakhir
// 0x7867b4, stack 0x1FFFF00 -> 0xF00000..0xF2FFFF aman.
constexpr u32 SD_STRUCT = 0x00F00000u, SD_BUF = 0x00F10000u, SD_CBUF = 0x00F20000u, SD_SIZE = 0x10000u;
std::map<u32, u32> g_cd_server;
bool g_autostub = true;   // auto-bind stub utk sid tanpa backend      // SifRpcClientData* -> sid (dari BIND)
u32 g_sreg[16]{};                    // SifCmdSRegData index/value (SET_SREG)

std::vector<u8>& iop() { if (g_iop.empty()) g_iop.assign(IOP_RAM_SIZE, 0); return g_iop; }

u32 rd(const u8* p, u32 off) {
    return (u32)p[off] | ((u32)p[off + 1] << 8) | ((u32)p[off + 2] << 16) | ((u32)p[off + 3] << 24);
}
void wr(u8* p, u32 off, u32 v) {
    p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); p[off + 2] = (u8)(v >> 16); p[off + 3] = (u8)(v >> 24);
}

void iop_write(u32 addr, const u8* data, u32 n) {
    if (addr >= IOP_RAM_SIZE) return;
    if (addr + (u64)n > IOP_RAM_SIZE) n = IOP_RAM_SIZE - addr;
    std::memcpy(iop().data() + addr, data, n);
}

bool iop_copy_out(u32 addr, u8* out, u32 n) {
    if (addr >= IOP_RAM_SIZE) return false;
    if (addr + (u64)n > IOP_RAM_SIZE) n = IOP_RAM_SIZE - addr;
    std::memcpy(out, iop().data() + addr, n);
    return true;
}

void iop_set_rpc_ready() {
    eekern::SifSetReg(4, eekern::sif_reg_peek(4) | 0x00040000u);
}

void dump_packet(const char* tag, u32 cid, const u8* p, u32 len) {
    if (g_dumped >= g_verbose) return;
    g_dumped++;
    std::fprintf(stderr, "[sifdma] %s cid=0x%08x len=%u bytes=", tag, cid, len);
    for (u32 i = 0; i < len && i < 64; i++) std::fprintf(stderr, "%02x", p[i]);
    std::fprintf(stderr, "\n");
}

// Kirim "rend" (reply) packet ke EE: rec_id bit1 = selesai (sceSifCheckStatRpc), lalu
// signal sema klien (cd->hdr.sema_id di offset 8) — model iSignalSema di _request_end.
void finish_request(u32 pkt_addr, u32 cd, u32 extra_cid, u32 sd, u32 buf, u32 cbuf) {
    u32 ee = pkt_addr & 0x1FFFFFFFu;
    u32 rec_id = eebus::read32(ee + 16);
    eebus::write32(ee + 16, rec_id | 2u);
    if (extra_cid) {
        eebus::write32(ee + 32, extra_cid);
        eebus::write32(ee + 36, sd);
        eebus::write32(ee + 40, buf);
        eebus::write32(ee + 44, cbuf);
    }
    if (cd) {
        s32 sema = (s32)eebus::read32((cd & 0x1FFFFFFFu) + 8);
        if (sema >= 0 && sema < 256) {
            eekern::SignalSema(sema);
            g_st.sema_signals++;
        }
    }
    g_st.last_pkt = ee;
}

} // namespace

const Stats& stats() { return g_st; }

void reset() {
    g_iop.clear();
    g_st = Stats{};
    g_last_id = 0;
    g_dumped = 0;
    g_cd_server.clear();
    std::memset(g_sreg, 0, sizeof(g_sreg));
}

void set_verbose(int n) { g_verbose = n; g_dumped = 0; }

bool iop_read(u32 addr, void* out, u32 n) {
    if (g_iop.empty()) return false;
    return iop_copy_out(addr, (u8*)out, n);
}

u32 dma_stat(u32) { return 0xFFFFFFFFu; }   // -1: tidak ada transfer aktif (instan)

u32 kick(u32 sdd_addr, u32 count) {
    g_st.kicks++;
    if (count > 64) count = 64;
    struct Ent { u32 src, dst, size, attr; bool ee2iop; };
    std::vector<Ent> ents;
    ents.reserve(count);
    std::vector<u8> tmp;

    // Pass 1: semua transfer DMA. Wajib mendahului decode: payload RPC datang di
    // entry berikutnya (sceSifSendCmd mengirim paket 64 B lalu extra data), jadi
    // paket baru boleh diproses setelah seluruh entry mendarat di RAM IOP.
    for (u32 i = 0; i < count; i++) {
        u32 a = sdd_addr + i * 16;
        Ent e{eebus::read32(a + 0), eebus::read32(a + 4), eebus::read32(a + 8), eebus::read32(a + 12), true};
        if (!e.size) continue;
        g_st.entries++;
        if (e.size > MAX_XFER) e.size = MAX_XFER;
        if (e.src >= 0x20000000u) e.ee2iop = true;               // alias uncached EE
        else if (e.dst >= 0x00100000u) e.ee2iop = false;         // tujuan window EE -> IOP->EE
        else e.ee2iop = true;                                     // default EE->IOP
        tmp.resize(e.size);
        if (e.ee2iop) {
            if (!eebus::mem_read(e.src, tmp.data(), e.size)) { g_st.pkt_short++; continue; }
            iop_write(e.dst, tmp.data(), e.size);
            g_st.ee2iop++; g_st.ee2iop_bytes += e.size;
            ents.push_back(e);
        } else {
            if (!iop_copy_out(e.src, tmp.data(), e.size)) { g_st.pkt_short++; continue; }
            eebus::mem_write(e.dst, tmp.data(), e.size);
            g_st.iop2ee++; g_st.iop2ee_bytes += e.size;
        }
    }

    // Pass 2: decode paket sifcmd dari salinan di RAM IOP.
    for (const Ent& e : ents) {
        const u8* p = iop().data() + e.dst;
        u32 hdr = rd(p, 0);
        u32 psize = hdr & 0xFFu;
        u32 cid = rd(p, 8);
        if (psize < 16 || cid < 0x80000000u) continue;
        g_st.last_cid = cid;
        dump_packet("pkt", cid, p, psize);

        switch (cid) {
        case CID_INIT_CMD: {
            g_st.cid_init++;
            // IOP (model in-process) menyelesaikan init -> set bit "RPC siap"
            // 0x40000 di SifReg 4 (di HW asli IOP memanggil SifSetReg). Game poll
            // 0x3b6250 `and v0,0x40000` — tanpa bit ini spin 93 M call.
            iop_set_rpc_ready();
            break;
        }
        case CID_RESET_CMD: {
            g_st.cid_reset++;
            iop_set_rpc_ready();
            break;
        }
        case CID_SET_SREG: {
            g_st.cid_sreg++;
            u32 idx = rd(p, 16), val = rd(p, 20);
            if (idx < 16) g_sreg[idx] = val;
            break;
        }
        case CID_RPC_BIND: {
            g_st.cid_bind++;
            u32 rec_id = rd(p, 16), pkt_addr = rd(p, 20), rpc_id = rd(p, 24);
            u32 cd = rd(p, 28), sid = rd(p, 32);
            g_st.last_server = sid;
            g_st.last_func = 0;
            if (pkt_addr == 0) pkt_addr = e.src;
            g_cd_server[cd] = sid;
            if (!sifrpc::bound(sid)) {
                g_st.unknown_server++;
                // Server IOP yang belum punya backend (mis. 0x80000592/0x80000006):
                // auto-bind stub 4-byte nol supaya handshake tidak menggantung. Ini
                // MODEL, bukan implementasi modul asli — dicatat di stats.autostub.
                if (g_autostub) {
                    sifrpc::bind(sid, "autostub", [](u32, const std::vector<u8>&, std::vector<u8>& rep) {
                        rep.assign(4, 0);
                        return 0;
                    });
                    g_st.autostub++;
                }
            }
            // Reply bind wajib membawa sd/buf/cbuf: klien EE menolak bind kalau
            // cd->server == NULL (terukur: 32x bind-retry ke sid 0x80000592).
            eebus::write32(SD_STRUCT + 0, sid);
            eebus::write32(SD_STRUCT + 8, SD_BUF);
            eebus::write32(SD_STRUCT + 12, SD_SIZE);
            eebus::write32(SD_STRUCT + 24, SD_CBUF);
            eebus::write32(SD_STRUCT + 28, SD_SIZE);
            // Di HW asli, handler _request_end() di context interrupt menyalin hasil
            // bind ke SifRpcClientData: cd->server=sd, cd->buf, cd->cbuf. Tanpa ini
            // cd->server tetap 0 -> game mengulang bind (terukur 32x lalu stall).
            if (cd) {
                u32 c = cd & 0x1FFFFFFFu;
                eebus::write32(c + 20, SD_BUF);
                eebus::write32(c + 24, SD_CBUF);
                eebus::write32(c + 36, SD_STRUCT);
            }
            finish_request(pkt_addr, cd, CID_RPC_BIND, SD_STRUCT, SD_BUF, SD_CBUF);
            g_st.bind_replies++;
            std::fprintf(stderr, "[sifdma] BIND sid=0x%08x (%s) rpc_id=%u cd=0x%08x rec_id=0x%x -> reply\n",
                         sid, sifrpc::server_name(sid).c_str(), rpc_id, cd, rec_id);
            break;
        }
        case CID_RPC_CALL: {
            g_st.cid_call++;
            u32 pkt_addr = rd(p, 20), cd = rd(p, 28);
            u32 fno = rd(p, 32), send_size = rd(p, 36);
            u32 recvbuf = rd(p, 40), recv_size = rd(p, 44);
            if (pkt_addr == 0) pkt_addr = e.src;
            auto it = g_cd_server.find(cd);
            u32 sid = it == g_cd_server.end() ? 0 : it->second;
            g_st.last_server = sid;
            g_st.last_func = fno;
            g_st.last_recvbuf = recvbuf;
            if (send_size > MAX_XFER) send_size = MAX_XFER;
            std::vector<u8> payload;
            if (send_size) {
                payload.resize(send_size);
                if (!iop_copy_out(e.dst + 64, payload.data(), send_size)) payload.clear();
            }
            g_st.payload_bytes += payload.size();
            std::vector<u8> reply;
            s32 result = -1;
            if (sid && sifrpc::call(sid, fno, payload, reply, result)) {
                if (result < 0) g_st.handler_fail++;
            } else {
                g_st.unknown_server++;
                reply.clear();
            }
            g_st.last_result = (u32)result;
            if (recvbuf && !reply.empty()) {
                u32 n = (u32)reply.size();
                if (recv_size && n > recv_size) n = recv_size;
                eebus::mem_write(recvbuf, reply.data(), n);
                g_st.reply_bytes += n;
            }
            finish_request(pkt_addr, cd, 0, 0, 0, 0);
            g_st.call_replies++;
            std::fprintf(stderr, "[sifdma] CALL sid=0x%08x fno=%u send=%u recv_cap=%u -> result=%d reply=%zu\n",
                         sid, fno, send_size, recv_size, result, reply.size());
            break;
        }
        case CID_RPC_END: g_st.cid_end++; break;
        default: g_st.cid_other++; break;
        }
    }
    // Model INTC SIF0 "transfer complete": bit0 flag sifcmd EE (0x0077E6C0) di-set
    // setelah DMA mendarat + handler jalan. Tanpa ini game poll di 0x3b205c selamanya
    // (terukur: 2 B inst berhenti, 0 bind).
    if (g_st.entries) {
        u32 flag = eebus::read32(0x0077E6C0u);
        eebus::write32(0x0077E6C0u, flag | 1u);
    }
    return ++g_last_id;
}

} // namespace sifdma
