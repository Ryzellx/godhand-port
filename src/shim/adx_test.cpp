// God Hand (PS2 SLUS-21503) — CRI ADX decoder verification tool
// Runtime: C++17 host | Target: prove src/shim/adx.cpp against independent decoders
//
// For every .adx file given (or every file in --dir):
//   1. parse the header (sample rate, channels, frame count, header sample_count,
//      loop block at 0x24/0x28/0x2c/0x30/0x34)
//   2. decode with the CRI flavour (ADXPLAY semantics: scale+1, header history seed)
//      both as full frames and clamped to the header sample_count
//   3. decode with the libavcodec flavour (raw scale, zero history, full frames)
//   4. unscramble the type-8 stream to a plain ADX and let ffmpeg decode it
//   5. compare: libavcodec flavour must be byte-exact vs ffmpeg (validates the
//      ADPCM core: nibble order, predictor, clamp, frame striding, interleave),
//      CRI flavour (trimmed) must be byte-exact vs vgmstream on the ORIGINAL
//      encrypted file (validates the keystream AND the CRI arithmetic AND the
//      sample_count trim), and the parsed loop points must equal vgmstream's
//   6. with --synth, exercise the encoding-type-2 per-frame filter table
//      (filter index 0..3) against vgmstream
//
// Exit code 0 only when every file PASSes.
#include "adx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using gh::audio::AdxDecoder;
using gh::audio::AdxFlavor;
using gh::audio::AdxHeader;
using gh::audio::AdxKey;
using gh::audio::AdxLoopRender;
using gh::audio::AdxLoopState;

struct Options {
    std::vector<std::string> files;
    std::string dir = "/root/ports/godhand/extract/afs";
    std::string ffmpeg = "ffmpeg";
    std::string vgmstream;
    std::string tmp = "/dev/shm/adxtest";
    std::string key = "karaage";
    uint32_t limit = 0;
    bool synth = false;
    bool skip_ffmpeg = false;
    bool loop_render = false;      // render the loop timeline and compare to vgmstream -l N -F
    std::string loop_state = "auto";  // carry | reset | linear | auto
    int loops = 1;                 // extra loop-body plays (vgmstream is run with -l loops+1)
    std::string wav_dir;           // optional: dump trimmed PCM to <dir>/<name>.wav
};

struct Compare {
    size_t samples = 0;
    size_t mismatches = 0;
    double snr_db = 0.0;
    bool exact = false;
    bool length_mismatch = false;
};

// What vgmstream reports for a file, parsed from `vgmstream-cli -m`.
struct VgmMeta {
    bool ok = false;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    long long total_samples = -1;
    bool loop = false;
    long long loop_start = -1;
    long long loop_end = -1;
    std::string encoding;
};

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out->resize((size_t)n);
    const size_t got = out->empty() ? 0 : std::fread(out->data(), 1, out->size(), f);
    std::fclose(f);
    return got == out->size();
}

bool write_file(const std::string& path, const void* data, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t put = n ? std::fwrite(data, 1, n, f) : 0;
    std::fclose(f);
    return put == n;
}

bool file_exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string shell_quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else q += c;
    }
    return q + "'";
}

bool run_cmd(const std::string& cmd) { return std::system(cmd.c_str()) == 0; }

std::string run_capture(const std::string& cmd) {
    std::string out;
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    return out;
}

Compare compare_pcm(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    Compare r;
    const size_t n = std::min(a.size(), b.size());
    r.samples = n;
    // A longer-than-reference decode is fine (ADX pads the last 18-byte frame out
    // to a full 32 samples); a shorter one means we lost audio and is a failure.
    r.length_mismatch = b.size() > a.size();
    if (n == 0) return r;
    long double sig = 0.0L, err = 0.0L;
    for (size_t i = 0; i < n; ++i) {
        const long long d = (long long)a[i] - (long long)b[i];
        if (d) ++r.mismatches;
        sig += (long double)b[i] * (long double)b[i];
        err += (long double)d * (long double)d;
    }
    r.exact = (r.mismatches == 0) && !r.length_mismatch;
    if (err == 0.0L) r.snr_db = 999.0;  // identical
    else if (sig == 0.0L) r.snr_db = -999.0;
    else r.snr_db = (double)(10.0L * std::log10(sig / err));
    return r;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    FILE* p = popen(("ls " + shell_quote(dir) + "/*.adx 2>/dev/null").c_str(), "r");
    if (!p) return out;
    char line[4096];
    while (std::fgets(line, sizeof(line), p)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) out.push_back(s);
    }
    pclose(p);
    std::sort(out.begin(), out.end());
    return out;
}

