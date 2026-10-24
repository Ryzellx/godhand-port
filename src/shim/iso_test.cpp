// iso_test.cpp — verifikasi pembaca ISO9660 terhadap ISO retail + file extract sebagai acuan.
// argv[1] = ISO (default GH_ISO / retail), argv[2] = dir extract (default /root/ports/godhand/extract)
#include "iso9660.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using u32 = uint32_t;

static int g_pass = 0, g_fail = 0;
static void ck(bool cond, const char* what) {
    std::printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    cond ? g_pass++ : g_fail++;
}

static bool read_host(const std::string& p, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    std::string iso = argc > 1 ? argv[1] : "";
    std::string root = argc > 2 ? argv[2] : "/root/ports/godhand/extract";
    ck(iso9660::mount(iso), "mount image ISO9660 (PVD CD001)");
    if (!iso9660::mounted()) { std::printf("iso_test: %d pass %d fail\n", g_pass, g_fail); return 1; }
    std::printf("image: %s\n", iso9660::image_path().c_str());

    // 1) entri root nyata (7 record + . / ..)
    struct Want { const char* path; unsigned min_size; };
    const Want want[] = {
        {"\\SYSTEM.CNF;1", 40},
        {"\\SLUS_215.03;1", 1000000},
        {"\\MODULES\\IRX.IMS;1", 1000},
        {"\\MODULES\\IOPRP310.IMG;1", 1000},
        {"\\_DATA.AFS;1", 100000000},
        {"\\0FLISTDV.DIR;1", 1},
        {"\\GODHAND.FST;1", 1000},
    };
    for (const Want& w : want) {
        auto st = iso9660::stat(w.path);
        char msg[160];
        std::snprintf(msg, sizeof msg, "lookup %s lba=%u size=%u dir=%d", w.path, st.lba, st.size, (int)st.dir);
        ck(st.ok && !st.dir && st.size >= w.min_size, msg);
    }
    ck(iso9660::stat("\\MODULES").dir, "lookup \\MODULES = direktori");
    ck(!iso9660::stat("\\TIDAK_ADA.BIN;1").ok, "lookup file tidak ada = gagal (bukan karangan)");
    ck(!iso9660::stat("\\..\\etc\\passwd").ok, "traversal ditolak");

    // 2) isi byte-identik dengan hasil ekstraksi (acuan independen)
    const char* pairs[][2] = {
        {"\\SYSTEM.CNF;1", "/SYSTEM.CNF"},
        {"\\GODHAND.FST;1", "/GODHAND.FST"},
        {"\\MODULES\\IRX.IMS;1", "/modules/irx.ims"},
        {"\\SLUS_215.03;1", "/SLUS_215.03"},
    };
    for (auto& pr : pairs) {
        std::vector<unsigned char> a, b;
        bool okA = iso9660::read_all(pr[0], a);
        bool okB = read_host(root + pr[1], b);
        char msg[200];
        std::snprintf(msg, sizeof msg, "isi %s == %s (%zu vs %zu B)", pr[0], pr[1], a.size(), b.size());
        ck(okA && okB && a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0), msg);
    }

    // 3) baca sebagian (offset non-nol) — jalur yang dipakai CDVFS read
    std::vector<unsigned char> whole, part(64);
    if (iso9660::read_all("\\GODHAND.FST;1", whole)) {
        auto st = iso9660::stat("\\GODHAND.FST;1");
        u32 got = iso9660::read_at(st.lba, st.size, 1024, part.data(), (u32)part.size());
        ck(got == 64 && std::memcmp(part.data(), whole.data() + 1024, 64) == 0, "read_at offset 1024 = 64 B benar");
        u32 tail = iso9660::read_at(st.lba, st.size, st.size - 16, part.data(), 64);
        ck(tail == 16, "read_at di ujung file dipotong ke sisa (16 B)");
    } else ck(false, "read_all GODHAND.FST");
    ck(iso9660::read_at(0, 0, 0, part.data(), 4) == 0, "read_at file kosong = 0");

    std::printf("iso_test: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
