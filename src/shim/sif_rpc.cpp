// sif_rpc.cpp — backend RPC host: fileio/pad/cdvd/mcserv. Path mc0:/mc1: -> memcard host.
#include "sif_rpc.h"
#include "host_io.h"
#include "memcard.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <cstdio>
#include <filesystem>
#include <cstdlib>

namespace sifrpc {
namespace {
struct Server { std::string name; Handler h; };
std::map<u32, Server> g_srv;
Stats g_st;

void put32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x & 0xFF)); v.push_back((u8)((x >> 8) & 0xFF));
    v.push_back((u8)((x >> 16) & 0xFF)); v.push_back((u8)((x >> 24) & 0xFF));
}
u32 get32(const std::vector<u8>& v, size_t off) {
    if (off + 4 > v.size()) return 0;
    return (u32)v[off] | ((u32)v[off + 1] << 8) | ((u32)v[off + 2] << 16) | ((u32)v[off + 3] << 24);
}
std::string getstr(const std::vector<u8>& v, size_t off, u32 len) {
    if (off + len > v.size()) len = (u32)(v.size() - off);
    return std::string((const char*)v.data() + off, len);
}
void putstr(std::vector<u8>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

// ---- vfs: satu pintu untuk cdrom/host/memcard -------------------------------
s32 vfs_open(const std::string& path, u32 flags) {
    return memcard::is_mc_path(path) ? memcard::open(path, flags) : hostio::open(path, flags);
}
u32 vfs_read(s32 fd, void* b, u32 n) {
    return fd >= 1000 ? memcard::read(fd, b, n) : hostio::read(fd, b, n);
}
u32 vfs_write(s32 fd, const void* b, u32 n) {
    return fd >= 1000 ? memcard::write(fd, b, n) : hostio::write(fd, b, n);
}
u32 vfs_lseek(s32 fd, s32 off, u32 wh) {
    return fd >= 1000 ? memcard::lseek(fd, off, wh) : hostio::lseek(fd, off, wh);
}
u32 vfs_close(s32 fd) {
    return fd >= 1000 ? memcard::close(fd) : hostio::close(fd);
}
u32 vfs_size(s32 fd) {
    return fd >= 1000 ? memcard::size(fd) : hostio::size(fd);
}
} // namespace

void reset() { g_srv.clear(); g_st = {}; }

bool bind(u32 server, const std::string& name, Handler h) {
    g_srv[server] = Server{name, std::move(h)};
    return true;
}
bool bound(u32 server) { return g_srv.count(server) > 0; }
std::string server_name(u32 server) {
    auto it = g_srv.find(server);
    return it == g_srv.end() ? std::string("?") : it->second.name;
}

std::vector<u8> encode(u32 server, u32 func, const std::vector<u8>& req) {
    std::vector<u8> out;
    out.reserve(24 + req.size());
    put32(out, 1);
    put32(out, server);
    put32(out, func);
    put32(out, (u32)req.size());
    put32(out, 0);
    put32(out, 0);
    out.insert(out.end(), req.begin(), req.end());
    return out;
}

bool decode(const std::vector<u8>& pkt, Header& h, std::vector<u8>& payload) {
    if (pkt.size() < 24) return false;
    h.cmd = get32(pkt, 0); h.server = get32(pkt, 4); h.func = get32(pkt, 8);
    h.psize = get32(pkt, 12); h.dsize = get32(pkt, 16); h.result = get32(pkt, 20);
    if (h.cmd != 1 || h.psize > pkt.size() - 24) return false;
    payload.assign(pkt.begin() + 24, pkt.begin() + 24 + h.psize);
    return true;
}

bool call(u32 server, u32 func, const std::vector<u8>& req, std::vector<u8>& reply, s32& result_out) {
    g_st.calls++;
    g_st.bytes_in += req.size();
    auto it = g_srv.find(server);
    if (it == g_srv.end()) { g_st.unknown++; result_out = -1; reply.clear(); return false; }
    reply.clear();
    s32 r = it->second.h(func, req, reply);
    result_out = r;
    g_st.bytes_out += reply.size();
    return true;
}

