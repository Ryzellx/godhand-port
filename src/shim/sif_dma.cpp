// sif_dma.cpp — Phase 8: SifSetDma (syscall 119) -> packet sifcmd/sifrpc -> backend host.
// Semua angka di Stats berasal dari paket nyata yang lewat; tidak ada reply sintetis
// yang tidak diminta protokol. DMA dianggap selesai seketika (IOP native in-process).
#include "sif_dma.h"
#include "sif_rpc.h"
#include "ee_bus.h"
#include "ee_cpu.h"
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
// DUA RUANG ALAMAT BERBEDA — jangan disatukan:
//  * SD_STRUCT adalah `cd->server`, pointer yang di-dereference GAME di RAM EE (32 MB).
//    Game memuat ELF di vaddr 0x00100000, jadi ruang bebas EE = di atas bss 0x7867b4
//    dan di bawah stack 0x1FFFF00 -> 0xF00000. (Regresi terukur 2026-10-24: SD_STRUCT
//    sempat dipindah ke 0x00100000 = alamat load ELF, 16 B pertama image game tertimpa
//    struct SifRpcServerData -> boot mati, 0 bind.)
//  * SD_BUF/SD_CBUF adalah alamat RAM IOP (2 MB, model g_iop). Game meng-DMA payload
//    kiriman ke cd->buf, jadi alamat ini WAJIB < 0x200000 supaya iop_write() mendarat.
//    0xF00000 dulu di luar RAM IOP -> payload terbuang (terukur: fno=0 send=512, isi nol).
constexpr u32 SD_STRUCT = 0x00F00000u;                                  // EE (cd->server)
constexpr u32 SD_BUF = 0x00110000u, SD_CBUF = 0x00120000u;              // IOP (2 MB)
constexpr u32 SD_SIZE = 0x10000u;
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

// Riwayat tulis RAM IOP dari DMA. Dipakai memilih sumber payload RPC: satu-satunya
// bukti sah bahwa sebuah alamat berisi payload kiriman game adalah DMA yang menulisnya.
struct WriteRec { u32 addr, len; u64 seq; };
WriteRec g_wr[16]{};
int g_wr_n = 0;
u64 g_wr_seq = 0;

void iop_write(u32 addr, const u8* data, u32 n) {
    if (addr >= IOP_RAM_SIZE) return;
    if (addr + (u64)n > IOP_RAM_SIZE) n = IOP_RAM_SIZE - addr;
    std::memcpy(iop().data() + addr, data, n);
    g_wr[g_wr_n % 16] = WriteRec{addr, n, ++g_wr_seq};
    g_wr_n++;
}

