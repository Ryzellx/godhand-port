// sif_rpc.cpp — backend RPC host: fileio/pad/cdvd/mcserv. Path mc0:/mc1: -> memcard host.
#include "sif_rpc.h"
#include "host_io.h"
#include "memcard.h"
#include "iso9660.h"
#include "ee_bus.h"
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
EeWrite g_ee_wr;
} // namespace

static u64 g_cdvfs_open_ok = 0, g_cdvfs_open_miss = 0, g_cdvfs_other = 0;

void ee_writeback(u32 addr, const std::vector<u8>& d) { g_ee_wr.addr = addr; g_ee_wr.data = d; }
bool take_ee_writeback(EeWrite& out) {
    if (!g_ee_wr.addr || g_ee_wr.data.empty()) return false;
    out = g_ee_wr;
    g_ee_wr.addr = 0; g_ee_wr.data.clear();
    return true;
}
u64 cdvfs_open_ok() { return g_cdvfs_open_ok; }
u64 cdvfs_open_miss() { return g_cdvfs_open_miss; }
u64 cdvfs_other_calls() { return g_cdvfs_other; }

void reset() { g_srv.clear(); g_st = {}; g_ee_wr = {}; g_cdvfs_open_ok = g_cdvfs_open_miss = g_cdvfs_other = 0; }

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
    if (const char* want = std::getenv("GH_DUMP_SID")) {
        if (server == (u32)std::strtoul(want, nullptr, 0)) {
            static int shown = 0;
            if (shown++ < 8) {
                std::fprintf(stderr, "[DUMP sid=%08x fno=%u n=%zu] ", server, func, req.size());
                for (size_t k = 0; k < req.size() && k < 200; k++) {
                    u8 ch = req[k];
                    std::fputc((ch >= 32 && ch < 127) ? ch : '.', stderr);
                }
                std::fprintf(stderr, "\n");
            }
        }
    }
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
// padman (SID 0x80000100 = pad utama, 0x80000101 = server kedua/multitap).
// fno 0 = scePadInit/bind-reply, fno 1 = scePadRead: request 128 B dengan word0 =
// 0x12, reply dibaca caller di word[3] dan dicek `(ret >> 8) == 4` (0x39770c-0x397710)
// = PAD_STATE_STABLE(4) di libpad; kalau != 4 game mencetak error 0x46e038/0x46e060
// lalu MENGULANG panggilan -> terukur 49.307 call di 400 M instruksi tanpa render.
// Jadi word[3] WAJIB 0x000004xx (state 4 << 8 | port 0). 8 bit bawah = kode port/err.
static s32 pad_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    if (func == 0) { put32(reply, 1); return 0; }
    if (func == 1) {
        u32 port = 0;                                  // word[3] low byte = port/err
        const char* envp = std::getenv("GH_PAD_PORT");
        if (envp) port = (u32)std::strtoul(envp, nullptr, 0) & 0xFFu;
        size_t cap = req.size() >= 16 ? (req.size() < 128 ? req.size() : 128) : 16;
        reply.assign(cap, 0);
        auto w = [&](size_t off, u32 v) {
            if (off + 4 <= reply.size()) {
                reply[off] = (u8)v; reply[off + 1] = (u8)(v >> 8);
                reply[off + 2] = (u8)(v >> 16); reply[off + 3] = (u8)(v >> 24);
            }
        };
        w(0, 0x12);                                    // echo command word request
        w(4, 0x0000FFFFu);                             // tombol port 0 (aktif-low, lepas)
        w(8, 0x00007F7Fu);                             // axis X/Y netral
        w(12, (4u << 8) | port);                       // state STABLE(4) << 8 | port
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------- cdvd
// CDVDFSV (SID 0x80000592). Kontrak yang terukur dari klien libcdvd game:
//   - bind  di 0x3990C8 (sceSifBindRpc sid=0x80000592), retry-loop kalau gagal
//   - fno 0 di 0x399168: kirim 4 B, terima 16 B; klien membaca word[1],word[2],word[3]
//     (0x3991B4..0x3991F0) dan membandingkan word[3] dengan 255/254 = kode media error.
// Model: disk selalu siap, word[0]=1 (dipakai juga oleh sif_test), word[3]=0 (bukan
// 254/255). fno lain dicatat sampai semantiknya terbaca dari pemakaian nyata.
static u64 g_cdvd_unknown = 0;
static s32 cdvd_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    switch (func) {
    case 0:                       // handshake disk-ready (4 B -> 16 B)
        put32(reply, 1u);         // siap
        put32(reply, 0u);
        put32(reply, 0u);
        put32(reply, 0u);         // word[3]=0: bukan 254/255 (yang dicek klien 0x3991E0)
                                  // Percobaan 0x14 (SCECdPS2DVD) → boot malah mundur (0 modul
                                  // IOP dimuat), jadi nilai 0 dipertahankan: terukur lebih baik.
        return 0;
    default:
        g_cdvd_unknown++;
        if (std::getenv("GH_SIF_DUMP"))
            std::fprintf(stderr, "[cdvd] fno=%u send=%zu (belum dipetakan)\n", func, req.size());
        put32(reply, 0u);
        return 0;
    }
}

