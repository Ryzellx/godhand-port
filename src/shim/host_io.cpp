#include "host_io.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace hostio {
namespace {
std::string g_root = "/root/ports/godhand/extract";
struct Fd { FILE* fp = nullptr; std::string path; u32 base = 0; };
Fd g_fd[64];

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::vector<std::string> split_path(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '\\' || c == '/') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// "FILE.EXT;1" -> "FILE.EXT" ; "DIR" tetap "DIR"
std::string strip_version(const std::string& name) {
    auto sc = name.find(';');
    return sc == std::string::npos ? name : name.substr(0, sc);
}

std::string strip_device(const std::string& p) {
    auto c = p.find(':');
    if (c == std::string::npos) return p;
    return p.substr(c + 1);
}
} // namespace

bool init(const std::string& root) {
    if (!root.empty()) g_root = root;
    else if (const char* env = std::getenv("GH_CDROOT")) g_root = env;
    std::error_code ec;
    return fs::is_directory(g_root, ec);
}

const std::string& root() { return g_root; }

bool resolve(const std::string& ps2_path, std::string& out, bool* is_dir) {
    if (is_dir) *is_dir = false;
    std::string rel = strip_device(ps2_path);
    fs::path cur(g_root);
    auto parts = split_path(rel);
    if (parts.empty()) { out = g_root; if (is_dir) *is_dir = true; return true; }

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string want = lower(strip_version(parts[i]));
        if (want == ".." || want == "." ) return false;   // traversal ditolak
        std::error_code ec;
        bool found = false;
        fs::path next;
        if (fs::is_directory(cur, ec)) {
            for (auto& e : fs::directory_iterator(cur, ec)) {
                if (lower(e.path().filename().string()) == want) { next = e.path(); found = true; break; }
            }
        }
        if (!found) {
            // fallback: nama asli (guest file mungkin tak ada di host)
            fs::path cand = cur / parts[i];
            if (fs::exists(cand, ec)) { next = cand; found = true; }
        }
        if (!found) return false;
        cur = next;
    }
    out = cur.string();
    std::error_code ec;
    if (fs::is_directory(cur, ec)) { if (is_dir) *is_dir = true; }
    return true;
}

bool stat(const std::string& ps2_path, Stat& st) {
    std::string host; bool isdir = false;
    if (!resolve(ps2_path, host, &isdir)) { st = {}; return false; }
    std::error_code ec;
    if (!fs::exists(host, ec)) { st = {}; return false; }
    st.ok = true;
    st.size = isdir ? 0 : (u32)fs::file_size(host, ec);
    st.mode = isdir ? 1u : 0u;
    return true;
}

s32 open(const std::string& ps2_path, u32 flags) {
    std::string host;
    if (!resolve(ps2_path, host)) return (s32)-1;
    int slot = -1;
    for (int i = 3; i < 64; ++i) if (!g_fd[i].fp) { slot = i; break; }
    if (slot < 0) return (s32)-1;
    const char* mode = (flags & 3) == 2 ? "wb" : ((flags & 3) == 3 ? "r+b" : "rb");
    if ((flags & 0x200) && (flags & 3) != 1) mode = "w+b";
    FILE* fp = std::fopen(host.c_str(), mode);
    if (!fp) {
        // cdrom selalu read-only: fallback ke rb
        fp = std::fopen(host.c_str(), "rb");
        if (!fp) return (s32)-1;
    }
    g_fd[slot].fp = fp;
    g_fd[slot].path = host;
    g_fd[slot].base = 0;
    return slot;
}

u32 read(s32 fd, void* buf, u32 n) {
    if (fd < 0 || fd >= 64 || !g_fd[fd].fp) return 0;
    size_t got = std::fread(buf, 1, n, g_fd[fd].fp);
    return (u32)got;
}

u32 write(s32 fd, const void* buf, u32 n) {
    if (fd < 0 || fd >= 64 || !g_fd[fd].fp) return 0;
    return (u32)std::fwrite(buf, 1, n, g_fd[fd].fp);
}

u32 lseek(s32 fd, s32 offset, u32 whence) {
    if (fd < 0 || fd >= 64 || !g_fd[fd].fp) return 0xFFFFFFFFu;
    int w = whence == 0 ? SEEK_SET : (whence == 1 ? SEEK_CUR : SEEK_END);
    if (std::fseek(g_fd[fd].fp, (long)offset, w) != 0) return 0xFFFFFFFFu;
    long pos = std::ftell(g_fd[fd].fp);
    return pos < 0 ? 0xFFFFFFFFu : (u32)pos;
}

u32 close(s32 fd) {
    if (fd < 0 || fd >= 64 || !g_fd[fd].fp) return (u32)-1;
    std::fclose(g_fd[fd].fp);
    g_fd[fd].fp = nullptr; g_fd[fd].path.clear(); g_fd[fd].base = 0;
    return 0;
}

u32 size(s32 fd) {
    if (fd < 0 || fd >= 64 || !g_fd[fd].fp) return 0;
    long cur = std::ftell(g_fd[fd].fp);
    std::fseek(g_fd[fd].fp, 0, SEEK_END);
    long end = std::ftell(g_fd[fd].fp);
    std::fseek(g_fd[fd].fp, cur, SEEK_SET);
    return end < 0 ? 0 : (u32)end;
}

u32 seek_base(s32 fd) {
    return (fd >= 0 && fd < 64) ? g_fd[fd].base : 0;
}

std::vector<std::string> listdir(const std::string& ps2_dir) {
    std::vector<std::string> out;
    std::string host;
    if (!resolve(ps2_dir, host)) return out;
    std::error_code ec;
    if (!fs::is_directory(host, ec)) return out;
    for (auto& e : fs::directory_iterator(host, ec))
        out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace hostio
