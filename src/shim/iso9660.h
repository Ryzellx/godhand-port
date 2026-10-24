// iso9660.h — pembaca ISO9660 mentah (ISO retail God Hand) untuk backend CDVD.
// Tidak lewat ekstraksi: file dibaca langsung dari image, jadi port tetap "no emulator,
// no unpack" dan nama ;1 / direktori MODULES dilayani seperti drive asli.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace iso9660 {
using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t;

bool mount(const std::string& path);      // "" -> GH_ISO env, lalu path retail default
bool mounted();
const std::string& image_path();

// path PS2 gaya disc: "\MODULES\IRX.IMS;1" / "cdrom0:\_DATA.AFS;1"
bool lookup(const std::string& ps2_path, u32& lba, u32& size, bool& is_dir);

// baca n byte dari file (offset relatif file), return byte terbaca
u32 read_at(u32 lba, u32 file_size, u32 off, void* buf, u32 n);
bool read_all(const std::string& ps2_path, std::vector<u8>& out);

// Baca mentah N sektor mulai LSN (bukan lookup): dipakai jalur "Read_EE" cdvdfsv
// (SID 0x80000595 fno 1) yang meminta LBA absolut, ukuran sektor dari mode[2].
u32 read_sectors(u32 lsn, u32 nsec, void* buf, u32 sector_size = 2048);

struct Stat { u32 lba = 0, size = 0; bool dir = false; bool ok = false; };
Stat stat(const std::string& ps2_path);

void reset();
} // namespace iso9660