const Stats& stats() { return g_st; }

// ---------------------------------------------------------------- fileio
// func: 0=open 1=close 2=read 3=write 4=lseek 5=size 6=stat 7=listdir
static s32 fileio_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    switch (func) {
    case 0: {
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        u32 flags = get32(req, 4 + len);
        s32 fd = vfs_open(path, flags);
        put32(reply, (u32)fd);
        return fd >= 0 ? 0 : -1;
    }
    case 1: {
        s32 fd = (s32)get32(req, 0);
        s32 r = (s32)vfs_close(fd);
        put32(reply, (u32)r);
        return r;
    }
    case 2: {
        s32 fd = (s32)get32(req, 0);
        u32 n = get32(req, 4);
        if (n > 65536) n = 65536;
        std::vector<u8> buf(n);
        u32 got = vfs_read(fd, buf.data(), n);
        if (std::getenv("GH_RPC_DEBUG"))
            std::fprintf(stderr, "[fileio] read fd=%d n=%u got=%u reqsz=%zu\n", fd, n, got, req.size());
        put32(reply, got);
        reply.insert(reply.end(), buf.begin(), buf.begin() + got);
        return (s32)got;
    }
    case 3: {
        s32 fd = (s32)get32(req, 0);
        u32 n = get32(req, 4);
        std::vector<u8> buf(req.begin() + std::min<size_t>(8, req.size()),
                            req.begin() + std::min<size_t>(8 + n, req.size()));
        u32 wrote = vfs_write(fd, buf.data(), (u32)buf.size());
        put32(reply, wrote);
        return (s32)wrote;
    }
    case 4: {
        s32 fd = (s32)get32(req, 0);
        s32 off = (s32)get32(req, 4);
        u32 wh = get32(req, 8);
        u32 pos = vfs_lseek(fd, off, wh);
        put32(reply, pos);
        return pos == 0xFFFFFFFFu ? -1 : (s32)pos;
    }
    case 5: {
        s32 fd = (s32)get32(req, 0);
        u32 sz = vfs_size(fd);
        put32(reply, sz);
        return (s32)sz;
    }
    case 6: {
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        u32 sz = 0, mode = 0;
        bool ok;
        if (memcard::is_mc_path(path)) ok = memcard::stat(path, sz, mode);
        else { hostio::Stat st; ok = hostio::stat(path, st); sz = st.size; mode = st.mode; }
        put32(reply, ok ? 1 : 0);
        put32(reply, sz);
        put32(reply, mode);
        return ok ? 0 : -1;
    }
    case 7: {
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        auto items = memcard::is_mc_path(path) ? memcard::listdir(path) : hostio::listdir(path);
        put32(reply, (u32)items.size());
        for (auto& s : items) { put32(reply, (u32)s.size()); putstr(reply, s); }
        return (s32)items.size();
    }
    case 8: {   // mkdir(path)
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        bool ok = memcard::is_mc_path(path) && memcard::mkdir(path);
        put32(reply, ok ? 1 : 0);
        return ok ? 0 : -1;
    }
    case 9: {   // remove(path)
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        bool ok = memcard::is_mc_path(path) && memcard::remove_file(path);
        put32(reply, ok ? 1 : 0);
        return ok ? 0 : -1;
    }
    default: return -1;
    }
}

// ---------------------------------------------------------------- mcserv
// func 0 = getinfo -> {total_clusters, used, free, free_bytes, formatted}
static s32 mcserv_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    switch (func) {
    case 0: {
        memcard::Info i = memcard::getinfo();
        put32(reply, i.total_clusters); put32(reply, i.used_clusters);
        put32(reply, i.free_clusters);  put32(reply, i.free_bytes);
        put32(reply, i.formatted ? 1u : 0u);
        return 0;
    }
    case 1: {   // mkdir(path)
        u32 len = get32(req, 0);
        std::string p = getstr(req, 4, len);
        return memcard::mkdir(p) ? 0 : -1;
    }
    case 2: {   // remove(path)
        u32 len = get32(req, 0);
        std::string p = getstr(req, 4, len);
        return memcard::remove_file(p) ? 0 : -1;
    }
    default: return -1;
    }
}

