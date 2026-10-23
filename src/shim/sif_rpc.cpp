// sif_rpc.cpp — backend RPC host: fileio/pad/cdvd/mcserv. Path mc0:/mc1: -> memcard host.
#include "sif_rpc.h"
#include "host_io.h"
#include "memcard.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <cstdio>
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

void install_defaults() {
    memcard::init();
    bind(SRV_FILEIO, "fileio", fileio_handler);
    bind(SRV_CDVD, "cdvd", cdvd_handler);
    bind(SRV_MCSERV, "mcserv", mcserv_handler);
    bind(SRV_PAD, "pad", pad_handler);
}

} // namespace sifrpc
