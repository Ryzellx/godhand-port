// sif_dma.h — Phase 8: jembatan SifSetDma (syscall 119) -> sifrpc dispatcher.
// Model native: DMA EE<->IOP sinkron. IOP diemulasi sebagai RAM 2 MB (ukuran asli IOP),
// paket sifcmd/sifrpc di-decode dari layout kanonik ps2sdk:
//   SifCmdHeader_t { u32 psize:8, dsize:24; void* dest; int cid; u32 opt; }  (16 B)
//   SifRpcBindPkt_t  = header + {rec_id, pkt_addr, rpc_id, cd, sid}
//   SifRpcCallPkt_t  = header + {rec_id, pkt_addr, rpc_id, cd, rpc_number,
//                                send_size, recvbuf, recv_size, rmode, sd}
//   SifRpcRendPkt_t  = header + {rec_id, pkt_addr, rpc_id, cd, cid, sd, buf, cbuf}
// Verifikasi lapangan: paket nyata di 0x77e740 (cid 0x80000002 init, 0x80000009 bind).
#pragma once
#include <cstdint>
#include <string>

namespace sifdma {

using u8 = uint8_t; using u32 = uint32_t; using s32 = int32_t; using u64 = uint64_t;

// cid kanonik (sifcmd-common.h)
enum : u32 {
    CID_SET_SREG = 0x80000001u,
    CID_INIT_CMD = 0x80000002u,
    CID_RESET_CMD= 0x80000003u,
    CID_RPC_END  = 0x80000008u,
    CID_RPC_BIND = 0x80000009u,
    CID_RPC_CALL = 0x8000000Au,
    CID_RPC_RDATA= 0x8000000Cu,
};

struct Stats {
    u64 kicks = 0;            // panggilan SifSetDma
    u64 entries = 0;          // total sdd entry diproses
    u64 ee2iop = 0, iop2ee = 0;
    u64 ee2iop_bytes = 0, iop2ee_bytes = 0;
    u64 cid_init = 0, cid_reset = 0, cid_bind = 0, cid_call = 0, cid_end = 0, cid_sreg = 0;
    u64 cid_other = 0, pkt_short = 0;
    u64 bind_replies = 0, call_replies = 0, sema_signals = 0;
    u64 cb_queued = 0;   // end_func yang diantrekan (Phase 9, emulasi _request_end)
    u64 stat_pending = 0, stat_retired = 0;   // SifDmaStat: poll saat masih antre / sudah pensiun
    u64 unknown_server = 0, handler_fail = 0, autostub = 0;
    u64 payload_bytes = 0, reply_bytes = 0;
    u32 last_cid = 0, last_server = 0, last_func = 0, last_result = 0;
    u32 last_recvbuf = 0, last_pkt = 0;
};
const Stats& stats();

void reset();                        // RAM IOP + statistik + id DMA
u32  kick(u32 sdd_addr, u32 count);  // syscall 119: return dma id (0 = gagal)
u32  dma_stat(u32 id);               // syscall 118: -1 = selesai (transfer instan)

// Observasi (test): isi RAM IOP pada alamat tertentu, + dump N paket pertama.
bool iop_read(u32 addr, void* out, u32 n);
void set_verbose(int first_n_packets);   // dump heksadesimal N paket pertama ke stderr

} // namespace sifdma