// ---------------------------------------------------------------- cdvdfsv
// SID 0x80000597 = cdvdfsv.irx (modul m09.elf di IOPRP310.IMG, 33.440 B). Kontrak diambil
// dari server aslinya, bukan tebakan: handler 0x7AC mendispatch menurut PANJANG paket
// (bukan fno) — 300 B = open, 296 B = operasi lain — lalu menyalin struct hasil 36 B ke
// alamat EE yang disimpan klien di paket+292 (DMA IOP->EE), dan reply 4 B = nilai balik.
// Klien game (0x398890) memakai hasil itu: cvFsOpen 0x332C84 `lw a1,36(s1)` (= info+4)
// sebagai ukuran file, dan nilai balik != 0 sebagai tanda sukses.
static std::string cdvfs_name(const std::vector<u8>& req) {
    std::string name;
    for (size_t i = 36; i < req.size() && i < 36 + 256 && req[i]; ++i) name.push_back((char)req[i]);
    return name;
}

static s32 cdvfs_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    (void)func;
    if (req.size() < 40) { put32(reply, 0); return 0; }
    std::string name = cdvfs_name(req);
    if (std::getenv("GH_CDVFS_DUMP")) {
        std::fprintf(stderr, "[cdvfs] n=%zu mode=%u name=\"%s\"\n",
                     req.size(), get32(req, req.size() >= 300 ? 296 : 292), name.c_str());
    }
    // --- open (paket 300 B) ---
    if (req.size() == 300) {
        u32 lba = 0, size = 0; bool dir = false;
        bool found = iso9660::lookup(name, lba, size, dir);
        if (!found) {                       // fallback: hasil ekstraksi di disk
            hostio::Stat hst{};
            if (hostio::stat(name, hst)) { found = true; size = hst.size; dir = (hst.mode & 1) != 0; lba = 0; }
        }
        std::vector<u8> info(36, 0);
        if (found && !dir) {
            // Field yang TERBUKTI dibaca klien: +4 = ukuran byte. +0 = status (1 = ada),
            // +8 = LBA awal. Field lain nol sampai ada konsumen yang membacanya.
            // Tata letak struct 36 B = CdlFILE: {u32 lsn; u32 size; ...} (ps2sdk).
            // Bukti A/B pada boot nyata: dengan +0 = flag 1, ADXF meminta Read_EE
            // lsn=1 (system area) lalu macet; dengan +0 = lba, permintaannya
            // lsn=2175 nsec=154 — persis \MODULES\IRX.IMS;1 (iso_test: lba 2175,
            // 314.304 B = 153,5 sektor). Jadi +0 = lsn, bukan flag. Flag "ada"
            // dibawa nilai balik RPC (= ukuran), bukan field struct.
            const bool legacy = std::getenv("GH_CDVFS_LEGACY") != nullptr;
            for (int k = 0; k < 4; k++) { info[0 + k] = (u8)((legacy ? 1u : lba) >> (8 * k)); }
            for (int k = 0; k < 4; k++) { info[4 + k] = (u8)(size >> (8 * k)); }
            for (int k = 0; k < 4; k++) { info[8 + k] = (u8)(lba >> (8 * k)); }
            put32(reply, size ? size : 1);
            g_cdvfs_open_ok++;
        } else {
            put32(reply, 0);
            g_cdvfs_open_miss++;
            if (std::getenv("GH_CDVFS_DUMP") || std::getenv("GH_SIF_DUMP"))
                std::fprintf(stderr, "[cdvfs] open MISS \"%s\" dir=%d\n", name.c_str(), (int)dir);
        }
        u32 ee = get32(req, 292) & 0x1FFFFFFFu;
        if (ee) ee_writeback(ee, info);
        return 0;
    }
    // --- selain open: belum dipetakan, jangan mengarang hasil ---
    g_cdvfs_other++;
    if (std::getenv("GH_CDVFS_DUMP") || std::getenv("GH_SIF_DUMP"))
        std::fprintf(stderr, "[cdvfs] n=%zu (belum dipetakan) name=\"%s\"\n", req.size(), name.c_str());
    put32(reply, 0);
    return 0;
}


