// sif_rpc — Phase 8: SIF RPC host backend (pengganti IOP).
// Kontrak: request/response in-memory; paket menyimpan cmd/server/func + payload.
// Server bawaan: fileio (0x80000001), pad (0x80000100), cdvd (0x80000005),
// mcserv (0x80000400), loadfile (0x80000006).
// SID diverifikasi dari modul IOP asli (extract/modules): padman.irx @0x7a80 lui/ori
// 0x80000100, mcman.irx @0x4300 & mcserv.irx @0x2e4 = 0x80000400, dan IOPRP310.IMG
// modul ROMDIR "LOADFILE" (index 12) = 0x80000006.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sifrpc {

using u8 = uint8_t; using u32 = uint32_t; using s32 = int32_t; using u64 = uint64_t;

enum : u32 {
    SRV_FILEIO   = 0x80000001u,   // FILEIO.IRX (IOPRP) @0x? -> RPC server fileio
    SRV_IOPHEAP  = 0x80000003u,   // heap IOP: sceSifAllocIopHeap/FreeIopHeap
    SRV_CDVD     = 0x80000005u,   // belum terverifikasi dari modul (kandidat CDVDFSV)
    SRV_LOADFILE = 0x80000006u,   // IOPRP LOADFILE — fno 255 = word versi "3100"
    SRV_MCSERV   = 0x80000400u,   // mcman.irx @0x4300 + mcserv.irx @0x2e4
    SRV_SJX      = 0x90000200u,   // SJX/CRI sound engine (libsjx + cri_adxi)
    SRV_PAD      = 0x80000100u,   // padman.irx @0x7a80 (0x80000101 = server kedua)
};

struct Header {
    u32 cmd = 0;       // 1 = call
    u32 server = 0;    // server id (lihat enum)
    u32 func = 0;      // fungsi di server
    u32 psize = 0;     // panjang payload request
    u32 dsize = 0;     // kapasitas buffer reply
    u32 result = 0;    // diisi saat reply
};

using Handler = std::function<s32(u32 func, const std::vector<u8>& req, std::vector<u8>& reply)>;

void reset();
bool bind(u32 server, const std::string& name, Handler h);
bool bound(u32 server);
std::string server_name(u32 server);

// Encode request jadi byte stream (header 24B + payload), lalu dispatch.
bool call(u32 server, u32 func, const std::vector<u8>& req, std::vector<u8>& reply, s32& result_out);

std::vector<u8> encode(u32 server, u32 func, const std::vector<u8>& req);
bool decode(const std::vector<u8>& pkt, Header& h, std::vector<u8>& payload);

void install_defaults();

struct Stats { u64 calls = 0, unknown = 0, bytes_in = 0, bytes_out = 0; };
const Stats& stats();
u64 loadfile_other_calls();   // fno 1..10 loadfile yang belum diimplementasi
u64 loadfile_load_count();    // fno 0: modul yang berhasil dibuka dari disk
u64 loadfile_load_fail_count();
const char* loadfile_modules_csv();

} // namespace sifrpc
