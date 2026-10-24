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

// How the predictor history behaves when playback jumps loop_end -> loop_start.
// A real-time player carries the running history across the jump; a player that
// re-seeks decodes the loop body again and lands on the linear history instead.
// Which one to emulate was decided empirically against vgmstream's looped render on
// all 45 looping streams (docs/PROJECT.md); the game's own IOP player is not reversed
// yet, so this is a reference-decoder choice, not a hardware claim.
enum class AdxLoopState {
    Carry,        // keep the history from the sample before loop_end
    ResetHeader,  // reseed h1/h2 from header 0x18/0x1c at the jump
    Linear,       // take the loop body straight out of the linear decode (history as if no jump)
};

struct AdxLoopRender {
    int32_t      loops = 1;                          // extra loop-body plays after the intro
    bool         tail  = true;                       // append loop_end..end once the loops are done
    AdxLoopState state = AdxLoopState::Linear;  // matches vgmstream byte-exact
};

// Writes interleaved s16 to a canonical 44-byte-header WAV (RIFF/WAVE/fmt 16/data).
bool adx_write_wav(const char* path, const int16_t* pcm, size_t samples_per_channel,
                   uint32_t channels, uint32_t sample_rate);

// Configuration for AdxStream. Same timeline as AdxLoopRender: intro, N extra
// loop bodies, optional tail. `block_hint` is informational only — read() is
// block-size independent down to one frame.
struct AdxStreamConfig {
    int32_t      loops = 0;                       // extra loop-body plays
    bool         tail  = true;                    // play loop_end..sample_count once loops are done
    AdxLoopState state = AdxLoopState::Linear;    // history behaviour at the jump
};

// Runtime-facing pull decoder: emits the loop timeline in blocks of arbitrary
// size without ever materialising the whole PCM. Constant memory (one 32-sample
// frame plus the stream bytes the caller owns), O(1) jump, byte-identical to
// AdxDecoder::render_loop() with the same AdxStreamConfig for every block size.
//
// The decrypted-payload copy AdxDecoder does is avoided here: the type-8 keystream
// is advanced one word per 18-byte block and the state at loop_start is snapshotted
// on the first pass, so a jump can restore the cipher state too.
class AdxStream {
public:
    // `data` must stay alive and unchanged while the stream is used; the class does
    // not copy it. Parses the header, validates frame layout, prepares the loop
    // restore point. Returns false with `err` set if the stream is unusable.
    bool open(const uint8_t* data, size_t size, AdxFlavor flavor, const AdxKey& key,
              const AdxStreamConfig& cfg, std::string* err);

    // Copies at most `frames` interleaved frames to `out`; returns frames written.
    // 0 means the timeline ended (or read() was called after it ended).
    size_t read(int16_t* out, size_t frames);

    bool ended() const { return done_; }
    const AdxHeader& header() const { return header_; }
    AdxFlavor flavor() const { return flavor_; }
    // Samples per channel left before the next event (loop jump or end of timeline).
    size_t samples_until_event() const;
    // Total timeline length in samples per channel that read() will produce.
    size_t timeline_samples() const;

private:
    struct Hist {
        int32_t h1[kAdxMaxChannels] = {};
        int32_t h2[kAdxMaxChannels] = {};
    };

    Hist history_at_cursor() const;
    bool load_frame(size_t idx);
    void jump_to_loop_start();
    void advance_segment();
    void decode_block(const uint8_t* src, uint16_t key_word, int32_t* h1, int32_t* h2,
                      int16_t* out, size_t stride, uint32_t hist_at = kAdxFrameSamples,
                      int32_t* hist_h1 = nullptr, int32_t* hist_h2 = nullptr) const;

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    AdxHeader header_;
    AdxFlavor flavor_ = AdxFlavor::Cri;
    AdxKey key_;
    int32_t coef_c1_ = 0;   // header-derived predictor (same for all channels)
    int32_t coef_c2_ = 0;
    AdxStreamConfig cfg_;

    size_t channels_ = 0;
    size_t frame_bytes_ = 0;
    size_t blocks_ = 0;
    size_t total_ = 0;
    size_t loop_start_ = 0;
    size_t loop_end_ = 0;
    bool   loopable_ = false;

