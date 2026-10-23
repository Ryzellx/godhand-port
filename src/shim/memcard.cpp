// memcard.cpp — implementasi backend memory card (lihat memcard.h untuk kontrak).
#include "memcard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

namespace memcard {
namespace {

std::string g_root;
std::vector<FILE*> g_fd;
std::vector<u32> g_fd_size;
Stats g_st;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string default_root() {
    const char* e = std::getenv("GH_MEMCARD_DIR");
    if (e && *e) return e;
    const char* h = std::getenv("HOME");
    std::string home = (h && *h) ? h : ".";
    return home + "/.local/share/godhand-launcher/memcard";
}

// Path PS2 -> komponen host. Tolak ".." dan absolut.
bool split_components(const std::string& ps2, std::vector<std::string>& comps, std::string& card) {
    if (!is_mc_path(ps2)) return false;
    size_t colon = ps2.find(':');
    card = lower(ps2.substr(0, colon));            // "mc0" / "mc1"
    std::string rest = ps2.substr(colon + 1);
    for (char& c : rest) if (c == '\\') c = '/';
    // buang ";"-suffix (versi file ISO-style) dan leading slash
    size_t semi = rest.find(';');
    if (semi != std::string::npos) rest = rest.substr(0, semi);
    size_t i = 0;
    while (i < rest.size()) {
        while (i < rest.size() && rest[i] == '/') i++;
        size_t j = rest.find('/', i);
        if (j == std::string::npos) j = rest.size();
        std::string c = rest.substr(i, j - i);
        if (!c.empty()) {
            if (c == "." || c == "..") { g_st.denied++; return false; }
            comps.push_back(c);
        }
        i = j;
    }
    return true;
}

// Cari komponen secara case-insensitive di disk; kembalikan path aktual.
bool walk(std::string& cur, const std::string& name, bool create) {
    std::error_code ec;
    fs::path p = fs::path(cur) / name;
    if (fs::exists(p, ec)) { cur = p.string(); return true; }
    if (create) {
        fs::path parent = fs::path(cur);
        if (!fs::exists(parent, ec)) return false;
        // cari nama yang sama case-insensitive dulu supaya tidak duplikat
        for (auto& e : fs::directory_iterator(parent, ec)) {
            if (lower(e.path().filename().string()) == lower(name)) { cur = e.path().string(); return true; }
        }
        cur = p.string();
        return true;   // belum ada: pemanggil yang membuat (open CREAT / mkdir)
    }
    return false;
}

u32 allocated_bytes() {
    u64 total = 0;
    std::error_code ec;
    if (g_root.empty()) return 0;
    for (auto& e : fs::recursive_directory_iterator(g_root, ec)) {
        if (e.is_regular_file(ec)) {
            auto sz = (u64)e.file_size(ec);
            total += ((sz + CLUSTER - 1) / CLUSTER) * CLUSTER;
        }
    }
    return (u32)std::min<u64>(total, (u64)TOTAL_CLUSTERS * CLUSTER);
}

} // namespace

const Stats& stats() { return g_st; }
const std::string& root() { return g_root; }

bool is_mc_path(const std::string& ps2) {
    if (ps2.size() < 4) return false;
    std::string p = lower(ps2.substr(0, 4));
    return p == "mc0:" || p == "mc1:";
}

bool init(const std::string& dir) {
    g_root = dir.empty() ? default_root() : dir;
    std::error_code ec;
    fs::create_directories(g_root, ec);
    fs::create_directories(fs::path(g_root) / "mc0", ec);
    fs::create_directories(fs::path(g_root) / "mc1", ec);
    return fs::exists(g_root, ec);
}

bool resolve(const std::string& ps2, std::string& out, bool* is_dir, bool create) {
    std::vector<std::string> comps;
    std::string card;
    if (g_root.empty()) init();
    if (!split_components(ps2, comps, card)) return false;
    std::string cur = (fs::path(g_root) / card).string();
    std::error_code ec;
    if (!fs::exists(cur, ec)) { if (!create) return false; fs::create_directories(cur, ec); }
    for (size_t i = 0; i < comps.size(); i++) {
        bool last = (i + 1 == comps.size());
        if (!walk(cur, comps[i], create && last)) return false;
    }
    out = cur;
    if (is_dir) *is_dir = fs::is_directory(out, ec);
    return true;
}

s32 open(const std::string& ps2, u32 flags) {
    g_st.opens++;
    std::string host;
    bool creat = (flags & 0x200u) != 0;
    if (!resolve(ps2, host, nullptr, creat) || host.empty()) { g_st.notfound++; return -1; }
    std::error_code ec;
    if (fs::is_directory(host, ec)) { g_st.denied++; return -1; }
    bool wr = (flags & 3u) != 1u;      // 1 = RDONLY; 2/3 = tulis
    if (creat) {
        if (fs::exists(host, ec) && (flags & 0x400u)) std::remove(host.c_str());
        fs::create_directories(fs::path(host).parent_path(), ec);
        FILE* f = std::fopen(host.c_str(), wr ? "ab" : "rb");
        if (f) std::fclose(f);
        if (!wr) std::fprintf(stderr, "[memcard] CREAT read-only %s\n", host.c_str());
    }
    std::string mode = wr ? (fs::exists(host, ec) ? "r+b" : "w+b") : "rb";
    FILE* f = std::fopen(host.c_str(), mode.c_str());
    if (!f) { g_st.notfound++; return -1; }
    g_fd.push_back(f);
    g_fd_size.push_back((u32)fs::file_size(host, ec));
    return (s32)(g_fd.size() - 1) + 1000;      // fd unik (offset 1000: beda dari hostio)
}

static FILE* get(s32 fd) {
    s32 i = fd - 1000;
    if (i < 0 || (size_t)i >= g_fd.size() || !g_fd[i]) return nullptr;
    return g_fd[i];
}

u32 read(s32 fd, void* buf, u32 n) {
    FILE* f = get(fd);
    if (!f || !buf) return 0;
    size_t got = std::fread(buf, 1, n, f);
    g_st.reads++; g_st.bytes_read += got;
    return (u32)got;
}

u32 write(s32 fd, const void* buf, u32 n) {
    FILE* f = get(fd);
    if (!f || !buf) return 0;
    size_t put = std::fwrite(buf, 1, n, f);
    std::fflush(f);
    g_st.writes++; g_st.bytes_written += put;
    u32 i = (u32)(fd - 1000);
    if (i < g_fd_size.size()) {
        long pos = std::ftell(f);
        if (pos > (long)g_fd_size[i]) g_fd_size[i] = (u32)pos;
    }
    return (u32)put;
}

u32 lseek(s32 fd, s32 off, u32 whence) {
    FILE* f = get(fd);
    if (!f) return 0xFFFFFFFFu;
    int wh = whence == 1 ? SEEK_CUR : (whence == 2 ? SEEK_END : SEEK_SET);
    if (std::fseek(f, off, wh) != 0) return 0xFFFFFFFFu;
    long pos = std::ftell(f);
    return pos < 0 ? 0xFFFFFFFFu : (u32)pos;
}

u32 close(s32 fd) {
    s32 i = fd - 1000;
    if (i < 0 || (size_t)i >= g_fd.size() || !g_fd[i]) return 0xFFFFFFFFu;
    std::fclose(g_fd[i]);
    g_fd[i] = nullptr;
    g_st.closes++;
    return 0;
}

u32 size(s32 fd) {
    s32 i = fd - 1000;
    if (i < 0 || (size_t)i >= g_fd.size()) return 0;
    if (g_fd[i]) {
        long cur = std::ftell(g_fd[i]);
        std::fseek(g_fd[i], 0, SEEK_END);
        long e = std::ftell(g_fd[i]);
        std::fseek(g_fd[i], cur, SEEK_SET);
        if (e >= 0) g_fd_size[i] = (u32)e;
    }
    return g_fd_size[i];
}

bool stat(const std::string& ps2, u32& size_out, u32& mode_out) {
    std::string host;
    if (!resolve(ps2, host, nullptr, false)) { g_st.notfound++; return false; }
    std::error_code ec;
    if (!fs::exists(host, ec)) { g_st.notfound++; return false; }
    size_out = fs::is_directory(host, ec) ? 0 : (u32)fs::file_size(host, ec);
    mode_out = fs::is_directory(host, ec) ? 1u : 0u;
    return true;
}

std::vector<std::string> listdir(const std::string& ps2_dir) {
    std::vector<std::string> out;
    std::string host;
    if (!resolve(ps2_dir, host, nullptr, false)) { g_st.notfound++; return out; }
    std::error_code ec;
    if (!fs::is_directory(host, ec)) return out;
    for (auto& e : fs::directory_iterator(host, ec))
        out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

bool mkdir(const std::string& ps2_dir) {
    std::string host;
    if (!resolve(ps2_dir, host, nullptr, true)) { g_st.denied++; return false; }
    std::error_code ec;
    bool ok = fs::create_directories(host, ec);
    if (!ok && fs::is_directory(host, ec)) ok = true;
    if (ok) g_st.mkdirs++;
    return ok;
}

bool remove_file(const std::string& ps2) {
    std::string host;
    if (!resolve(ps2, host, nullptr, false)) { g_st.notfound++; return false; }
    std::error_code ec;
    bool ok = fs::remove(host, ec);
    if (ok) g_st.removes++;
    return ok;
}

Info getinfo() {
    Info i;
    u32 used = allocated_bytes();
    i.used_clusters = (used + CLUSTER - 1) / CLUSTER;
    if (i.used_clusters > TOTAL_CLUSTERS) i.used_clusters = TOTAL_CLUSTERS;
    i.free_clusters = TOTAL_CLUSTERS - i.used_clusters;
    i.free_bytes = i.free_clusters * CLUSTER;
    return i;
}

} // namespace memcard