// ---------------------------------------------------------------- pad
static s32 pad_handler(u32 func, const std::vector<u8>&, std::vector<u8>& reply) {
    if (func == 0) { put32(reply, 1); return 0; }
    if (func == 1) {
        put32(reply, 0xFFFF);   // tombol aktif-low: semua lepas
        put32(reply, 0x7F7F);
        put32(reply, 0x7F7F);
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------- cdvd
static s32 cdvd_handler(u32 func, const std::vector<u8>&, std::vector<u8>& reply) {
    put32(reply, func == 0 ? 1u : 0u);
    return 0;
}

// ---------------------------------------------------------------- loadfile
// Ground truth: IOPRP310.IMG, modul ROMDIR "LOADFILE" (ELF #12). Tabel jump fno 0..10
// di .data 0x1ca8 (0x150 0x240 0x3d0 0x360 0x1fc 0x2f8 0x434 0x4ac 0x4f0 0x520 0x54c),
// dan .data 0x1c9c = ASCII "3100" — nilai yang dikembalikan fno 255 (versi
// PsIIloadfile3100). Dispatch asli: fno>=11 & fno!=255 -> return 0 tanpa tulis reply.
static u64 g_lf_other = 0;
static u64 g_lf_load = 0, g_lf_load_fail = 0;
static std::vector<std::string> g_lf_mods;   // "nama ukuran"

// fno 0 = LoadModule: payload = {u32 size; u32 pad; char name[252]; ...}
// (layout dari dispatch LOADFILE IOPRF 0x150: name = req+8, param = req+0x104).
// Native: file dibuka dari extract/modules/irx (cdrom0:\MODULES\*.IRX), ukuran
// dibaca nyata; modul dicatat sebagai "dimuat". IOP asli memanggil modload —
// link/relokasi tetap di sisi IOP yang tidak kita emulasi.
static s32 loadfile_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    if (func == 255) {           // versi modul: 4 byte "3100" (0x30303133)
        put32(reply, 0x30303133u);
        return 0;
    }
    if (func == 0) {
        std::string ps2name;
        for (size_t i = 8; i < req.size() && req[i]; i++) {
            if (ps2name.size() >= 252) break;
            ps2name.push_back((char)req[i]);
        }
        std::string path;
        u32 fsize = 0;
        // ISO retail menyimpan modul di dalam MODULES\IRX.IMS; hasil ekstraksi ada di
        // <root>/modules/irx/<nama>.IRX. resolve() dulu, lalu fallback ke folder itu.
        bool resolved = !ps2name.empty() && hostio::resolve(ps2name, path);
        if (!resolved && !ps2name.empty()) {
            std::string base = ps2name;
            auto sl = base.find_last_of("\\/:");
            if (sl != std::string::npos) base = base.substr(sl + 1);
            auto sc = base.find(';');
            if (sc != std::string::npos) base = base.substr(0, sc);
            for (auto& ch : base) ch = (char)std::tolower((unsigned char)ch);
            std::string dir = hostio::root() + "/modules/irx";
            std::error_code ec;
            for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
                std::string fn = e.path().filename().string();
                std::string lfn = fn;
                for (auto& ch : lfn) ch = (char)std::tolower((unsigned char)ch);
                if (lfn == base) { path = e.path().string(); resolved = true; break; }  // SDRDRV.IRX vs sdrdrv
            }
        }
        if (resolved) {
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (f) { std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fclose(f);
                     if (n > 0) fsize = (u32)n; }
        }
        if (fsize) {
            g_lf_load++;
            auto sl = path.find_last_of('/');
            g_lf_mods.push_back((sl == std::string::npos ? path : path.substr(sl + 1)) +
                                " " + std::to_string(fsize) + "B");
            std::fprintf(stderr, "[loadfile] LOAD ok \"%s\" -> %s (%u B)\n",
                         ps2name.c_str(), path.c_str(), fsize);
            put32(reply, 0);          // id modul 0 = sukses (model; bukan id karangan)
            return 0;
        }
        g_lf_load_fail++;
        std::fprintf(stderr, "[loadfile] LOAD GAGAL \"%s\" (resolve=%s)\n",
                     ps2name.c_str(), path.c_str());
        put32(reply, (u32)-1);
        return -1;
    }
    g_lf_other++;
    put32(reply, 0);
    return 0;
}
u64 loadfile_other_calls() { return g_lf_other; }
u64 loadfile_load_count() { return g_lf_load; }
u64 loadfile_load_fail_count() { return g_lf_load_fail; }
const char* loadfile_modules_csv() {
    static std::string s;
    s.clear();
    for (size_t i = 0; i < g_lf_mods.size(); i++) { if (i) s += ", "; s += g_lf_mods[i]; }
    return s.c_str();
}

