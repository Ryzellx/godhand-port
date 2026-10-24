// memcard.h — Phase 8: backend memory card PS2 (MCMAN/MCSERV) berbasis direktori host.
// Mapping: "mc0:/BASLUS-21503/SYS" -> <root>/mc0/BASLUS-21503/SYS
//          "mc1:/..."              -> <root>/mc1/...
// Default root: $HOME/.local/share/godhand-launcher/memcard (env GH_MEMCARD_DIR).
// Kartu PS2 dimodelkan: 128 KB, cluster 8 KB, 15 cluster terpakai-able (1 page sistem).
// CATATAN JUJUR: layout FAT/ECC mentah kartu asli TIDAK direproduksi; yang dilayani
// adalah permukaan API mcman/mcserv (open/read/write/getinfo/mkdir/remove).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace memcard {

using u8 = uint8_t; using u32 = uint32_t; using s32 = int32_t; using u64 = uint64_t;

inline constexpr u32 CARD_BYTES = 128u * 1024u;
inline constexpr u32 CLUSTER = 8192u;
inline constexpr u32 TOTAL_CLUSTERS = 15u;   // 16 page - 1 page sistem

struct Info {
    u32 total_clusters = TOTAL_CLUSTERS;
    u32 used_clusters = 0;
    u32 free_clusters = TOTAL_CLUSTERS;
    u32 free_bytes = TOTAL_CLUSTERS * CLUSTER;
    bool formatted = true;      // kartu host selalu "siap" setelah init
};

struct Stats {
    u64 opens = 0, reads = 0, writes = 0, closes = 0;
    u64 bytes_read = 0, bytes_written = 0;
    u64 mkdirs = 0, removes = 0, denied = 0, notfound = 0;
};
const Stats& stats();

bool init(const std::string& dir = "");      // "" -> env GH_MEMCARD_DIR / default
const std::string& root();
bool is_mc_path(const std::string& ps2);     // "mc0:" / "mc1:" prefix

// resolve + validasi; create=true -> komponen terakhir boleh belum ada (open CREAT)
bool resolve(const std::string& ps2, std::string& out, bool* is_dir = nullptr, bool create = false);

s32 open(const std::string& ps2, u32 flags);       // flags sama hostio: 1=RD 2=WR 3=RDWR 0x200=CREAT 0x400=TRUNC
u32 read(s32 fd, void* buf, u32 n);
u32 write(s32 fd, const void* buf, u32 n);
u32 lseek(s32 fd, s32 off, u32 whence);
u32 close(s32 fd);
u32 size(s32 fd);
bool stat(const std::string& ps2, u32& size_out, u32& mode_out);
std::vector<std::string> listdir(const std::string& ps2_dir);
bool mkdir(const std::string& ps2_dir);
bool remove_file(const std::string& ps2);
Info getinfo();

} // namespace memcard
