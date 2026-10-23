// sif_rpc — Phase 8: SIF RPC host backend (pengganti IOP).
// Kontrak: request/response in-memory; paket menyimpan cmd/server/func + payload.
// Server bawaan: fileio (0x80000001), pad (0x80000121), cdvd (0x80000005).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sifrpc {

using u8 = uint8_t; using u32 = uint32_t; using s32 = int32_t; using u64 = uint64_t;

enum : u32 { SRV_FILEIO = 0x80000001u, SRV_PAD = 0x80000121u, SRV_CDVD = 0x80000005u };

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
// Return false kalau server tak dikenal / payload rusak.
bool call(u32 server, u32 func, const std::vector<u8>& req, std::vector<u8>& reply, s32& result_out);

// Helper codec.
std::vector<u8> encode(u32 server, u32 func, const std::vector<u8>& req);
bool decode(const std::vector<u8>& pkt, Header& h, std::vector<u8>& payload);

// Register default backends (fileio/pad/cdvd).
void install_defaults();

// Statistik nyata buat test/runtime.
struct Stats { u64 calls = 0, unknown = 0, bytes_in = 0, bytes_out = 0; };
const Stats& stats();

} // namespace sifrpc