// ---------------------------------------------------------------- iopheap
// SID 0x80000003 — manajer heap RAM IOP. Verifikasi dari kode game sendiri (bukan
// tebakan): 0x3b5968 = sceSifAllocIopHeap -> call(cd@0x7817c0, fno=1, send u32 size,
// recv u32 addr); 0x3b59d8 = sceSifFreeIopHeap -> fno=2, send u32 addr, recv u32 status.
// Tanpa backend ini autostub mengembalikan 0 = "gagal alokasi" dan game masuk jalur
// fatal: log @0x455dc8 lalu spin mati di 0x33b2b0 (terukur 279.485.177 iterasi).
//
// Alokator: bump + free-list di ruang alamat RAM IOP 2 MB. Heap mulai 0x130000 —
// di atas buffer SIF model (SD_BUF 0x110000, SD_CBUF 0x120000) dan di bawah 0x200000.
namespace {
constexpr u32 IOP_HEAP_BASE = 0x00130000u, IOP_HEAP_END = 0x00200000u, IOP_HEAP_ALIGN = 64u;
u32 g_hp = IOP_HEAP_BASE;
std::map<u32, u32> g_hp_live;             // addr -> size
std::multimap<u32, u32> g_hp_free;        // size -> addr (first-fit)
u64 g_hp_calls = 0, g_hp_fails = 0;

u32 iopheap_alloc(u32 size) {
    if (!size) return 0;
    u32 n = (size + IOP_HEAP_ALIGN - 1) & ~(IOP_HEAP_ALIGN - 1);
    for (auto it = g_hp_free.lower_bound(n); it != g_hp_free.end(); ++it) {
        u32 addr = it->second, blk = it->first;
        g_hp_free.erase(it);
        if (blk > n) g_hp_free.emplace(blk - n, addr + n);
        g_hp_live[addr] = n;
        return addr;
    }
    if (g_hp + n > IOP_HEAP_END) { g_hp_fails++; return 0; }
    u32 addr = g_hp; g_hp += n;
    g_hp_live[addr] = n;
    return addr;
}

s32 iopheap_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    u32 a0 = req.size() >= 4 ? (u32)req[0] | ((u32)req[1] << 8) | ((u32)req[2] << 16) | ((u32)req[3] << 24) : 0;
    g_hp_calls++;
    if (func == 1) {                       // AllocIopHeap(size) -> addr (0 = gagal)
        u32 addr = iopheap_alloc(a0);
        std::fprintf(stderr, "[iopheap] alloc %u -> 0x%08x\n", a0, addr);
        put32(reply, addr);
        return addr ? 0 : -1;
    }
    if (func == 2) {                       // FreeIopHeap(addr) -> status
        auto it = g_hp_live.find(a0);
        if (it != g_hp_live.end()) { g_hp_free.emplace(it->second, a0); g_hp_live.erase(it);
                                     std::fprintf(stderr, "[iopheap] free 0x%08x\n", a0); }
        put32(reply, 0);
        return 0;
    }
    put32(reply, 0);
    return 0;
}
u64 iopheap_calls() { return g_hp_calls; }
u64 iopheap_fails() { return g_hp_fails; }
} // namespace

