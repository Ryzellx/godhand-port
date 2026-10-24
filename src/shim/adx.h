// God Hand (PS2 SLUS-21503) — CRI ADX ADPCM decoder (port audio backend)
// Runtime: C++17 host (no PS2 deps) | Target: native port audio path
//
// Format facts implemented here (verified against the game's own streams):
//   18-byte frame = 2-byte scale/filter word + 16-byte nibble payload
//   one frame decodes 32 samples per channel (16 payload bytes x 2 nibbles)
//   4-bit signed ADPCM, 2-tap LPC predictor. Predictor coefficients are either
//   per-frame (encoding type 2: filter index 0..3 in the top 3 bits of the
//   frame word) or derived from the header cutoff frequency (types 3/4).
//
// God Hand streams are CRI "type 8" encrypted: version word 0x0408, and only
// the per-frame scale word is scrambled with an LCG keystream derived from the
// keystring "karaage" (found verbatim in SLUS_215.03). Nibbles are not ciphered,
// so a decoded frame needs the keystream word before scale/predictor are usable.
#ifndef GH_SHIM_ADX_H
#define GH_SHIM_ADX_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gh {
namespace audio {

constexpr uint32_t kAdxFrameBytes   = 18;   // bytes per channel per frame
constexpr uint32_t kAdxFrameSamples = 32;   // decoded samples per frame
constexpr uint32_t kAdxCoefBits     = 12;   // predictor coefficient fixed point
constexpr uint32_t kAdxMaxChannels  = 8;

// Keystream state for CRI ADX encryption type 8 / 9.
struct AdxKey {
    uint16_t start = 0;
    uint16_t mult  = 0;
    uint16_t add   = 0;
    bool     valid = false;
};

// Derives (start, mult, add) from a CRI keystring exactly like ADX_Decoder does:
// three independent prime-table walks seeded at index 0x100/0x200/0x300.
AdxKey adx_key_from_keystring(const char* keystring);

// God Hand / Okami keystring. "karaage" -> start 0x49E1, mult 0x4A57, add 0x553D.
extern const char* const kAdxGodHandKeystring;

struct AdxHeader {
    uint16_t header_size     = 0;   // raw field at 0x02; audio starts 4 bytes later
    uint32_t data_offset     = 0;   // header_size + 4
    uint8_t  encoding        = 0;   // 2 = fixed filter, 3 = normal, 4 = exponential
    uint8_t  frame_size      = 0;   // 0x12
    uint8_t  bits_per_sample = 0;   // 4
    uint8_t  channels        = 0;
    uint32_t sample_rate     = 0;   // big-endian u32 at 0x08
    uint32_t sample_count    = 0;   // per-channel samples at 0x0C
    uint16_t cutoff_hz       = 0;   // high-pass cutoff at 0x10 (500 in every GH file)
    uint16_t version_word    = 0;   // 0x12: 0x0400 plain, 0x0408 / 0x0409 encrypted
    bool     encrypted       = false;
    // v4 loop block, fixed offsets (verified against vgmstream on every looping
    // God Hand stream; a history-relative layout misreads mono files):
    //   0x20 u16 blank samples (encoder padding at loop entry)
    //   0x24 u32 loop flag      0x28 u32 loop start sample
    //   0x2c u32 loop start byte 0x30 u32 loop end sample
    //   0x34 u32 loop end byte
    // Byte offsets are padded to the CD sector grid, so samples are authoritative
    // and are always frame aligned (multiple of 32).
    bool     loop            = false;
    int32_t  loop_start_sample = 0;
    int32_t  loop_end_sample   = 0;
    uint32_t loop_start_byte   = 0;
    uint32_t loop_end_byte     = 0;
    uint16_t loop_padding      = 0; // blank samples the encoder inserted for block alignment
    int16_t  history[kAdxMaxChannels][2] = {};  // initial predictor state (v4 header 0x18)
    uint32_t total_frames() const { return (uint32_t)((sample_count + kAdxFrameSamples - 1) / kAdxFrameSamples); }
};

// Which arithmetic to emulate when decoding.
//   Cri    - CRI ADXPLAY behaviour: scale+1, history seeded from the header,
//            coefficients truncated from the float cutoff formula. Byte-exact
//            with vgmstream.
//   Ffmpeg - libavcodec adpcm_adx behaviour: scale used raw, zero history,
//            coefficients rounded with lrintf. Byte-exact with `ffmpeg -i x.adx`.
enum class AdxFlavor { Cri, Ffmpeg };

// Copies `size` bytes of ADX into `out` with the type-8/9 keystream removed from
// every frame's scale word (13-bit scale kept, filter bits cleared) and the
// version word rewritten to 0x0400, i.e. a plain ADX ffmpeg/VGAudio can decode.
bool adx_decrypt_to_plain(const uint8_t* data, size_t size, const AdxKey& key,
                          const AdxHeader& header, std::vector<uint8_t>* out);

bool adx_is_adx(const uint8_t* data, size_t size);
bool adx_parse_header(const uint8_t* data, size_t size, AdxHeader* out, std::string* err);

class AdxDecoder {
public:
    // Copies the stream internally; parses the header (stores it) and, when the
    // stream is encrypted, unscrambles the frame scale words with `key`.
    bool init(const uint8_t* data, size_t size, AdxFlavor flavor,
              const AdxKey& key, std::string* err);

    const AdxHeader& header() const { return header_; }
    AdxFlavor flavor() const { return flavor_; }

    // Decodes every complete frame. Output is interleaved 16-bit little-endian
    // order in memory (ch0 s0, ch1 s0, ch0 s1, ...). Returns samples per channel
    // actually produced (may exceed header().sample_count; caller clamps).
    const std::vector<int16_t>& decode();

    // Same decode, clamped to the header's per-channel sample_count. This is the
    // length the PS2 ADX player (and vgmstream) actually returns: the encoder pads
    // the last block out to a full 18-byte frame, so decode() can be up to one
    // frame longer per channel. Port code should use this unless it wants the
    // libavcodec-identical tail.
    const std::vector<int16_t>& decode_trimmed();

    size_t samples_per_channel() const { return samples_per_channel_; }
    size_t samples_per_channel_trimmed() const;

private:
    std::vector<uint8_t> data_;
    AdxHeader header_;
    AdxFlavor flavor_ = AdxFlavor::Cri;
    std::vector<int16_t> pcm_;
    std::vector<int16_t> pcm_trimmed_;
    size_t samples_per_channel_ = 0;
    size_t samples_per_channel_trimmed_ = 0;
    bool decoded_ = false;
    bool trimmed_ = false;
};

// Convenience: full stream decode from a file path (used by tooling/tests).
bool adx_decode_file(const char* path, AdxFlavor flavor, const AdxKey& key,
                     AdxHeader* header, std::vector<int16_t>* pcm, std::string* err);

}  // namespace audio
}  // namespace gh

#endif  // GH_SHIM_ADX_H