    // timeline cursor
    size_t pos_ = 0;        // sample position inside the current pass
    size_t limit_ = 0;      // end of the current segment
    size_t frame_idx_ = 0;  // frame currently buffered in frame_buf_
    bool   have_frame_ = false;
    bool   done_ = false;
    int32_t played_ = 0;

    Hist   run_;     // running predictor history
    Hist   entry_;   // history entering the buffered frame
    Hist   ls_entry_;
    uint16_t ks_ = 0;
    uint16_t ls_ks_ = 0;
    bool   ls_valid_ = false;
    size_t ls_frame_ = 0;

    std::vector<int16_t> frame_buf_;
    std::vector<uint8_t> block_;
};

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

    // Renders the playback timeline the PS2 player produces, not just the file
    // layout: intro [0, loop_end), `loops` x [loop_start, loop_end), then the tail
    // [loop_end, sample_count). Non-looping streams degrade to decode_trimmed().
    // `out` receives interleaved s16. Malformed loop points fall back to a plain
    // linear render with `err` set (still returns true); false means the stream layout
    // itself is unusable.
    bool render_loop(const AdxLoopRender& opt, std::vector<int16_t>* out, std::string* err);

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

// ── Phase 9d: sample-rate conversion to the SPU2 rate ────────────────────────
// The PS2 SPU2 only plays 48 kHz (59.94 Hz NTSC basis); 8 of the 367 God Hand
// streams are 24 kHz. Mixing them into SPU2 as-is would play both at double
// speed and halve the timeline, so the port needs a real converter, not a
// nearest-sample pick.
//
// Implementation contract (all measured, see docs/PROJECT.md Phase 9d):
//   * windowed-sinc, Kaiser window, 128 taps per phase, Q23 coefficients
//   * cutoff scales with min(1, out/in) so decimation cannot alias
//   * phase accumulator is integer (fixed point) so 24000->48000 is exactly 2
//     output samples per input sample with zero drift over 10^9 samples
//   * output length is ceil(in * out_rate / in_rate) and is exactly reproducible
struct AdxResampleConfig {
    uint32_t taps           = 128;   // FIR taps per phase (even, >= 8)
    double   kaiser_beta    = 8.6;   // stopband attenuation ~ 90 dB
    double   cutoff         = 0.97;  // passband edge as a fraction of the lower Nyquist
    uint32_t phase_bits     = 12;    // sub-sample resolution: 4096 phases + half
    bool     clamp          = true;  // clamp to int16 (quantisation of overshoot)
};

// Resamples interleaved s16 in place-of-copy. `in` holds `in_frames` frames of
// `channels` interleaved samples; `out` receives interleaved s16 at `out_rate`.
// Returns false only on bad arguments (zero rate, zero channels, null buffers).
bool adx_resample_s16(const int16_t* in, size_t in_frames, uint32_t in_rate,
                      uint32_t channels, uint32_t out_rate,
                      const AdxResampleConfig& cfg, std::vector<int16_t>* out,
                      std::string* err);

// Number of output frames adx_resample_s16() will emit for `in_frames`, i.e.
// ceil(in_frames * out_rate / in_rate). Same formula the implementation uses,
// so callers can size buffers without guessing.
size_t adx_resample_out_frames(size_t in_frames, uint32_t in_rate, uint32_t out_rate);

// Streaming converter for the mixer: push interleaved frames at the stream rate,
// pull frames at `out_rate`. Same coefficients and same integer phase arithmetic
// as adx_resample_s16(), so one-shot and block-streamed output are byte-identical
// for every block size (gate: --resample-check).
//
// LOOPING (measured, Phase 9d): AdxStream::read() already emits playback order —
// the loop jump is inside the stream — so resampling a looping timeline is
// byte-identical to resampling one whole render_loop() buffer (verified on
// adx_m66_0004 at 48->96 kHz: 52.439.352 frames, 0 mismatches). Callers do NOT
// need to cut reads at samples_until_event(). What does corrupt output is a
// discontinuity the caller introduces itself (seek, retrigger, restart from
// loop_start): the FIR history then holds audio from before the seek, and the
// first taps/2 frames after it are wrong. Call reset() on every explicit jump.
class AdxRateConverter {
public:
    bool init(uint32_t in_rate, uint32_t out_rate, uint32_t channels,
              const AdxResampleConfig& cfg, std::string* err);