// ---------------------------------------------------------- cdvd Read_EE (0x595)
// Ground truth: extract/modules/ioprp/m09.elf (cdvdfsv.irx 33.440 B).
//   register server : FUN_000044a4 -> FUN_00004bc8(0x58e8, 0x80000595, FUN_00003c1c, ...)
//   dispatcher      : FUN_00003c1c, string "sce_cdvd N cmd start %d", jump table .data 0x53d0
//   fno 1           : tabel[0] = 0x3cb4 -> FUN_00001dc4(paket, 24, reply, mode_dvd)
// Paket 24 B (dibangun klien EE 0x399728; dipanggil dari 0x331830 dan 0x333808):
//   +0  lsn      +4  nsec     +8  dst_EE   +12 mode[3]+pad
//   +16 ctx_EE (144 B, = end_param sceSifCallRpc)   +20 out_EE (4 B)
// Ukuran sektor dari mode[2] (0x1e98..0x1ee4): 1 -> 2328, 2 -> 2340, selain itu 2048.
// IOP asli menyalin data ke buffer IOP lalu DMA dua kali (0x2630..0x2684):
//   {0x5780 -> ctx_EE, 144 B} = {prefix, trail, dst, dst+align}; {0x5a18 -> out_EE, 4 B} = byte terkirim.
// Native: baca langsung dari ISO retail (LSN absolut) dan tulis ke RAM EE di titik yang sama.
static u64 g_cdvd_read_ok = 0, g_cdvd_read_fail = 0, g_cdvd_read_bytes = 0, g_cdvd_other = 0;

