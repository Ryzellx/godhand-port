// God Hand (PS2 SLUS-21503) — AdxStream memory proof harness
// Runtime: C++17 host | build: cmake --build build --target adx_stream_rss
//
// Streams one ADX through the runtime pull API and prints peak RSS next to the size
// the same timeline would take if it were materialised. This is the measurement
// behind the "constant memory" claim in docs/PROJECT.md (Phase 9c): peak RSS must
// come out near the file size, not near timeline_samples * channels * 2.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "adx.h"

using namespace gh::audio;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: adx_stream_rss <file.adx> [block_frames] [loops]\n");
        return 2;
    }
    const size_t block = argc > 2 ? (size_t)std::strtoul(argv[2], nullptr, 10) : 512;
    const int loops = argc > 3 ? std::atoi(argv[3]) : 2;

    std::ifstream f(argv[1], std::ios::binary);
    if (!f) {
        std::printf("error: cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const AdxKey key = adx_key_from_keystring(kAdxGodHandKeystring);
    AdxStreamConfig cfg;
    cfg.loops = loops;
    cfg.tail = true;
    cfg.state = AdxLoopState::Linear;

    AdxStream st;
    std::string err;
    if (!st.open(bytes.data(), bytes.size(), AdxFlavor::Cri, key, cfg, &err)) {
        std::printf("error: %s\n", err.c_str());
        return 1;
    }
    const size_t channels = st.header().channels;
    std::vector<int16_t> buf(block * channels);
    size_t frames = 0;
    uint64_t sum = 0;  // keep the reads from being optimised away
    while (!st.ended()) {
        const size_t n = st.read(buf.data(), block);
        frames += n;
        for (size_t i = 0; i < n * channels; ++i) sum += (uint16_t)buf[i];
    }
    struct rusage ru = {};
    getrusage(RUSAGE_SELF, &ru);

    const size_t timeline = st.timeline_samples();
    std::printf("file=%s bytes=%zu channels=%zu sample_rate=%u block=%zu loops=%d\n", argv[1],
                bytes.size(), channels, st.header().sample_rate, block, loops);
    std::printf("timeline_frames=%zu read_frames=%zu checksum=%llu\n", timeline, frames,
                (unsigned long long)sum);
    std::printf("timeline_if_materialised_bytes=%zu peak_rss_kb=%ld\n", timeline * channels * 2,
                ru.ru_maxrss);
    return frames == timeline ? 0 : 1;
}