    bool ok() const { return ok_; }
    bool bypass() const { return bypass_; }
    uint32_t in_rate() const { return in_rate_; }
    uint32_t out_rate() const { return out_rate_; }
    uint32_t channels() const { return channels_; }

    // Output frames that the whole input (this call included) will produce.
    size_t output_frames(size_t in_frames) const;

    // Pushes `in_frames`, writes at most `max_out` interleaved frames to `out`.
    // Returns frames written. Caller must size `out` for output_frames(total_in)
    // minus what was already written; frames beyond max_out stay queued in the
    // input history and come out on the next call.
    size_t process(const int16_t* in, size_t in_frames, int16_t* out, size_t max_out);

    // Zero-pads the tail and emits the remaining output frames. Idempotent.
    size_t flush(int16_t* out, size_t max_out);

    bool finished() const { return done_; }
    void reset();

private:
    size_t generate(int16_t* out, size_t max_out, bool flush);

    uint32_t in_rate_ = 0, out_rate_ = 0, channels_ = 1;
    uint32_t up_ = 1, down_ = 1, phases_ = 1, taps_ = 0;
    bool exact_ = true, bypass_ = false, ok_ = false, flushing_ = false, done_ = false;
    std::vector<int32_t> coef_;
    std::vector<int16_t> hist_;   // interleaved input frames, hist_[0] == frame hist_base_
    size_t hist_base_ = 0;
    size_t in_total_ = 0;         // input frames pushed so far
    size_t n_gen_ = 0;            // output frames produced so far
    size_t delivered_ = 0;        // frames handed to the caller (bypass bookkeeping)
};

// What the mixer wants: one pull API, already at the SPU2 rate, loop timeline
// included. AdxPlayback = AdxStream (decode + loop) -> AdxRateConverter (rate),
// with the block bounce hidden. `data` must outlive the object.
struct AdxPlaybackConfig {
    int32_t          loops    = 0;                        // extra loop-body plays
    bool             tail     = true;                     // play loop_end..end once
    AdxLoopState     state    = AdxLoopState::Linear;     // history at the jump
    uint32_t         out_rate = 48000;                    // SPU2 rate
    AdxResampleConfig resample;
};

class AdxPlayback {
public:
    bool open(const uint8_t* data, size_t size, AdxFlavor flavor, const AdxKey& key,
              const AdxPlaybackConfig& cfg, std::string* err);

    // Interleaved s16 at cfg.out_rate. Returns frames written; 0 means the
    // timeline ended. Byte-identical to resampling one render_loop() buffer.
    size_t read(int16_t* out, size_t frames);

    bool ended() const { return ended_; }
    const AdxHeader& header() const { return stream_.header(); }
    uint32_t channels() const { return channels_; }
    uint32_t out_rate() const { return cfg_.out_rate; }
    size_t timeline_frames() const { return conv_.output_frames(stream_.timeline_samples()); }
    bool bypass() const { return conv_.bypass(); }

private:
    AdxStream        stream_;
    AdxRateConverter conv_;
    AdxPlaybackConfig cfg_;
    uint32_t channels_ = 0;
    std::vector<int16_t> ib_, ob_;
    size_t ob_pos_ = 0, ob_len_ = 0;
    bool ended_ = false, flushed_ = false;
};

// Convenience for the runtime path: decode `path` (CRI flavour, trimmed to the
// header sample_count) and resample to `out_rate` if needed. If the stream is
// already at `out_rate` the PCM is copied, not filtered.
bool adx_decode_file_resampled(const char* path, AdxFlavor flavor, const AdxKey& key,
                               uint32_t out_rate, const AdxResampleConfig& cfg,
                               AdxHeader* header, std::vector<int16_t>* pcm,
                               size_t* out_frames, std::string* err);

}  // namespace audio
}  // namespace gh

#endif  // GH_SHIM_ADX_H