static s32 cdvd_ee_handler(u32 func, const std::vector<u8>& req, std::vector<u8>& reply) {
    if (func != 1 || req.size() < 24) {
        // fno lain di server ini belum dipetakan: jangan mengarang hasil.
        g_cdvd_other++;
        if (std::getenv("GH_SIF_DUMP") || std::getenv("GH_CDVD_DUMP"))
            std::fprintf(stderr, "[cdvd_ee] fno=%u n=%zu (belum dipetakan)\n", func, req.size());
        put32(reply, 0);
        return 0;
    }
    const u32 lsn  = get32(req, 0);
    const u32 nsec = get32(req, 4);
    const u32 dst  = get32(req, 8);
    const u32 ctx  = get32(req, 16);
    const u32 outp = get32(req, 20);
    const u8  m2   = req[14];
    const u32 ss   = (m2 == 1) ? 2328u : (m2 == 2) ? 2340u : 2048u;
    const u64 total64 = (u64)nsec * ss;
    const u32 total = (total64 > (u64)(64u << 20)) ? 0u : (u32)total64;   // sanity: <= 64 MB
    u32 got = 0;
    if (total) {
        std::vector<u8> buf(total);
        got = iso9660::read_sectors(lsn, nsec, buf.data(), ss);
        if (got) eebus::mem_write(dst & 0x1FFFFFFFu, buf.data(), got);   // IOP juga transfer parsial
    }
    // struct 144 B yang di-DMA IOP -> ctx_EE: field yang benar-benar ditulis IOP = 4 word pertama
    std::vector<u8> info(144, 0);
    const u32 prefix = (dst & 0x3F) ? 64u : 0u;
    const u32 main_len = ((dst + got) & ~0x3Fu) - (dst + prefix);
    const u32 trail = (dst + got) - ((dst + got) & ~0x3Fu);
    auto p32 = [&](size_t off, u32 v) {
        info[off] = (u8)v; info[off + 1] = (u8)(v >> 8);
        info[off + 2] = (u8)(v >> 16); info[off + 3] = (u8)(v >> 24);
    };
    p32(0, prefix);                 // s7: byte awal yang dibuang (offset & 63)
    p32(4, trail);                  // s6: byte ekor setelah blok 64 B terakhir
    p32(8, dst);                    // alamat tujuan EE
    p32(12, dst + prefix + main_len);   // batas transfer 64 B terakhir
    if (ctx) eebus::mem_write(ctx & 0x1FFFFFFFu, info.data(), info.size());
    if (outp) { u32 v = got; eebus::mem_write(outp & 0x1FFFFFFFu, &v, 4); }
    if (got == total && total) { g_cdvd_read_ok++; g_cdvd_read_bytes += got; }
    else g_cdvd_read_fail++;
    if (std::getenv("GH_SIF_DUMP") || std::getenv("GH_CDVD_DUMP")) {
        std::fprintf(stderr, "[cdvd_ee] Read_EE lsn=%u nsec=%u ss=%u dst=0x%08x -> %u/%u B ctx=0x%08x out=0x%08x\n",
                     lsn, nsec, ss, dst, got, total, ctx, outp);
    }
    put32(reply, 0);
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
    if (op == 0x00) {                       // SJX_Create (E0100401 di 0x455e20 dipakai 0x33b4e8)
        // Payload terukur (dump GH_SIF_DUMP): {sjunihandle, iopbuf, 1, tabelptr}.
        // Server IOP asli membuat objek SJX di heap IOP lalu balik handle != 0.
        // MODEL: alokasi nyata 64 B dari alokator heap IOP (0x130000..0x200000) —
        // bukan angka karangan, dan tiap panggilan dapat alamat berbeda.
        u32 h = iopheap_alloc(64);
        if (!h) { put32(reply, 0); return -1; }
        g_sjx_obj[h] = 64; g_sjx_create++;
        std::fprintf(stderr, "[sjx] op=0x00 SJX_Create -> handle 0x%08x (64 B heap IOP)\n", h);
        put32(reply, h);
        return 0;
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
    bind(SRV_CDVDFS, "cdvdfsv", cdvfs_handler);
    bind(SRV_CDVD_EE, "cdvd_ee", cdvd_ee_handler);
    bind(SRV_MCSERV, "mcserv", mcserv_handler);
    bind(SRV_PAD, "pad", pad_handler);
    bind(SRV_LOADFILE, "loadfile", loadfile_handler);
    bind(SRV_IOPHEAP, "iopheap", iopheap_handler);
    bind(SRV_SJX, "sjx", sjx_handler);
}

u64 cdvd_read_ok() { return g_cdvd_read_ok; }
u64 cdvd_read_fail() { return g_cdvd_read_fail; }
u64 cdvd_read_bytes() { return g_cdvd_read_bytes; }
u64 cdvd_other_calls() { return g_cdvd_other; }

} // namespace sifrpc
