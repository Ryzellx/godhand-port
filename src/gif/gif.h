// gif.h — GS GIF tag parser (interception layer port God Hand native)
// Sumber spesifikasi: PS2 GS User's Manual (GIF tag: NLOOP/EOP/PRE/PRIM/FLG/NREG/REGS)
// Tujuan: EE DMA channel-2 (0x1000A000) mentah -> daftar aksi terstruktur buat backend Vulkan
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace gif {

// FLG (2 bit) — mode pembawa data GIF
enum class Flag : uint32_t { PACKED = 0, REGLIST = 1, IMAGE = 2 };

// Deskriptor register REGS[i] (4 bit)
enum Reg : uint32_t {
    PRIM = 0x0, RGBAQ = 0x1, ST = 0x2, UV = 0x3, XYZF2 = 0x4, XYZ2 = 0x5,
    TEX0_1 = 0x6, TEX0_2 = 0x7, CLAMP_1 = 0x8, CLAMP_2 = 0x9,
    TEX1_1 = 0xA, TEX1_2 = 0xB, TEX2_1 = 0xC, TEX2_2 = 0xD,
    AD = 0xE,  // address+data (mode PACKED)
    NOP = 0xF
};

struct Tag {
    uint32_t nloop;   // 15 bit — jumlah qword payload / loop
    bool     eop;     // end-of-packet
    bool     pre;     // PRE bit (prim sebelum data — PACKED only)
    uint32_t prim;    // 11 bit — nilai PRIM gabungan (khusus PRE=1)
    Flag     flg;
    uint32_t nreg;    // 4 bit — register per loop (0 => 16)
    uint8_t  regs[16];// deskriptor 4 bit tiap slot
};

// Satu aksi yang sudah dinormalisasi — backend tinggal implement
struct Action {
    enum Kind { PRIM_WRITE, AD_WRITE, UV_WRITE, XYZ_WRITE, RGBA_WRITE, IMAGE_DATA, NOP } kind;
    uint64_t ad_addr = 0;   // untuk AD_WRITE: register GS (11 bit rendah word pertama)
    uint64_t ad_data = 0;   // data 64 bit
    uint32_t prim = 0;      // untuk PRIM_WRITE
    uint32_t qwords = 0;    // untuk IMAGE_DATA: jumlah qword data
};

// Decode 128-bit (16 byte) GIF tag little-endian
Tag decode_tag(const uint8_t* q);

// Parse rantai paket: mulai dari tag, konsumsi payload sesuai NLOOP/NREG/FLG.
// kembalikan aksi + offset byte setelah paket terakhir.
// buf/len = seluruh stream DMA (qword-aligned). false kalau truncated/FLG tak dikenal.
bool parse_stream(const uint8_t* buf, size_t len, std::vector<Action>& out, size_t* consumed);

// Hitung jumlah qword payload milik 1 tag (tanpa tag)
size_t payload_qwords(const Tag& t);

} // namespace gif
