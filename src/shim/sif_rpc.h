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
    SRV_CDVD     = 0x80000592u,   // CDVDFSV / cdvdman — SID kanonik ps2sdk; dikonfirmasi
                                  // dari klien libcdvd game: bind di 0x3990C8 (jal 0x3b2f48)
                                  // lalu sceSifCallRpc fno 0 (4 B -> 16 B reply) di 0x399168
    SRV_LOADFILE = 0x80000006u,   // IOPRP LOADFILE — fno 255 = word versi "3100"
    SRV_MCSERV   = 0x80000400u,   // mcman.irx @0x4300 + mcserv.irx @0x2e4
    SRV_SJX      = 0x90000200u,   // SJX/CRI sound engine (libsjx + cri_adxi)
    SRV_PAD      = 0x80000100u,   // padman.irx @0x7a80 (0x80000101 = server kedua)
    SRV_CDVD_EE  = 0x80000595u,   // cdvdfsv.irx server kedua: register 0x44a4 ->
                                  // FUN_00003c1c "sce_cdvd N cmd", jump table .data 0x53d0.
                                  // fno 1 = Read_EE (baca sektor langsung ke RAM EE).
    SRV_CDVDFS   = 0x80000597u,   // cdvdfsv.irx (IOPRP310 m09.elf): file system CD
                                  // (open/read file disc). Dispatch server ASLI by size
                                  // paket (300 = open), bukan fno — lihat cdvfs_handler.
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

// DMA balik IOP->EE. Server CD (cdvdfsv) menulis struct hasil langsung ke buffer client
// di EE: paket 300 B menyimpan pointer EE-nya sendiri di +292. Di HW itu DMA IOP->EE;
// di sini antrean satu-slot yang dieksekusi sifdma setelah handler selesai.
struct EeWrite { u32 addr = 0; std::vector<u8> data; };
void ee_writeback(u32 addr, const std::vector<u8>& d);
bool take_ee_writeback(EeWrite& out);

u64 cdvfs_open_ok();
u64 cdvfs_open_miss();
u64 cdvfs_other_calls();

// Read_EE (SID 0x80000595 fno 1) — jalur baca sektor mentah ke RAM EE.
u64 cdvd_read_ok();
u64 cdvd_read_fail();
u64 cdvd_read_bytes();
u64 cdvd_other_calls();   // fno lain di SID 0x80000595 (mis. 14 = cek drive)

struct Stats { u64 calls = 0, unknown = 0, bytes_in = 0, bytes_out = 0; };
const Stats& stats();
u64 loadfile_other_calls();   // fno 1..10 loadfile yang belum diimplementasi
u64 loadfile_load_count();    // fno 0: modul yang berhasil dibuka dari disk
u64 loadfile_load_fail_count();
const char* loadfile_modules_csv();

} // namespace sifrpc
