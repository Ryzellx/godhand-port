// adx.cpp — decoder CRI ADX 4-bit (block 18 byte / 32 sampel / channel).
#include "adx.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace adx {
namespace {
u16 rb16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }
u32 rb32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
inline s16 clip16(int v) { return (s16)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

// ff_adx_calculate_coeffs() — identik dengan libavcodec/adx.c
void calc_coeffs(int cutoff, int rate, int bits, int* coeff) {
    const double sq2 = std::sqrt(2.0);
    double a = sq2 - std::cos(2.0 * M_PI * cutoff / (double)rate);
    double b = sq2 - 1.0;
    double c = (a - std::sqrt((a + b) * (a - b))) / b;
    coeff[0] = (int)std::lround(c * 2.0 * (1 << bits));
    coeff[1] = (int)std::lround(-(c * c) * (1 << bits));
}
} // namespace

Header parse_header(const u8* buf, size_t n) {
    Header h;
    if (n < 24) { h.err = "file < 24 B"; return h; }
    if (rb16(buf) != 0x8000) { h.err = "magic != 0x8000"; return h; }
    h.data_offset = (u32)rb16(buf + 2) + 4;
    if (h.data_offset < 6 || h.data_offset > n) { h.err = "header offset di luar file"; return h; }
    if (std::memcmp(buf + h.data_offset - 6, "(c)CRI", 6) != 0) {
        h.err = "copyright string bukan (c)CRI";
        return h;
    }
    h.encoding = buf[4]; h.block_size = buf[5]; h.bit_depth = buf[6]; h.channels = buf[7];
    if (h.encoding != 3 || h.block_size != BLOCK_SIZE || h.bit_depth != 4) {
        h.err = "format tak didukung (butuh enc=3 block=18 bit=4)";
        return h;
    }
    if (h.channels < 1 || h.channels > 6) { h.err = "channel di luar 1..6"; return h; }
    h.sample_rate = rb32(buf + 8);
    h.total_samples = rb32(buf + 12);
    h.cutoff = rb16(buf + 16);
    if (h.sample_rate < 1) { h.err = "sample rate 0"; return h; }
    calc_coeffs(h.cutoff, (int)h.sample_rate, COEFF_BITS, h.coeff);
    if (buf[0x13] & 0x01) {   // loop flag
        h.has_loop = true;
        h.loop_start_sample = rb32(buf + 0x14);
        h.loop_start_byte = rb32(buf + 0x18);
        h.loop_end_sample = rb32(buf + 0x1C);
        h.loop_end_byte = rb32(buf + 0x20);
    }
    h.ok = true;
    return h;
}

Header load(const std::string& path, std::vector<u8>& out) {
    Header h;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { h.err = "file tak bisa dibuka"; return h; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); h.err = "file kosong"; return h; }
    out.resize((size_t)sz);
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { h.err = "short read"; return h; }
    return parse_header(out.data(), out.size());
}

u32 decode(const std::vector<u8>& file, const Header& h, std::vector<s16>& pcm) {
    pcm.clear();
    if (!h.ok || h.channels <= 0) return 0;
    const u32 frame_bytes = (u32)BLOCK_SIZE * (u32)h.channels;
    if (file.size() <= h.data_offset) return 0;
    u32 blocks = (u32)((file.size() - h.data_offset) / frame_bytes);
    std::vector<int> s1(h.channels, 0), s2(h.channels, 0);
    std::vector<s16> planar((size_t)h.channels * BLOCK_SAMPLES);
    pcm.reserve((size_t)blocks * BLOCK_SAMPLES * h.channels);
    const u8* p = file.data() + h.data_offset;
    for (u32 b = 0; b < blocks; b++) {
        for (int ch = 0; ch < h.channels; ch++, p += BLOCK_SIZE) {
            int scale = rb16(p);
            if (scale & 0x8000) {                 // EOF packet
                pcm.resize((size_t)b * BLOCK_SAMPLES * h.channels);
                return b * BLOCK_SAMPLES;
            }
            const u8* nib = p + 2;                // 16 byte = 32 nibble
            s16* dst = planar.data() + (size_t)ch * BLOCK_SAMPLES;
            for (int i = 0; i < BLOCK_SAMPLES; i++) {
                u8 byte = nib[i >> 1];
                int d = (i & 1) ? (byte & 0x0F) : (byte >> 4);
                if (d >= 8) d -= 16;              // signed 4-bit
                int s0 = d * scale + ((h.coeff[0] * s1[ch] + h.coeff[1] * s2[ch]) >> COEFF_BITS);
                s2[ch] = s1[ch];
                s1[ch] = clip16(s0);
                dst[i] = (s16)s1[ch];
            }
        }
        // PCM keluar interleaved per sampel (L,R,L,R,...) — blok planar di dalam
        for (int i = 0; i < BLOCK_SAMPLES; i++)
            for (int ch = 0; ch < h.channels; ch++)
                pcm.push_back(planar[(size_t)ch * BLOCK_SAMPLES + i]);
    }
    return blocks * BLOCK_SAMPLES;
}

void play(const std::vector<s16>& pcm, int channels, Sink& sink) {
    sink.samples += pcm.size();
    sink.frames += channels > 0 ? pcm.size() / (size_t)channels : 0;
    sink.bytes += pcm.size() * 2;
    if (sink.capture) sink.captured.insert(sink.captured.end(), pcm.begin(), pcm.end());
    // native: tidak ada device audio -> data dibuang (setara /dev/null)
}

bool write_wav(const std::string& path, const std::vector<s16>& pcm, int channels, u32 rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    u32 data_bytes = (u32)(pcm.size() * 2);
    u16 ch = (u16)channels, bits = 16, align = (u16)(channels * 2);
    u32 byte_rate = rate * align, riff = 36 + data_bytes;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32 fmt_len = 16; std::fwrite(&fmt_len, 4, 1, f);
    u16 pcm_tag = 1; std::fwrite(&pcm_tag, 2, 1, f);
    std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data_bytes, 4, 1, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return true;
}

} // namespace adx
