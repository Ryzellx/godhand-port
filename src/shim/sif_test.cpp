// sif_test — Phase 8 acceptance: host FS backend + SIF RPC dispatch.
// Bukti nyata: baca file asli dari extract ISO lewat jalur RPC fileio.
#include "host_io.h"
#include "sif_rpc.h"
#include "memcard.h"
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int pass = 0, fail = 0;
static void chk(bool ok, const char* what, const std::string& detail = "") {
    std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " — ", detail.c_str());
    ok ? ++pass : ++fail;
}

using namespace sifrpc;

static std::vector<u8> u32s(std::initializer_list<u32> v) {
    std::vector<u8> out;
    for (u32 x : v) { out.push_back((u8)x); out.push_back((u8)(x >> 8)); out.push_back((u8)(x >> 16)); out.push_back((u8)(x >> 24)); }
    return out;
}
static std::vector<u8> str_req(const std::string& s, u32 extra = 0) {
    std::vector<u8> v = u32s({(u32)s.size()});
    v.insert(v.end(), s.begin(), s.end());
    auto e = u32s({extra});
    v.insert(v.end(), e.begin(), e.end());
    return v;
}
static std::string hex32(u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%08x", v); return b; }

static u32 rd32(const std::vector<u8>& v, size_t off = 0) {
    if (off + 4 > v.size()) return 0;
    return (u32)v[off] | ((u32)v[off + 1] << 8) | ((u32)v[off + 2] << 16) | ((u32)v[off + 3] << 24);
}

int main(int argc, char** argv) {
    // memcard di direktori uji yang bersih (bukan HOME user)
    setenv("GH_MEMCARD_DIR", "/root/.hermes/cache/scratch/mc_test", 1);
    std::filesystem::remove_all("/root/.hermes/cache/scratch/mc_test");
    std::string root = argc > 1 ? argv[1] : "/root/ports/godhand/extract";
    std::printf("root: %s\n", root.c_str());
    chk(hostio::init(root), "hostio::init", hostio::root());

    // 1. resolve + baca SYSTEM.CNF asli
    hostio::Stat st;
    bool ok = hostio::stat("cdrom0:\\SYSTEM.CNF;1", st);
    chk(ok && st.size > 0, "stat SYSTEM.CNF", "size=" + std::to_string(st.size));

    s32 fd = hostio::open("cdrom0:\\SYSTEM.CNF;1", 1);
    chk(fd >= 0, "open SYSTEM.CNF", "fd=" + std::to_string(fd));
    char buf[256] = {};
    u32 got = hostio::read(fd, buf, sizeof(buf) - 1);
    std::string text(buf, got);
    chk(got > 0, "read SYSTEM.CNF", std::to_string(got) + " B");
    chk(text.find("BOOT2") != std::string::npos, "SYSTEM.CNF berisi BOOT2",
        text.substr(0, 40));
    hostio::close(fd);

    // 2. path traversal harus ditolak
    std::string out;
    chk(!hostio::resolve("cdrom0:\\..\\..\\etc\\passwd", out), "traversal ditolak");

    // 3. case-insensitive (guest pakai huruf besar, host bisa beda)
    chk(hostio::resolve("cdrom0:\\system.cnf;1", out), "case-insensitive resolve", out);

    // 4. RPC: echo server roundtrip
    reset();
    bind(0xDEADBEEF, "echo", [](u32 f, const std::vector<u8>& req, std::vector<u8>& rep) {
        rep = req; return (s32)f;
    });
    std::vector<u8> rep; s32 res = 0;
    bool called = call(0xDEADBEEF, 7, {1, 2, 3}, rep, res);
    chk(called && res == 7 && rep == std::vector<u8>({1, 2, 3}), "RPC echo roundtrip");

    // 5. RPC: unknown server
    called = call(0x12345678, 0, {}, rep, res);
    chk(!called && res == -1, "unknown server ditolak");

    // 6. fileio lewat RPC (jalur yang dipakai game): open -> read -> close
    install_defaults();
    called = call(SRV_FILEIO, 0, str_req("cdrom0:\\SYSTEM.CNF;1", 1), rep, res);
    s32 rfd = (s32)rd32(rep, 0);
    chk(called && res == 0 && rfd >= 0, "fileio.open via RPC", "fd=" + std::to_string(rfd));

    called = call(SRV_FILEIO, 2, u32s({(u32)rfd, 64}), rep, res);
    u32 n = rd32(rep, 0);
    std::string data((const char*)rep.data() + 4, rep.size() - 4);
    chk(called && n > 0 && data.find("BOOT2") != std::string::npos,
        "fileio.read via RPC", std::to_string(n) + " B, BOOT2 " +
        (data.find("BOOT2") != std::string::npos ? "ketemu" : "TIDAK ADA"));

    called = call(SRV_FILEIO, 4, u32s({(u32)rfd, 8, 0}), rep, res);
    chk(called && rd32(rep, 0) == 8, "fileio.lseek via RPC", "pos=" + std::to_string(rd32(rep, 0)));

    called = call(SRV_FILEIO, 1, u32s({(u32)rfd}), rep, res);
    chk(called && res == 0, "fileio.close via RPC");

    // 7. pad + cdvd server hidup
    call(SRV_PAD, 1, {}, rep, res);
    chk(res == 0 && rd32(rep, 0) == 0xFFFF, "pad state netral", hex32(rd32(rep, 0)));
    call(SRV_CDVD, 0, {}, rep, res);
    chk(res == 0 && rd32(rep, 0) == 1, "cdvd disk ready");

    // 8. listdir root
    auto items = hostio::listdir("cdrom0:\\");
    chk(!items.empty(), "listdir root", std::to_string(items.size()) + " entri");

    // 9. memcard: mkdir + tulis + baca balik + stat + listdir + getinfo
    chk(memcard::init(), "memcard::init", memcard::root());
    chk(memcard::mkdir("mc0:/BASLUS-21503"), "memcard mkdir mc0:/BASLUS-21503");
    chk(!memcard::resolve("mc0:/../etc/passwd", out), "memcard traversal ditolak");

    s32 mfd = memcard::open("mc0:/BASLUS-21503/SAVE.DAT", 3 | 0x200);   // RDWR|CREAT
    chk(mfd >= 1000, "memcard open CREAT", "fd=" + std::to_string(mfd));
    std::vector<u8> pattern(4096);
    for (size_t i = 0; i < pattern.size(); i++) pattern[i] = (u8)(i * 7 + 3);
    u32 wrote = memcard::write(mfd, pattern.data(), (u32)pattern.size());
    chk(wrote == 4096, "memcard write 4096 B", std::to_string(wrote) + " B");
    chk(memcard::lseek(mfd, 0, 0) == 0, "memcard lseek 0");
    std::vector<u8> back(4096);
    u32 rgot = memcard::read(mfd, back.data(), (u32)back.size());
    chk(rgot == 4096 && back == pattern, "memcard read-back byte-exact",
        std::to_string(rgot) + " B");
    chk(memcard::size(mfd) == 4096, "memcard size", std::to_string(memcard::size(mfd)));
    memcard::close(mfd);

    u32 msz = 0, mmode = 0;
    bool mstat = memcard::stat("mc0:/BASLUS-21503/SAVE.DAT", msz, mmode);
    chk(mstat && msz == 4096, "memcard stat via path", std::to_string(msz) + " B");
    auto mc_items = memcard::listdir("mc0:/BASLUS-21503");
    bool has_save = false;
    for (auto& it : mc_items) if (it == "SAVE.DAT") has_save = true;
    chk(has_save, "memcard listdir", std::to_string(mc_items.size()) + " entri");

    memcard::Info mi = memcard::getinfo();
    chk(mi.used_clusters == 1 && mi.free_clusters == 14 && mi.free_bytes == 14 * 8192,
        "memcard getinfo cluster", "used=1 free=14 free_bytes=" + std::to_string(mi.free_bytes));

    // 10. getinfo lewat server mcserv (jalur RPC)
    called = call(SRV_MCSERV, 0, {}, rep, res);
    u32 rpc_free = rd32(rep, 12);
    chk(called && res == 0 && rpc_free == 14 * 8192,
        "mcserv getinfo via RPC", "total=" + std::to_string(rd32(rep, 0)) +
        " used=" + std::to_string(rd32(rep, 4)) + " free_bytes=" + std::to_string(rpc_free));

    // 11. fileio RPC dengan path mc0: (jalur yang dipakai game untuk save)
    called = call(SRV_FILEIO, 0, str_req("mc0:/BASLUS-21503/SAVE.DAT", 1), rep, res);
    s32 mc_rfd = (s32)rd32(rep, 0);
    chk(called && mc_rfd >= 1000, "fileio.open mc0: via RPC", "fd=" + std::to_string(mc_rfd));
    called = call(SRV_FILEIO, 5, u32s({(u32)mc_rfd}), rep, res);
    chk(called && rd32(rep, 0) == 4096, "fileio.size mc0: via RPC",
        std::to_string(rd32(rep, 0)) + " B");
    call(SRV_FILEIO, 1, u32s({(u32)mc_rfd}), rep, res);

    // 12. bukti fisik di disk host (read-after-write)
    std::error_code fec;
    auto hpath = std::filesystem::path(memcard::root()) / "mc0" / "BASLUS-21503" / "SAVE.DAT";
    chk(std::filesystem::exists(hpath, fec) && (u32)std::filesystem::file_size(hpath, fec) == 4096,
        "file save ada di disk host", hpath.string());

    // 13. loadfile: word versi (fno 255) — nilai yang dibaca game @0x781ba8
    called = call(SRV_LOADFILE, 255, {}, rep, res);
    chk(called && rd32(rep, 0) == 0x30303133u, "loadfile fno255 = \"3100\"",
        hex32(rd32(rep, 0)));

    // 14. loadfile: LoadModule nama nyata -> file di extract/modules/irx ter-resolve
    {
        std::string name = "cdrom0:\\MODULES\\SIO2MAN.IRX;1";
        std::vector<u8> req = u32s({0, 0});
        for (char ch : name) req.push_back((u8)ch);
        req.resize(8 + 252, 0);
        called = call(SRV_LOADFILE, 0, req, rep, res);
        chk(called && res == 0 && rd32(rep, 0) == 0, "loadfile LoadModule sio2man",
            "modul=" + std::to_string(loadfile_load_count()) + " fail=" +
            std::to_string(loadfile_load_fail_count()));
    }

    // 15. iopheap: alloc -> alamat di RAM IOP, free -> bisa dipakai ulang
    called = call(SRV_IOPHEAP, 1, u32s({1024}), rep, res);
    u32 hp1 = rd32(rep, 0);
    chk(called && hp1 >= 0x00130000u && hp1 < 0x00200000u, "iopheap alloc 1024 B",
        "addr=" + hex32(hp1));
    call(SRV_IOPHEAP, 2, u32s({hp1}), rep, res);
    called = call(SRV_IOPHEAP, 1, u32s({512}), rep, res);
    chk(called && rd32(rep, 0) == hp1, "iopheap free+reuse addr sama",
        "addr=" + hex32(rd32(rep, 0)));

    // 16. sjx: create DTX (op 0x02) -> handle = buffer IOP yang dikirim game
    called = call(SRV_SJX, 2, u32s({0, 0x003E9080u, 0x00130040u, 2176}), rep, res);
    chk(called && rd32(rep, 0) == 0x00130040u, "sjx create DTX handle=iopbuf",
        "handle=" + hex32(rd32(rep, 0)));

    const Stats& s = stats();
    std::printf("rpc stats: calls=%llu unknown=%llu in=%llu out=%llu\n",
                (unsigned long long)s.calls, (unsigned long long)s.unknown,
                (unsigned long long)s.bytes_in, (unsigned long long)s.bytes_out);
    std::printf("%s: %d/%d PASS\n", fail == 0 ? "PASS" : "FAIL", pass, pass + fail);
    return fail == 0 ? 0 : 1;
}
