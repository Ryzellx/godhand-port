#include "sif_rpc.h"
#include "host_io.h"
#include <cstring>
#include <map>

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
    put32(out, 1);                    // cmd = call
    put32(out, server);
    put32(out, func);
    put32(out, (u32)req.size());
    put32(out, 0);                    // dsize (diisi pemanggil kalau perlu)
    put32(out, 0);                    // result
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
    case 0: {  // open(path, flags)
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        u32 flags = get32(req, 4 + len);
        s32 fd = hostio::open(path, flags);
        put32(reply, (u32)fd);
        return fd >= 0 ? 0 : -1;
    }
    case 1: {  // close(fd)
        s32 fd = (s32)get32(req, 0);
        s32 r = (s32)hostio::close(fd);
        put32(reply, (u32)r);
        return r;
    }
    case 2: {  // read(fd, n) -> bytes
        s32 fd = (s32)get32(req, 0);
        u32 n = get32(req, 4);
        if (n > 65536) n = 65536;
        std::vector<u8> buf(n);
        u32 got = hostio::read(fd, buf.data(), n);
        put32(reply, got);
        reply.insert(reply.end(), buf.begin(), buf.begin() + got);
        return (s32)got;
    }
    case 3: {  // write(fd, data, n)
        s32 fd = (s32)get32(req, 0);
        u32 n = get32(req, 4);
        std::vector<u8> buf(req.begin() + std::min<size_t>(8, req.size()),
                            req.begin() + std::min<size_t>(8 + n, req.size()));
        u32 wrote = hostio::write(fd, buf.data(), (u32)buf.size());
        put32(reply, wrote);
        return (s32)wrote;
    }
    case 4: {  // lseek(fd, off, whence)
        s32 fd = (s32)get32(req, 0);
        s32 off = (s32)get32(req, 4);
        u32 wh = get32(req, 8);
        u32 pos = hostio::lseek(fd, off, wh);
        put32(reply, pos);
        return pos == 0xFFFFFFFFu ? -1 : (s32)pos;
    }
    case 5: {  // size(fd)
        s32 fd = (s32)get32(req, 0);
        u32 sz = hostio::size(fd);
        put32(reply, sz);
        return (s32)sz;
    }
    case 6: {  // stat(path)
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        hostio::Stat st;
        bool ok = hostio::stat(path, st);
        put32(reply, ok ? 1 : 0);
        put32(reply, st.size);
        put32(reply, st.mode);
        return ok ? 0 : -1;
    }
    case 7: {  // listdir(path)
        u32 len = get32(req, 0);
        std::string path = getstr(req, 4, len);
        auto items = hostio::listdir(path);
        put32(reply, (u32)items.size());
        for (auto& s : items) { put32(reply, (u32)s.size()); putstr(reply, s); }
        return (s32)items.size();
    }
    default: return -1;
    }
}

// ---------------------------------------------------------------- pad
// State netral: 0xFFFF = semua tombol lepas (PS2 pad aktif-low).
static s32 pad_handler(u32 func, const std::vector<u8>&, std::vector<u8>& reply) {
    if (func == 0) {              // init
        put32(reply, 1);
        return 0;
    }
    if (func == 1) {              // read state
        put32(reply, 0xFFFF);
        put32(reply, 0x7F7F);     // rx,ry netral
        put32(reply, 0x7F7F);     // lx,ly
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------- cdvd
static s32 cdvd_handler(u32 func, const std::vector<u8>&, std::vector<u8>& reply) {
    // func 0: disk ready -> 1 ; func 1: status -> 0
    put32(reply, func == 0 ? 1u : 0u);
    return 0;
}

void install_defaults() {
    bind(SRV_FILEIO, "fileio", fileio_handler);
    bind(SRV_PAD, "pad", pad_handler);
    bind(SRV_CDVD, "cdvd", cdvd_handler);
}

} // namespace sifrpc