// ---------------------------------------------------------------- sjx (CRI/SJX)
// SID 0x90000200 — server sound middleware di sisi IOP. Identifikasi dari kode game:
// string "E0100301: SJX_Init can't allocate IOP Heap" dan "E0100302: SJX_Init can't
// create DTX" ada di .rodata 0x455dc8/0x455df8, dipakai SJX_Init @0x33b218.
// Urutan yang terukur: alloc IOP heap 2256 B (0x3b5968, SID 0x80000003 fno 1) lalu
// create DTX (fno 2) dengan payload 16 B:
//   +0  u32 0
//   +4  u32 workbuf EE (0x003e9080)
//   +8  u32 iop buffer (0x00130040 = heap+64 — server membuat objek di dalam buffer ini)
//   +12 u32 ukuran (2176)
// Reply = 4 B handle. Tanpa backend (autostub 0) game masuk jalur fatal 0x33b308
// (log + spin). MODEL: handle = alamat buffer IOP yang dikirim game (server-side object
// memang hidup di buffer itu); pemakaian handle berikutnya masih lewat RPC ke server ini.
static std::map<u32, u32> g_sjx_obj;     // handle -> ukuran objek
static u64 g_sjx_create = 0, g_sjx_ops = 0;

// Protokol suara game (terpetakan dari 0x332670): fno RPC = 0x400 | opcode, payload =
// array u32 argumen (jumlah = send_size/4), reply = array u32 (jumlah = recv_size/4).
// Opcode 0x22 = SJUNI_CreateRmt (dipanggil 0x33aed0), 0x23 = delete/pasangan.
s32 sjx_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    u32 op = (func >= 0x400u) ? (func & 0xFFu) : func;
    std::vector<u32> a;
    for (size_t i = 0; i + 4 <= req.size(); i += 4)
        a.push_back((u32)req[i] | ((u32)req[i + 1] << 8) | ((u32)req[i + 2] << 16) | ((u32)req[i + 3] << 24));
    g_sjx_ops++;
    if (getenv("GH_SIF_DUMP")) {
        std::fprintf(stderr, "[sjx] op=0x%02x argc=%zu:", op, a.size());
        for (size_t i = 0; i < a.size() && i < 12; i++) std::fprintf(stderr, " %08x", a[i]);
        std::fprintf(stderr, "\n");
    }
    if (op == 0x02 || op == 0x22) {         // 0x02 = create DTX, 0x22 = SJUNI_CreateRmt
        // op 0x02: args={0, workbufEE, iopbuf, size} -> iopbuf = a[2]
        // op 0x22: args={tipe, iopbuf, size}          -> iopbuf = a[1]
        u32 buf = (op == 0x02) ? (a.size() > 2 ? a[2] : 0) : (a.size() > 1 ? a[1] : 0);
        u32 h = buf ? buf : 0x00130000u;
        g_sjx_obj[h] = 1; g_sjx_create++;
        std::fprintf(stderr, "[sjx] op=0x%02x -> handle 0x%08x\n", op, h);
        put32(reply, h);
        return 0;
    }
    put32(reply, 0);
    return 0;
}
u64 sjx_create_count() { return g_sjx_create; }

void install_defaults() {
    memcard::init();
    bind(SRV_FILEIO, "fileio", fileio_handler);
    bind(SRV_CDVD, "cdvd", cdvd_handler);
    bind(SRV_MCSERV, "mcserv", mcserv_handler);
    bind(SRV_PAD, "pad", pad_handler);
    bind(SRV_LOADFILE, "loadfile", loadfile_handler);
    bind(SRV_IOPHEAP, "iopheap", iopheap_handler);
    bind(SRV_SJX, "sjx", sjx_handler);
}

} // namespace sifrpc
