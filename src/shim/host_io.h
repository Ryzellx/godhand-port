// host_io — Phase 8: host filesystem backend untuk path PS2 (cdrom0:, host:, mc0:).
// Mapping: "cdrom0:\\DIR\\FILE.EXT;1" -> <root>/DIR/FILE.EXT (case-insensitive).
// Root standar: /root/ports/godhand/extract (hasil unpack ISO).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hostio {

using u8 = uint8_t; using u32 = uint32_t; using s32 = int32_t; using s64 = int64_t;

struct Stat {
    u32 size = 0;
    u32 mode = 0;      // bit0 = directory (cukup buat game)
    bool ok = false;
};

// root default: env GH_CDROOT lalu /root/ports/godhand/extract
bool init(const std::string& root = "");
const std::string& root();

// Resolve path PS2 -> host. tolower-compare per komponen, tolak ".." dan absolut.
bool resolve(const std::string& ps2_path, std::string& out, bool* is_dir = nullptr);

s32 open(const std::string& ps2_path, u32 flags);           // flags: 1=RDONLY 2=WRONLY 3=RDWR 0x200=CREAT
u32 read(s32 fd, void* buf, u32 n);
u32 write(s32 fd, const void* buf, u32 n);
u32 lseek(s32 fd, s32 offset, u32 whence);                  // 0=SET 1=CUR 2=END
u32 close(s32 fd);
u32 size(s32 fd);
u32 seek_base(s32 fd);
bool stat(const std::string& ps2_path, Stat& st);
std::vector<std::string> listdir(const std::string& ps2_dir);

} // namespace hostio