std::string stem_of(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

// Pulls the integer that follows `label` in vgmstream's metadata dump.
long long meta_int(const std::string& text, const std::string& label) {
    const size_t at = text.find(label);
    if (at == std::string::npos) return -1;
    const char* p = text.c_str() + at + label.size();
    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return -1;
    return std::strtoll(p, nullptr, 10);
}

VgmMeta vgm_meta(const Options& o, const std::string& file) {
    VgmMeta m;
    if (o.vgmstream.empty()) return m;
    const std::string text = run_capture(shell_quote(o.vgmstream) + " -m " + shell_quote(file));
    if (text.find("failed opening") != std::string::npos) return m;
    m.sample_rate = (uint32_t)meta_int(text, "sample rate:");
    m.channels = (uint32_t)meta_int(text, "channels:");
    m.total_samples = meta_int(text, "stream total samples:");
    m.loop_start = meta_int(text, "loop start:");
    m.loop_end = meta_int(text, "loop end:");
    m.loop = (m.loop_start >= 0 && m.loop_end >= 0);
    const size_t at = text.find("encoding: ");
    if (at != std::string::npos) {
        const size_t eol = text.find('\n', at);
        m.encoding = text.substr(at + 10, eol == std::string::npos ? std::string::npos : eol - at - 10);
    }
    m.ok = (m.sample_rate != 0 && m.total_samples >= 0);
    return m;
}

// Decodes a file with vgmstream-cli and returns s16le PCM.
bool vgmstream_pcm(const Options& o, const std::string& file, std::vector<int16_t>* pcm) {
    if (o.vgmstream.empty()) return false;
    const std::string base = o.tmp + "/vgm_" + stem_of(file);
    const std::string wav = base + ".wav";
    const std::string raw = base + ".raw";
    std::remove(wav.c_str());
    std::remove(raw.c_str());
    // -i = ignore loop. Without it vgmstream-cli expands the loop point and emits
    // every loop pass, which fails an exact compare even though the audio matches.
    if (!run_cmd(shell_quote(o.vgmstream) + " -i -o " + shell_quote(wav) + " " + shell_quote(file) +
                 " >/dev/null 2>&1"))
        return false;
    if (!run_cmd(shell_quote(o.ffmpeg) + " -v error -y -i " + shell_quote(wav) + " -f s16le -acodec pcm_s16le " +
                 shell_quote(raw) + " >/dev/null 2>&1"))
        return false;
    std::vector<uint8_t> bytes;
    if (!read_file(raw, &bytes) || bytes.empty()) return false;
    pcm->resize(bytes.size() / 2);
    std::memcpy(pcm->data(), bytes.data(), pcm->size() * 2);
    std::remove(wav.c_str());
    std::remove(raw.c_str());
    return true;
}

// Decodes a looping file the way vgmstream's renderer expands it: -l <loops+1>.0
// plays one extra loop body per unit, -F suppresses the fade tail so the output is
// exactly intro + N bodies + tail.
bool vgmstream_loop_pcm(const Options& o, const std::string& file, int loops,
                        std::vector<int16_t>* pcm) {
    if (o.vgmstream.empty()) return false;
    const std::string base = o.tmp + "/vgmloop_" + stem_of(file);
    const std::string wav = base + ".wav";
    const std::string raw = base + ".raw";
    std::remove(wav.c_str());
    std::remove(raw.c_str());
    const std::string cmd = shell_quote(o.vgmstream) + " -l " + std::to_string(loops + 1) +
                            ".0 -F -o " + shell_quote(wav) + " " + shell_quote(file) +
                            " >/dev/null 2>&1";
    if (!run_cmd(cmd)) return false;
    if (!run_cmd(shell_quote(o.ffmpeg) + " -v error -y -i " + shell_quote(wav) +
                 " -f s16le -acodec pcm_s16le " + shell_quote(raw) + " >/dev/null 2>&1"))
        return false;
    std::vector<uint8_t> bytes;
    if (!read_file(raw, &bytes) || bytes.empty()) return false;
    pcm->resize(bytes.size() / 2);
    std::memcpy(pcm->data(), bytes.data(), pcm->size() * 2);
    std::remove(wav.c_str());
    std::remove(raw.c_str());
    return true;
}

// Builds a synthetic encoding-type-2 ADX (per-frame filter index) so the 0..3
// fixed-coefficient table can be validated against vgmstream.
std::vector<uint8_t> make_synth_adx(uint32_t filter, uint32_t frames, uint32_t sample_rate) {
    const uint32_t channels = 1;
    const uint32_t data_offset = 0x28;
    std::vector<uint8_t> d(data_offset, 0);
    d[0] = 0x80; d[1] = 0x00; d[2] = 0x00; d[3] = 0x24;
    d[4] = 0x02;  // encoding: fixed coefficients -> per-frame filter index
    d[5] = 0x12;  // frame size 18
    d[6] = 0x04;  // 4-bit
    d[7] = (uint8_t)channels;
    auto put32 = [&](size_t off, uint32_t v) {
        d[off] = (uint8_t)(v >> 24); d[off + 1] = (uint8_t)(v >> 16);
        d[off + 2] = (uint8_t)(v >> 8); d[off + 3] = (uint8_t)v;
    };
    put32(0x08, sample_rate);
    put32(0x0c, frames * gh::audio::kAdxFrameSamples);
    d[0x10] = 0x01; d[0x11] = 0xf4;  // cutoff 500
    d[0x12] = 0x04; d[0x13] = 0x00;  // version 4.00, plain
    std::memcpy(&d[0x22], "(c)CRI", 6);

    uint32_t lcg = 0x1234567u + filter * 0x9e3779b9u;
    for (uint32_t f = 0; f < frames; ++f) {
        const uint16_t word = (uint16_t)(((filter & 0x07) << 13) | 0x0fff);  // scale 4096
        d.push_back((uint8_t)(word >> 8));
        d.push_back((uint8_t)(word & 0xff));
        for (uint32_t i = 0; i < 16; ++i) {
            lcg = lcg * 1103515245u + 12345u;
            d.push_back((uint8_t)(lcg >> 16));
        }
    }
    return d;
}

struct FileResult {
    bool pass = false;
    double snr_ffmpeg_libav = 0.0;  // ffmpeg flavour vs ffmpeg (expect exact)
    double snr_vgm_cri = 0.0;       // CRI flavour (trimmed) vs vgmstream
    bool ffmpeg_exact = false;
    bool vgm_exact = false;
    bool vgm_checked = false;
    bool len_ok = true;
    bool loop_ok = true;
    size_t hdr_samples = 0;
    bool loop_seen = false;
    bool loop_render_checked = false;
    bool loop_render_exact = false;
    bool loop_render_len_ok = false;
    double loop_snr = 0.0;
    size_t loop_samples = 0;      // ours, per channel
    size_t loop_ref_samples = 0;  // vgmstream, per channel
    int loop_state_mask = 0;      // bit0 carry, bit1 reset, bit2 linear matched byte-exact
    int loop_state_used = 0;      // 0 carry, 1 reset, 2 linear
    size_t pcm_samples = 0;
    size_t trimmed_samples = 0;
};

// Gates:
//  * ffmpeg byte-exactness in the libavcodec flavour -> our ADPCM core is right.
//  * vgmstream byte-exactness in the CRI flavour on the ORIGINAL encrypted file ->
//    the type-8 keystream, the CRI arithmetic and the sample_count trim are right.
//  * loop points and trimmed length must equal vgmstream's metadata.
FileResult check_file(const Options& o, const AdxKey& key, const std::string& path) {
    FileResult r;
    std::string err;
    const std::string name = stem_of(path);

    std::vector<uint8_t> src;
    if (!read_file(path, &src)) {
        std::printf("FAIL %-24s cannot read file\n", name.c_str());
        return r;
    }
    AdxHeader disk;  // as stored on disc, so the encrypted flag is still set
    if (!gh::audio::adx_parse_header(src.data(), src.size(), &disk, &err)) {
        std::printf("FAIL %-24s header error: %s\n", name.c_str(), err.c_str());
        return r;
    }

    AdxDecoder dec;
    if (!dec.init(src.data(), src.size(), AdxFlavor::Cri, key, &err)) {
        std::printf("FAIL %-24s CRI decode error: %s\n", name.c_str(), err.c_str());
        return r;
    }
    const std::vector<int16_t>& cri_full = dec.decode();
    const std::vector<int16_t>& cri = dec.decode_trimmed();

    AdxHeader fh;
    std::vector<int16_t> ff_pcm;
    if (!gh::audio::adx_decode_file(path.c_str(), AdxFlavor::Ffmpeg, key, &fh, &ff_pcm, &err)) {
        std::printf("FAIL %-24s libavcodec decode error: %s\n", name.c_str(), err.c_str());
        return r;
    }

    r.hdr_samples = disk.sample_count;
    r.loop_seen = disk.loop;
    r.pcm_samples = cri_full.size() / disk.channels;
    r.trimmed_samples = cri.size() / disk.channels;

    // Plain (unscrambled) copy for the external ffmpeg reference.
    std::vector<uint8_t> plain;
    if (disk.encrypted && !gh::audio::adx_decrypt_to_plain(src.data(), src.size(), key, disk, &plain)) {
        std::printf("FAIL %-24s decryption error\n", name.c_str());
        return r;
    }
    const std::vector<uint8_t>& plain_ref = disk.encrypted ? plain : src;
    const std::string plain_path = o.tmp + "/" + name + ".plain.adx";
    if (!write_file(plain_path, plain_ref.data(), plain_ref.size())) {
        std::printf("FAIL %-24s cannot write plain copy (disk full?)\n", name.c_str());
        return r;
    }

    if (!o.skip_ffmpeg) {
        const std::string raw = o.tmp + "/" + name + ".ffmpeg.raw";
        std::remove(raw.c_str());
        const std::string cmd = shell_quote(o.ffmpeg) + " -v error -y -i " + shell_quote(plain_path) +
                                " -f s16le -acodec pcm_s16le " + shell_quote(raw) + " >/dev/null 2>&1";
        std::vector<uint8_t> bytes;
        if (!run_cmd(cmd) || !read_file(raw, &bytes) || bytes.empty()) {
            std::printf("FAIL %-24s ffmpeg reference decode failed\n", name.c_str());
            std::remove(plain_path.c_str());
            return r;
        }
        std::vector<int16_t> ref(bytes.size() / 2);
        std::memcpy(ref.data(), bytes.data(), ref.size() * 2);
        const Compare cmp = compare_pcm(ff_pcm, ref);
        r.ffmpeg_exact = cmp.exact;
        r.snr_ffmpeg_libav = cmp.snr_db;
        if (!cmp.exact)
            std::printf("  note: %s libavcodec vs ffmpeg mismatch %zu/%zu samples\n", name.c_str(),
                        cmp.mismatches, cmp.samples);
        std::remove(raw.c_str());
    }
    std::remove(plain_path.c_str());

    if (!o.vgmstream.empty()) {
        const VgmMeta meta = vgm_meta(o, path);
        std::vector<int16_t> vgm_pcm;
        if (meta.ok && vgmstream_pcm(o, path, &vgm_pcm)) {
            r.vgm_checked = true;
            const Compare cmp = compare_pcm(cri, vgm_pcm);
            r.vgm_exact = cmp.exact;
            r.snr_vgm_cri = cmp.snr_db;
            if (!cmp.exact)
                std::printf("  note: %s CRI vs vgmstream mismatch %zu/%zu samples (snr %.2f dB)\n",
                            name.c_str(), cmp.mismatches, cmp.samples, cmp.snr_db);
            r.len_ok = (meta.total_samples == (long long)r.trimmed_samples);
            if (!r.len_ok)
                std::printf("  note: %s length: ours=%zu vgmstream=%lld\n", name.c_str(), r.trimmed_samples,
                            meta.total_samples);
            const bool vgm_loop = meta.loop;
            r.loop_ok = (disk.loop == vgm_loop) &&
                        (!disk.loop || (disk.loop_start_sample == meta.loop_start &&
                                        disk.loop_end_sample == meta.loop_end));
            if (!r.loop_ok)
                std::printf("  note: %s loop: ours(flag=%d %d..%d) vgmstream(flag=%d %lld..%lld)\n",
                            name.c_str(), (int)disk.loop, disk.loop_start_sample, disk.loop_end_sample,
                            (int)vgm_loop, meta.loop_start, meta.loop_end);
        } else {
            std::printf("  note: %s vgmstream cross-check unavailable\n", name.c_str());
            r.loop_ok = !disk.loop;  // unverified loop claim is not a pass
        }
    }

    if (o.loop_render && disk.loop) {
        r.loop_render_checked = true;
        std::printf("     loop align: start%%32=%d end%%32=%d blank=%u\n",
                    disk.loop_start_sample % 32, disk.loop_end_sample % 32, disk.loop_padding);
        std::vector<int16_t> ref;
        if (!vgmstream_loop_pcm(o, path, o.loops, &ref)) {
            std::printf("FAIL %-24s loop render: vgmstream reference failed\n", name.c_str());
            r.loop_render_len_ok = false;
        } else {
            r.loop_ref_samples = ref.size() / disk.channels;
            struct Cand { const char* name; AdxLoopState st; int bit; };
            static const Cand kCands[3] = {{"carry", AdxLoopState::Carry, 1},
                                           {"reset", AdxLoopState::ResetHeader, 2},
                                           {"linear", AdxLoopState::Linear, 4}};
            std::vector<int16_t> carry_pcm, linear_pcm;
            const size_t loop_pt_start = (size_t)disk.loop_start_sample;
            const size_t loop_pt_end = (size_t)disk.loop_end_sample;
            for (const Cand& cand : kCands) {
                if (o.loop_state != "auto" && o.loop_state != cand.name) continue;
                AdxLoopRender lr;
                lr.loops = o.loops;
                lr.tail = true;
                lr.state = cand.st;
                std::vector<int16_t> ours;
                std::string lerr;
                if (!dec.render_loop(lr, &ours, &lerr)) {
                    std::printf("FAIL %-24s loop render (%s): %s\n", name.c_str(), cand.name,
                                lerr.empty() ? "render error" : lerr.c_str());
                    continue;
                }
                if (r.loop_samples == 0) r.loop_samples = ours.size() / disk.channels;
                if (cand.bit == 1) carry_pcm = ours;
                if (cand.bit == 4) linear_pcm = ours;
                const Compare cmp = compare_pcm(ours, ref);
                const bool exact = cmp.exact && ours.size() == ref.size();
                if (exact) r.loop_state_mask |= cand.bit;
                if (o.loop_state == "auto") {
                    std::printf("     loop state %-6s ours=%-9zu ref=%-9zu snr=%7.2f dB %s\n",
                                cand.name, ours.size() / disk.channels, r.loop_ref_samples, cmp.snr_db,
                                exact ? "EXACT" : "diff");
                } else {
                    r.loop_state_used = (cand.bit == 1) ? 0 : (cand.bit == 2) ? 1 : 2;
                    r.loop_snr = cmp.snr_db;
                    r.loop_render_len_ok = (ours.size() == ref.size());
                    r.loop_render_exact = exact;
                    std::printf("LOOPRENDER %-22s state=%-6s loops=%d ours=%-9zu ref=%-9zu "
                                "snr=%.2f %s\n",
                                name.c_str(), cand.name, o.loops, ours.size() / disk.channels,
                                r.loop_ref_samples, cmp.snr_db, exact ? "PASS" : "FAIL");
                    if (!exact)
                        std::printf("     loop diff: %zu/%zu samples, len %zu vs %zu\n",
                                    cmp.mismatches, cmp.samples, ours.size(), ref.size());
                }
            }
            int carry_vs_linear_diffs = 0;
            int carry_vs_linear_max = 0;
            if (!carry_pcm.empty() && carry_pcm.size() == linear_pcm.size()) {
                for (size_t i = 0; i < carry_pcm.size(); ++i) {
                    const int d = (int)carry_pcm[i] - (int)linear_pcm[i];
                    if (d != 0) {
                        ++carry_vs_linear_diffs;
                        if (std::abs(d) > carry_vs_linear_max) carry_vs_linear_max = std::abs(d);
                    }
                }
            }
            // Loop seam: how big is the step from the last sample before the jump to
            // the first sample after it, compared with the normal step distribution in
            // the body. A seamless authored loop shows a seam near the body median.
            auto seam_of = [&](const std::vector<int16_t>& pcm) -> int {
                if (pcm.empty() || loop_pt_end == 0) return -1;
                const size_t ch = disk.channels;
                int worst = 0;
                for (size_t c = 0; c < ch; ++c) {
                    const int a = (int)pcm[(loop_pt_end - 1) * ch + c];
                    const int b = (int)pcm[loop_pt_end * ch + c];
                    if (std::abs(b - a) > worst) worst = std::abs(b - a);
                }
                return worst;
            };
            int body_p999 = 0;
            if (!linear_pcm.empty()) {
                const size_t ch = disk.channels;
                std::vector<int> deltas;
                deltas.reserve((loop_pt_end - loop_pt_start) * ch);
                for (size_t i = loop_pt_start * ch + 1; i < loop_pt_end * ch; ++i) {
                    const int d = std::abs((int)linear_pcm[i] - (int)linear_pcm[i - 1]);
                    deltas.push_back(d);
                }
                if (!deltas.empty()) {
                    const size_t k = (deltas.size() * 999) / 1000;
                    std::nth_element(deltas.begin(), deltas.begin() + (ptrdiff_t)k, deltas.end());
                    body_p999 = deltas[k];
                }
            }
            const int seam_carry = seam_of(carry_pcm);
            const int seam_linear = seam_of(linear_pcm);
            if (o.loop_state == "auto") {
                r.loop_render_exact = (r.loop_state_mask != 0);
                r.loop_snr = r.loop_render_exact ? 999.0 : 0.0;
                std::printf("LOOPRENDER %-22s loops=%d ours=%-9zu ref=%-9zu match=%s%s%s "
                            "carry_vs_linear_diff=%d max=%d\n",
                            name.c_str(), o.loops, r.loop_samples, r.loop_ref_samples,
                            (r.loop_state_mask & 1) ? "carry" : "-",
                            (r.loop_state_mask & 2) ? ",reset" : "",
                            (r.loop_state_mask & 4) ? ",linear" : "", carry_vs_linear_diffs,
                            carry_vs_linear_max);
                std::printf("     loop seam: carry=%d linear=%d body_p999=%d\n", seam_carry,
                            seam_linear, body_p999);
            } else if (r.loop_samples != 0 && r.loop_ref_samples != 0) {
                r.loop_render_len_ok = (r.loop_samples == r.loop_ref_samples);
            }
        }
    }

    if (!o.wav_dir.empty()) {
        const std::string wav = o.wav_dir + "/" + name + ".wav";
        if (!gh::audio::adx_write_wav(wav.c_str(), cri.data(), cri.size() / disk.channels, disk.channels,
                                     disk.sample_rate))
            std::printf("     warn: cannot write %s\n", wav.c_str());
        if (disk.loop) {
            // The dump must carry the same state as the gate, otherwise the WAV a
            // listener hears is not the timeline the decoder claims to produce.
            const AdxLoopState st = o.loop_state == "carry"   ? AdxLoopState::Carry
                                    : o.loop_state == "reset" ? AdxLoopState::ResetHeader
                                                              : AdxLoopState::Linear;
            AdxLoopRender lr;
            lr.loops = o.loops;
            lr.tail = true;
            lr.state = st;
            std::vector<int16_t> looped;
            std::string lerr;
            if (dec.render_loop(lr, &looped, &lerr)) {
                const std::string lwav = o.wav_dir + "/" + name + ".loop-" +
                                         (st == AdxLoopState::Carry   ? "carry"
                                          : st == AdxLoopState::ResetHeader ? "reset"
                                                                            : "linear") +
                                         ".wav";
                gh::audio::adx_write_wav(lwav.c_str(), looped.data(), looped.size() / disk.channels,
                                         disk.channels, disk.sample_rate);
            }
        }
    }

    r.pass = (o.skip_ffmpeg || (r.ffmpeg_exact && r.snr_ffmpeg_libav >= 60.0)) &&
             (!r.vgm_checked || (r.vgm_exact && r.len_ok && r.loop_ok)) &&
             (!r.loop_render_checked || r.loop_render_exact);

    std::printf("%s %-24s sr=%-6u ch=%u frames=%-7zu hdr=%-8zu pcm=%-8zu trimmed=%-8zu loop=%-3s "
                "snr_ff=%.2f snr_vgm=%.2f\n",
                r.pass ? "PASS" : "FAIL", name.c_str(), disk.sample_rate, disk.channels,
                (size_t)disk.total_frames(), r.hdr_samples, r.pcm_samples, r.trimmed_samples,
                disk.loop ? "yes" : "no", r.snr_ffmpeg_libav, r.vgm_checked ? r.snr_vgm_cri : 0.0);
    if (disk.loop)
        std::printf("     loop: start=%d end=%d start_byte=%u end_byte=%u blank=%u\n",
                    disk.loop_start_sample, disk.loop_end_sample, disk.loop_start_byte,
                    disk.loop_end_byte, disk.loop_padding);
    return r;
}

int run_synth(const Options& o) {
    if (o.vgmstream.empty()) {
        std::printf("SYNTH skipped: pass --vgmstream <vgmstream-cli> to validate filter tables\n");
        return 0;
    }
    int failures = 0;
    for (uint32_t filter = 0; filter < 4; ++filter) {
        const std::vector<uint8_t> adx = make_synth_adx(filter, 400, 48000);
        const std::string path = o.tmp + "/synth_filter" + std::to_string(filter) + ".adx";
        if (!write_file(path, adx.data(), adx.size())) {
            std::printf("SYNTH filter=%u FAIL (write)\n", filter);
            ++failures;
            continue;
        }
        AdxHeader h;
        std::vector<int16_t> mine;
        std::string err;
        if (!gh::audio::adx_decode_file(path.c_str(), AdxFlavor::Cri, AdxKey{}, &h, &mine, &err)) {
            std::printf("SYNTH filter=%u FAIL (%s)\n", filter, err.c_str());
            ++failures;
            continue;
        }
        std::vector<int16_t> ref;
        if (!vgmstream_pcm(o, path, &ref)) {
            std::printf("SYNTH filter=%u SKIP (vgmstream failed)\n", filter);
            continue;
        }
        const Compare cmp = compare_pcm(mine, ref);
        std::printf("SYNTH filter=%u %s samples=%zu mismatch=%zu snr=%.2f dB\n", filter,
                    cmp.exact ? "PASS" : "FAIL", cmp.samples, cmp.mismatches, cmp.snr_db);
        if (!cmp.exact) ++failures;
        std::remove(path.c_str());
    }
    return failures;
}

void usage() {
    std::printf(
        "usage: adx_test [options] [file.adx ...]\n"
        "  --dir <path>        directory to scan (default: /root/ports/godhand/extract/afs)\n"
        "  --limit <n>         cap number of files, evenly spread over the sorted list\n"
        "  --ffmpeg <bin>      ffmpeg binary (default: ffmpeg)\n"
        "  --vgmstream <bin>   vgmstream-cli binary for cross-check (optional)\n"
        "  --tmp <dir>         scratch dir for plain copies / refs (default: /dev/shm/adxtest)\n"
        "  --key <keystring>   ADX type-8 keystring (default: karaage = God Hand)\n"
        "  --synth             build encoding-type-2 vectors and validate filter 0..3\n"
        "  --loop-render       render intro+bodies+tail and diff against vgmstream -l N -F\n"
        "  --loop-state <s>    carry|reset|linear|auto (default auto = report which matches)\n"
        "  --loops <n>         extra loop-body plays, default 1 (vgmstream gets -l n+1)\n"
        "  --wav <dir>         dump trimmed PCM (and one looped pass) as 16-bit WAV\n"
        "  --no-ffmpeg         decode + vgmstream self-check only\n");
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::printf("error: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--dir") o.dir = next("--dir");
        else if (a == "--limit") o.limit = (uint32_t)std::strtoul(next("--limit").c_str(), nullptr, 10);
        else if (a == "--ffmpeg") o.ffmpeg = next("--ffmpeg");
        else if (a == "--vgmstream") o.vgmstream = next("--vgmstream");
        else if (a == "--tmp") o.tmp = next("--tmp");
        else if (a == "--key") o.key = next("--key");
        else if (a == "--synth") o.synth = true;
        else if (a == "--no-ffmpeg") o.skip_ffmpeg = true;
        else if (a == "--loop-render") o.loop_render = true;
        else if (a == "--loop-state") o.loop_state = next("--loop-state");
        else if (a == "--loops") o.loops = (int)std::strtol(next("--loops").c_str(), nullptr, 10);
        else if (a == "--wav") o.wav_dir = next("--wav");
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            std::printf("error: unknown option %s\n", a.c_str());
            return 2;
        } else o.files.push_back(a);
    }

    if (o.files.empty()) o.files = list_dir(o.dir);
    if (o.files.empty()) {
        std::printf("error: no .adx files found (dir=%s)\n", o.dir.c_str());
        return 2;
    }
    if (o.limit && o.files.size() > o.limit) {
        const size_t step = o.files.size() / o.limit;
        std::vector<std::string> picked;
        for (size_t i = 0; i < o.limit; ++i) picked.push_back(o.files[i * step]);
        o.files.swap(picked);
    }

    if (o.loop_state != "auto" && o.loop_state != "carry" && o.loop_state != "reset" &&
        o.loop_state != "linear") {
        std::printf("error: --loop-state must be carry|reset|linear|auto\n");
        return 2;
    }
    if (o.loops < 0) o.loops = 0;

    run_cmd("mkdir -p " + shell_quote(o.tmp));
    if (!o.wav_dir.empty()) run_cmd("mkdir -p " + shell_quote(o.wav_dir));
    if (o.vgmstream.empty()) {
        const char* env = std::getenv("VGMSTREAM_CLI");
        if (env && *env) o.vgmstream = env;
        else if (run_cmd("command -v vgmstream-cli >/dev/null 2>&1")) o.vgmstream = "vgmstream-cli";
    }
    if (!o.vgmstream.empty() && !file_exists(o.vgmstream) && o.vgmstream.find('/') != std::string::npos) {
        std::printf("warning: vgmstream-cli not found at %s, cross-check disabled\n", o.vgmstream.c_str());
        o.vgmstream.clear();
    }

    const AdxKey key = gh::audio::adx_key_from_keystring(o.key.c_str());
    std::printf("# adx_test: files=%zu key=\"%s\" (%04X/%04X/%04X) flavors=cri+libavcodec\n", o.files.size(),
                o.key.c_str(), key.start, key.mult, key.add);

    size_t pass = 0, fail = 0, ffmpeg_exact = 0, vgm_checked = 0, vgm_exact = 0;
    size_t loops = 0, loop_ok_checked = 0;
    size_t lr_checked = 0, lr_exact = 0, lr_carry = 0, lr_reset = 0, lr_linear = 0;
    size_t lr_samples = 0;
    double snr_ff_min = 1e9, snr_vgm_min = 1e9;
    size_t total_samples = 0;
    for (const std::string& f : o.files) {
        const FileResult r = check_file(o, key, f);
        if (r.pass) ++pass; else ++fail;
        if (r.ffmpeg_exact) ++ffmpeg_exact;
        if (r.vgm_checked) { ++vgm_checked; if (r.vgm_exact) ++vgm_exact; }
        if (r.hdr_samples) total_samples += r.trimmed_samples;
        if (r.snr_ffmpeg_libav < snr_ff_min) snr_ff_min = r.snr_ffmpeg_libav;
        if (r.vgm_checked && r.snr_vgm_cri < snr_vgm_min) snr_vgm_min = r.snr_vgm_cri;
        if (r.hdr_samples && r.loop_seen) { ++loops; if (r.loop_ok) ++loop_ok_checked; }
        if (r.loop_render_checked) {
            ++lr_checked;
            if (r.loop_render_exact) ++lr_exact;
            if (r.loop_state_mask & 1) ++lr_carry;
            if (r.loop_state_mask & 2) ++lr_reset;
            if (r.loop_state_mask & 4) ++lr_linear;
            lr_samples += r.loop_samples;
        }
    }

    int synth_fail = 0;
    if (o.synth) synth_fail = run_synth(o);

    if (snr_ff_min > 1e8) snr_ff_min = 0.0;
    if (snr_vgm_min > 1e8) snr_vgm_min = 0.0;
    std::printf("SUMMARY files=%zu pass=%zu fail=%zu ffmpeg_exact=%zu/%zu vgmstream_exact=%zu/%zu "
                "snr_ff_min=%.2f dB snr_vgm_min=%.2f dB trimmed_samples_total=%zu "
                "loop_files=%zu loop_verified=%zu\n",
                o.files.size(), pass, fail, ffmpeg_exact, o.files.size(), vgm_exact, vgm_checked,
                snr_ff_min, snr_vgm_min, total_samples, loops, loop_ok_checked);
    if (o.loop_render)
        std::printf("SUMMARY looprender_checked=%zu looprender_exact=%zu state_carry=%zu "
                    "state_reset=%zu state_linear=%zu loop_pcm_samples_total=%zu loops=%d\n",
                    lr_checked, lr_exact, lr_carry, lr_reset, lr_linear, lr_samples, o.loops);
    if (o.synth) std::printf("SUMMARY synth_failures=%d\n", synth_fail);
    return (fail == 0 && synth_fail == 0) ? 0 : 1;
}