// seq tulis terakhir yang MENUTUPI penuh [addr, addr+n); 0 = tidak ada.
u64 wr_seq_covering(u32 addr, u32 n) {
    if (!addr || !n) return 0;
    u64 best = 0;
    for (int i = 0; i < 16 && i < g_wr_n; i++) {
        const WriteRec& w = g_wr[i];
        if (w.seq > best && w.addr <= addr && w.addr + (u64)w.len >= addr + (u64)n) best = w.seq;
    }
    return best;
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
    // rec_id bit0 = "request masih jalan", bit1 = "selesai". Bukti semantik dari kode
    // game sendiri: is_busy() @0x3b3328 (dipakai wait @0x375930) kembali 1 selama
    // (rec_id & 1) != 0, jadi completion WAJIB clear bit0 — kalau tidak, wait-loop
    // berputar selamanya walau bit1 sudah set (terukur: 325.967.981 iterasi).
    eebus::write32(ee + 16, (rec_id & ~1u) | 2u);
    if (extra_cid) {
        eebus::write32(ee + 32, extra_cid);
        eebus::write32(ee + 36, sd);
        eebus::write32(ee + 40, buf);
        eebus::write32(ee + 44, cbuf);
    }
    if (cd) {
        // sceSifCheckStatRpc (0x3B3328) mengembalikan 1 (= masih jalan) selama
        //   cd->command(cd+4) == sd->[0x18]  DAN  (sd->[0x10] & 1).
        // Di HW asli _request_end() menyalin command ke server-data dan membersihkan
        // bit "processing" itu. Tanpa ini libcdvd menunggu selamanya di 0x398DD8
        // (loop: CheckStatRpc != 0 -> delay 4000) walau paket sudah dibalas.
        u32 c = cd & 0x1FFFFFFFu;
        u32 sdp = eebus::read32(c + 0);
        if (sdp) {
            eebus::write32(sdp + 0x18, eebus::read32(c + 4));       // command tercatat
            eebus::write32(sdp + 0x10, eebus::read32(sdp + 0x10) & ~1u);  // clear busy
        }
        s32 sema = (s32)eebus::read32(c + 8);
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
    std::memset(g_wr, 0, sizeof(g_wr));
    g_wr_n = 0; g_wr_seq = 0;
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
        // Arah: sdd dari SifSetDma (sisi EE) umumnya EE->IOP. IOP->EE hanya kalau
        // src jelas alamat IOP (< 2 MB) DAN dst jelas alamat EE (>= 2 MB).
        // Heuristik lama (dst >= 1 MB -> IOP->EE) salah begitu buffer SIF kita
        // sendiri ada di 0x110000: payload kiriman game jadi dibuang (terukur:
        // send=512 tapi isi nol).
        e.ee2iop = !(e.src < 0x00200000u && e.dst >= 0x00200000u);
        if (getenv("GH_SIF_DUMP") && e.size >= 256) {
            for (u32 k = 0; k < 4; k++)
                std::fprintf(stderr, "[raw] kick=%llu entry%d w%d=0x%08x\n",
                             (unsigned long long)g_st.kicks, i, k, eebus::read32(a + k * 4));
            unsigned char eb[64];
            if (eebus::mem_read(e.src, eb, 64)) {
                std::fprintf(stderr, "[raw] EE@0x%08x:", e.src);
                for (int k = 0; k < 48; k++) std::fprintf(stderr, "%02x", eb[k]);
                std::fprintf(stderr, "\n");
            }
        }
        if (getenv("GH_SIF_DUMP") && e.size >= 64)
            std::fprintf(stderr, "[sdd] kick=%llu i=%u src=0x%08x dst=0x%08x size=%u %s\n",
                         (unsigned long long)g_st.kicks, i, e.src, e.dst, e.size,
                         e.ee2iop ? "EE->IOP" : "IOP->EE");
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
            if (getenv("GH_SIF_DUMP"))
                std::fprintf(stderr, "[sifdma] BIND sid=0x%08x (%s) rpc_id=%u cd=0x%08x rec_id=0x%x caller=0x%08x -> reply\n",
                             sid, sifrpc::server_name(sid).c_str(), rpc_id, cd, rec_id, eecpu::g_syscall_pc);
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
            // Send-data RPC tidak selalu menempel di packet+64: game meng-DMA
            // payload ke cd->buf (alamat yang kita balas saat BIND). Terukur di
            // trace: entry DMA terpisah src=EE buf -> dst=cd->buf, size=send_size.
            u32 sendbuf = cd ? eebus::read32((cd & 0x1FFFFFFFu) + 20) : 0;
            std::vector<u8> payload;
            if (send_size) {
                payload.resize(send_size);
                // Dua kandidat, keduanya sah di HW: payload menempel di packet+64
                // (sceSifSendCmd) atau di cd->buf hasil DMA terpisah. Pilih yang
                // benar-benar ditulis DMA paling akhir — bukan tebakan urutan.
                u32 cand_cd = sendbuf, cand_pkt = e.dst + 64;
                u64 sc = wr_seq_covering(cand_cd, send_size);
                u64 sp = wr_seq_covering(cand_pkt, send_size);
                u32 use = (sc || sp) ? (sc >= sp ? cand_cd : cand_pkt) : cand_pkt;
                const char* src_used = (use == cand_cd) ? "cd->buf(dma)" : "pkt+64(dma)";
                if (!iop_copy_out(use, payload.data(), send_size)) {
                    src_used = "pkt+64(raw)";
                    if (!iop_copy_out(cand_pkt, payload.data(), send_size)) payload.clear();
                }
                if (getenv("GH_SIF_DUMP") && send_size <= 64) {
                    std::fprintf(stderr, "[call] sid=0x%08x fno=%u payload(%zu):", sid, fno, payload.size());
                    for (size_t k = 0; k < payload.size() && k < 64; k++) std::fprintf(stderr, " %02x", payload[k]);
                    std::fprintf(stderr, "\n");
                }
                if (const char* want = getenv("GH_DUMP_SID")) {
                    u32 w = (u32)std::strtoul(want, nullptr, 0);
                    if (sid == w) {
                        std::fprintf(stderr, "[payload sid=%08x fno=%u n=%zu] ", sid, fno, payload.size());
                        for (size_t k = 0; k < payload.size() && k < 192; k++) {
                            u8 ch = payload[k];
                            std::fputc((ch >= 32 && ch < 127) ? ch : '.', stderr);
                        }
                        std::fprintf(stderr, "\n");
                    }
                }
                if (getenv("GH_SIF_DUMP"))
                    std::fprintf(stderr, "[call] payload dari %s (cd->buf=0x%08x seq=%llu, pkt+64=0x%08x seq=%llu) n=%zu\n",
                                 src_used, sendbuf, (unsigned long long)sc, cand_pkt, (unsigned long long)sp, payload.size());
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
            // Diagnosa loadfile: payload = struct {u32 size; char name[252]; ...}
            if (getenv("GH_SIF_DUMP") && send_size >= 256 && !payload.empty()) {
                static int dc = 0;
                char fn[64]; std::snprintf(fn, sizeof fn, "/tmp/sif_pkt_%d.bin", dc++);
                FILE* f = std::fopen(fn, "wb");
                if (f) { std::fwrite(iop().data() + e.dst, 1, 64 + send_size, f); std::fclose(f); }
            }
            if (sid == 0x80000006u && !payload.empty()) {
                char nm[64] = {0};
                size_t k = 0;
                for (size_t o = 8; o < payload.size() && k < 63 && payload[o]; o++) nm[k++] = (char)payload[o];
                std::fprintf(stderr, "[loadfile] fno=%u size=%zu name@+8=\"%s\" words:", fno, payload.size(), nm);
                for (size_t w = 0; w + 4 <= payload.size() && w < 40; w += 4)
                    std::fprintf(stderr, " %08x", (unsigned)(payload[w] | (payload[w+1] << 8) |
                                 (payload[w+2] << 16) | ((unsigned)payload[w+3] << 24)));
                std::fprintf(stderr, "\n");
            }
            if (recvbuf && !reply.empty()) {
                u32 n = (u32)reply.size();
                if (recv_size && n > recv_size) n = recv_size;
                eebus::mem_write(recvbuf, reply.data(), n);
                g_st.reply_bytes += n;
            }
            finish_request(pkt_addr, cd, 0, 0, 0, 0);
            g_st.call_replies++;
            // end_func: sceSifCallRpc menyimpan end_func di cd+28 dan end_param di cd+32
            // (terbaca di packet-builder 0x3B31A0..0x3B31C8: `sw t3,28(s1)` / `sw arg9,32(s1)`).
            // HW asli memanggilnya dari _request_end di interrupt SIF0; native: antrean
            // callback yang dieksekusi run-loop (lihat eecpu::request_callback).
            if (cd) {
                u32 cc = cd & 0x1FFFFFFFu;
                u32 endf = eebus::read32(cc + 28);
                if (endf) {
                    eecpu::request_callback(endf, eebus::read32(cc + 32), (u32)result);
                    g_st.cb_queued++;
                }
            }
            if (getenv("GH_SIF_DUMP"))
            {
                std::fprintf(stderr, "[sifdma] CALL sid=0x%08x fno=%u send=%u recv_cap=%u -> result=%d reply=%zu caller=0x%08x\n",
                             sid, fno, send_size, recv_size, result, reply.size(), eecpu::g_syscall_pc);
                std::fprintf(stderr, "   pkt:");
                for (u32 k = 0; k < 64; k += 4) std::fprintf(stderr, " %u:%08x", k, rd(p, k));
                std::fprintf(stderr, "\n");
            }
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
