// adx.h — Phase 8: backend audio CRI ADX (SDHD/SDRDRV/SNDDRV di HW asli).
// Format mengikuti ffmpeg libavcodec/adx.h + adxdec.c (referensi BERO, COEFF_BITS=12,
// BLOCK_SIZE=18, BLOCK_SAMPLES=32): 32 sampel per blok dari 16 byte nibble, prediksi
// LPC orde-2 dengan koefisien dari cutoff di header. Loop point dibaca kalau flag bit0.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace adx {

using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using s16 = int16_t;
using s32 = int32_t; using u64 = uint64_t;

inline constexpr int BLOCK_SIZE = 18;      // byte per blok per channel
inline constexpr int BLOCK_SAMPLES = 32;   // sampel per blok
inline constexpr int COEFF_BITS = 12;

struct Header {
    bool ok = false;
    u32 data_offset = 0;
    int encoding = 0, block_size = 0, bit_depth = 0, channels = 0;
    u32 sample_rate = 0, total_samples = 0;
    u16 cutoff = 0;
    int coeff[2] = {0, 0};
    bool has_loop = false;
    u32 loop_start_sample = 0, loop_start_byte = 0, loop_end_sample = 0, loop_end_byte = 0;
    std::string err;
};

Header parse_header(const u8* buf, size_t n);
Header load(const std::string& path, std::vector<u8>& out);

// Decode semua blok -> PCM s16 interleaved. Return jumlah sampel per channel.
u32 decode(const std::vector<u8>& file, const Header& h, std::vector<s16>& pcm);

// Sink pemutaran: default membuang (setara /dev/null); bisa diganti callback.
struct Sink {
    u64 frames = 0, samples = 0, bytes = 0;
    std::vector<s16> captured;   // terisi hanya kalau capture=true (test)
    bool capture = false;
};
void play(const std::vector<s16>& pcm, int channels, Sink& sink);

bool write_wav(const std::string& path, const std::vector<s16>& pcm, int channels, u32 rate);

} // namespace adx
