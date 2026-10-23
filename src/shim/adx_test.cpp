// adx_test — verifikasi decoder ADX terhadap referensi ffmpeg (bit-exact / selisih).
// Pakai: adx_test <file.adx> [ref_s16le.raw] [out.wav]
#include "adx.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("pakai: adx_test <file.adx> [ref.raw] [out.wav]\n"); return 2; }
    std::vector<adx::u8> file;
    adx::Header h = adx::load(argv[1], file);
    if (!h.ok) { std::printf("FAIL header: %s\n", h.err.c_str()); return 1; }
    std::printf("header: enc=%d block=%d bit=%d ch=%d rate=%u total=%u cutoff=%u "
                "coeff=%d,%d data_off=%u loop=%d(%u..%u)\n",
                h.encoding, h.block_size, h.bit_depth, h.channels, h.sample_rate,
                h.total_samples, h.cutoff, h.coeff[0], h.coeff[1], h.data_offset,
                h.has_loop ? 1 : 0, h.loop_start_sample, h.loop_end_sample);

    std::vector<adx::s16> pcm;
    adx::u32 frames = adx::decode(file, h, pcm);
    adx::Sink sink;
    sink.capture = true;
    adx::play(pcm, h.channels, sink);
    std::printf("decode: %u sampel/ch, %zu nilai pcm, %.3f s, bytes=%llu\n",
                frames, pcm.size(), h.sample_rate ? (double)frames / h.sample_rate : 0.0,
                (unsigned long long)sink.bytes);

    int fail = 0;
    if (argc > 2) {
        FILE* f = std::fopen(argv[2], "rb");
        if (!f) { std::printf("FAIL buka referensi %s\n", argv[2]); return 1; }
        std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        std::vector<adx::s16> ref((size_t)sz / 2);
        if (std::fread(ref.data(), 2, ref.size(), f) != ref.size()) { std::fclose(f); std::printf("FAIL baca ref\n"); return 1; }
        std::fclose(f);
        size_t n = pcm.size() < ref.size() ? pcm.size() : ref.size();
        size_t eq = 0, diff1 = 0; int maxd = 0; double se = 0;
        for (size_t i = 0; i < n; i++) {
            int d = (int)pcm[i] - (int)ref[i];
            if (d == 0) eq++;
            if (std::abs(d) <= 1) diff1++;
            if (std::abs(d) > maxd) maxd = std::abs(d);
            se += (double)d * d;
        }
        double snr = se > 0 ? 10.0 * std::log10((double)n * 32768.0 * 32768.0 / se) : 999.0;
        std::printf("compare: ref=%zu n=%zu exact=%zu (%.4f%%) |diff|<=1=%zu (%.4f%%) maxdiff=%d snr=%.2f dB\n",
                    ref.size(), n, eq, n ? 100.0 * eq / n : 0.0, diff1, n ? 100.0 * diff1 / n : 0.0, maxd, snr);
        if (ref.size() != pcm.size()) { std::printf("FAIL panjang beda (ref %zu vs dec %zu)\n", ref.size(), pcm.size()); fail = 1; }
        if (maxd > 1) { std::printf("FAIL selisih > 1 LSB\n"); fail = 1; }
        if (!fail) std::printf("PASS: decoder ADX identik dengan ffmpeg (toleransi 1 LSB)\n");
    }
    if (argc > 3) {
        bool w = adx::write_wav(argv[3], pcm, h.channels, h.sample_rate);
        std::printf("%s write_wav %s (%zu sampel)\n", w ? "PASS" : "FAIL", argv[3], pcm.size());
        if (!w) fail = 1;
    }
    return fail;
}
