// iso9660.cpp — parse PVD + directory record ISO9660 (sector 2048) langsung dari image.
#include "iso9660.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace iso9660 {
namespace {
constexpr u32 SEC = 2048;
std::string g_path;
FILE* g_fp = nullptr;
u32 g_root_lba = 0, g_root_size = 0;

u32 rd32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

bool pread_at(u64 off, void* out, u32 n) {
    if (!g_fp) return false;
    if (std::fseek(g_fp, (long)off, SEEK_SET) != 0) return false;
    return std::fread(out, 1, n, g_fp) == n;
}

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// "IRX.IMS;1" -> "IRX.IMS" ; "MODULES" tetap
std::string strip_ver(const std::string& s) {
    std::string t = s;
    auto sc = t.find(';');
    if (sc != std::string::npos) t = t.substr(0, sc);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

std::vector<std::string> split(const std::string& p) {
    std::vector<std::string> out; std::string cur;
    for (char ch : p) {
        if (ch == '\\' || ch == '/') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Rec { std::string name; u32 lba = 0, size = 0; bool dir = false; };

// enumerasi record dalam satu direktori
std::vector<Rec> list_dir(u32 lba, u32 size) {
    std::vector<Rec> out;
    std::vector<u8> buf(size ? size : SEC);
    if (!pread_at((u64)lba * SEC, buf.data(), (u32)buf.size())) return out;
    u32 off = 0;
    while (off < buf.size()) {
        u8 len = buf[off];
        if (len == 0) { off = (off / SEC + 1) * SEC; continue; }
        if (off + len > buf.size() || len < 34) break;
        const u8* r = buf.data() + off;
        u32 rlba = rd32(r + 2), rsize = rd32(r + 10);
        bool dir = (r[25] & 0x02) != 0;
        u8 nl = r[32];
        std::string name((const char*)r + 33, nl);
        if (!(nl == 1 && (r[33] == 0 || r[33] == 1))) out.push_back(Rec{name, rlba, rsize, dir});
        off += len;
    }
    return out;
}
} // namespace

bool mount(const std::string& path) {
    reset();
    std::string p = path;
    if (p.empty()) {
        if (const char* e = std::getenv("GH_ISO")) p = e;
        else p = "/root/ports/godhand/God_Hand_USA_SLUS21503.iso";
    }
    g_fp = std::fopen(p.c_str(), "rb");
    if (!g_fp) return false;
    u8 pvd[SEC];
    if (!pread_at(16ull * SEC, pvd, SEC) || std::memcmp(pvd + 1, "CD001", 5) != 0) { reset(); return false; }
    if (pvd[0] != 1) { reset(); return false; }        // primary volume descriptor
    const u8* root = pvd + 156;
    g_root_lba = rd32(root + 2);
    g_root_size = rd32(root + 10);
    g_path = p;
    return true;
}

bool mounted() { return g_fp != nullptr; }
const std::string& image_path() { return g_path; }

void reset() {
    if (g_fp) { std::fclose(g_fp); g_fp = nullptr; }
    g_path.clear(); g_root_lba = g_root_size = 0;
}

Stat stat(const std::string& ps2_path) {
    Stat st;
    if (!g_fp) return st;
    // buang device "cdrom0:" / "cdvd:" dsb
    std::string rel = ps2_path;
    auto colon = rel.find(':');
    if (colon != std::string::npos) rel = rel.substr(colon + 1);
    auto parts = split(rel);
    u32 lba = g_root_lba, size = g_root_size;
    bool dir = true;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!dir) return st;                       // komponen setelah file = invalid
        std::string want = upper(strip_ver(parts[i]));
        if (want.empty() || want == "." || want == "..") return st;
        bool found = false;
        for (const Rec& r : list_dir(lba, size)) {
            if (upper(strip_ver(r.name)) == want) {
                lba = r.lba; size = r.size; dir = r.dir; found = true; break;
            }
        }
        if (!found) return st;
    }
    st.lba = lba; st.size = size; st.dir = dir; st.ok = true;
    return st;
}

bool lookup(const std::string& ps2_path, u32& lba, u32& size, bool& is_dir) {
    Stat s = stat(ps2_path);
    if (!s.ok) return false;
    lba = s.lba; size = s.size; is_dir = s.dir;
    return true;
}

u32 read_at(u32 lba, u32 file_size, u32 off, void* buf, u32 n) {
    if (!g_fp || off >= file_size) return 0;
    if (off + (u64)n > file_size) n = (u32)(file_size - off);
    u64 pos = (u64)lba * SEC + off;
    return pread_at(pos, buf, n) ? n : 0;
}

bool read_all(const std::string& ps2_path, std::vector<u8>& out) {
    Stat s = stat(ps2_path);
    if (!s.ok || s.dir) return false;
    out.resize(s.size);
    return s.size == 0 || read_at(s.lba, s.size, 0, out.data(), s.size) == s.size;
}

u32 read_sectors(u32 lsn, u32 nsec, void* buf, u32 sector_size) {
    if (!g_fp || nsec == 0 || sector_size == 0) return 0;
    u64 pos = (u64)lsn * (u64)sector_size;
    u64 n = (u64)nsec * (u64)sector_size;
    if (pread_at(pos, buf, (u32)n)) return (u32)n;
    return 0;
}

} // namespace iso9660
