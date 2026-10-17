// ee_kernel.h — implementasi portable (std::thread; jalan di Linux & mingw/Win)
#include "ee_kernel.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#define POPEN_CLOSE _close
#define PREAD _read
#define PWRITE _write
#define PSEEK _lseek
#define POPEN_(p, m) _open(p, m)
#define PSOPEN(p) _open(p, _O_BINARY | _O_RDONLY)
#define PMODE_R _O_BINARY | _O_RDONLY
#define PMODE_W _O_BINARY | _O_WRONLY | _O_CREAT | _O_TRUNC
#define PMODE_RW _O_BINARY | _O_RDWR | _O_CREAT
#define PATHSEP '\\'
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define POPEN_CLOSE close
#define POPEN_ open
#define PREAD read
#define PWRITE write
#define PSEEK lseek
#define PMODE_R O_RDONLY
#define PMODE_W O_WRONLY | O_CREAT | O_TRUNC
#define PMODE_RW O_RDWR | O_CREAT
#define PATHSEP '/'
#endif

namespace eekern {
namespace {

std::mutex g_mu;
std::condition_variable g_cv;

struct Thread {
    s32 tid = 0;
    s32 prio = 1;
    u32 status = 2;                 // 0 run, 1 wait, 2 stop
    ThreadEntry entry = nullptr;
    s32 arg = 0;
    std::thread th;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
};
std::map<s32, std::shared_ptr<Thread>> g_threads;
std::atomic<s32> g_next_tid{1};
thread_local s32 t_tid = 0;         // main = 0

struct Sema { s32 count, max; };
std::map<s32, std::shared_ptr<Sema>> g_semas;
std::atomic<s32> g_next_sema{1};

struct EvFlag { u32 bits; };
std::map<s32, std::shared_ptr<EvFlag>> g_flags;
std::atomic<s32> g_next_flag{1};

std::string g_cdrom_root = ".";
std::string g_host_root = ".";

struct FileEnt { int fd = -1; };
std::map<s32, std::shared_ptr<FileEnt>> g_files;
std::atomic<s32> g_next_fd{10};

GifKickFn g_gif_kick = nullptr;

void msleep(s32 usec) {
    if (usec > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

// cdrom0:\A\B;1 -> <root>/A/B (toleransi ;1 trailing & slash campur)
std::string map_path(const char* raw, const std::string& root) {
    std::string p(raw ? raw : "");
    for (auto& c : p)
        if (c == '\\') c = PATHSEP;
    size_t semi = p.rfind(';');
    if (semi != std::string::npos) p = p.substr(0, semi);
    if (p.rfind("cdrom0:", 0) == 0) p = p.substr(7);
    if (p.rfind("host:", 0) == 0) {
        p = p.substr(5);
        while (!p.empty() && (p[0] == PATHSEP || p[0] == '/')) p.erase(0, 1);
        return g_host_root + PATHSEP + p;
    }
    while (!p.empty() && (p[0] == PATHSEP || p[0] == '/')) p.erase(0, 1);
    return root + PATHSEP + p;
}

} // namespace

// ---------- waktu ----------
s64 GetSystemTimeUsec() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

// ---------- thread ----------
namespace {
void thread_main(std::shared_ptr<Thread> t) {
    t_tid = t->tid;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t->status = 0;
    }
    g_cv.notify_all();
    if (t->entry) t->entry(t->arg);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t->status = 2;
        t->finished = true;
    }
    g_cv.notify_all();
}
} // namespace

s32 CreateThread(const ThreadParam* tp, ThreadEntry entry, s32 arg) {
    auto t = std::make_shared<Thread>();
    t->tid = g_next_tid++;
    t->prio = tp ? tp->prio : 1;
    t->entry = entry;
    t->arg = arg;
    std::lock_guard<std::mutex> lk(g_mu);
    g_threads[t->tid] = t;
    return t->tid;
}

s32 StartThread(s32 tid, s32 arg) {
    std::shared_ptr<Thread> t;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_threads.find(tid);
        if (it == g_threads.end()) return -1;
        t = it->second;
        if (t->started) return -1;
        t->started = true;
    }
    t->arg = arg;
    t->th = std::thread(thread_main, t);
    t->th.detach();
    return 0;
}

s32 ExitThread() {
    std::shared_ptr<Thread> cur;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& [id, t] : g_threads)
            if (id == t_tid) { cur = t; break; }
        if (cur) { cur->status = 2; cur->finished = true; }
    }
    g_cv.notify_all();
    return 0;   // catatan: pemanggil di thread std::detach -> return dari entry
}

s32 ExitDeleteThread() { return ExitThread(); }

s32 DeleteThread(s32 tid) {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_threads.erase(tid) ? 0 : -1;
}

s32 TerminateThread(s32 tid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_threads.find(tid);
    if (it == g_threads.end()) return -1;
    it->second->finished = true;
    it->second->status = 2;
    g_cv.notify_all();
    return 0;
}

s32 GetThreadId() { return t_tid; }

s32 SleepThread(s32 usec) {
    if (usec < 0) {
        std::unique_lock<std::mutex> lk(g_mu);
        auto self = t_tid;
        auto it = g_threads.find(self);
        if (it != g_threads.end()) it->second->status = 1;
        // sampai di-Wakeup: polling sederhana (native port: ganti dgn cv per-thread)
        while (true) {
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lk.lock();
            it = g_threads.find(self);
            if (it == g_threads.end() || it->second->status == 2) return 0;
        }
    }
    msleep(usec == 0 ? 0 : usec);
    return 0;
}

s32 WakeupThread(s32 tid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_threads.find(tid);
    if (it == g_threads.end()) return -1;
    it->second->status = 0;
    g_cv.notify_all();
    return 0;
}

s32 WaitThreadEnd(s32 tid) {
    std::unique_lock<std::mutex> lk(g_mu);
    auto it = g_threads.find(tid);
    if (it == g_threads.end()) return -1;
    g_cv.wait(lk, [&] { return it->second->finished.load(); });
    return 0;
}

s32 ChangeThreadPriority(s32 tid, s32 prio) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_threads.find(tid);
    if (it == g_threads.end()) return -1;
    it->second->prio = prio;
    return 0;
}

s32 ReferThreadStatus(s32 tid, ThreadStatus* out) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_threads.find(tid);
    if (it == g_threads.end() || !out) return -1;
    out->tid = tid;
    out->prio = it->second->prio;
    out->status = it->second->status;
    return 0;
}

// ---------- sema ----------
s32 CreateSema(const SemaParam* sp) {
    auto s = std::make_shared<Sema>();
    s->count = sp ? sp->init : 0;
    s->max = sp ? sp->max : 1;
    s32 id = g_next_sema++;
    std::lock_guard<std::mutex> lk(g_mu);
    g_semas[id] = s;
    return id;
}

s32 DeleteSema(s32 id) {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_semas.erase(id) ? 0 : -1;
}

s32 WaitSema(s32 id) {
    std::unique_lock<std::mutex> lk(g_mu);
    auto it = g_semas.find(id);
    if (it == g_semas.end()) return -1;
    auto s = it->second;
    g_cv.wait(lk, [&] { return s->count > 0; });
    s->count--;
    return 0;
}

s32 SignalSema(s32 id) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_semas.find(id);
    if (it == g_semas.end()) return -1;
    if (it->second->count < it->second->max) it->second->count++;
    g_cv.notify_all();
    return 0;
}

s32 PollSema(s32 id) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_semas.find(id);
    if (it == g_semas.end()) return -1;
    if (it->second->count <= 0) return -1;
    it->second->count--;
    return 0;
}

s32 ReferSemaStatus(s32 id, s32* out) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_semas.find(id);
    if (it == g_semas.end() || !out) return -1;
    *out = it->second->count;
    return 0;
}

// ---------- event flag ----------
s32 CreateEventFlag(const char*, u32 bits) {
    s32 id = g_next_flag++;
    std::lock_guard<std::mutex> lk(g_mu);
    g_flags[id] = std::make_shared<EvFlag>(EvFlag{bits});
    return id;
}

s32 DeleteEventFlag(s32 id) {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_flags.erase(id) ? 0 : -1;
}

s32 SetEventFlag(s32 id, u32 bits) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_flags.find(id);
    if (it == g_flags.end()) return -1;
    it->second->bits |= bits;
    g_cv.notify_all();
    return 0;
}

s32 ClearEventFlag(s32 id, u32 bits) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_flags.find(id);
    if (it == g_flags.end()) return -1;
    it->second->bits &= ~bits;
    return 0;
}

static inline bool ef_match(u32 have, u32 want, u32 mode) {
    return (mode & EF_WAIT_OR) ? ((have & want) != 0) : ((have & want) == want);
}

s32 WaitEventFlag(s32 id, u32 bits, u32 mode, u32* out) {
    std::unique_lock<std::mutex> lk(g_mu);
    auto it = g_flags.find(id);
    if (it == g_flags.end()) return -1;
    auto f = it->second;
    g_cv.wait(lk, [&] { return ef_match(f->bits, bits, mode); });
    if (out) *out = f->bits;
    if (mode & EF_WAIT_CLEAR) f->bits &= ~bits;
    return 0;
}

s32 PollEventFlag(s32 id, u32 bits, u32 mode, u32* out) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_flags.find(id);
    if (it == g_flags.end()) return -1;
    if (!ef_match(it->second->bits, bits, mode)) return -1;
    if (out) *out = it->second->bits;
    if (mode & EF_WAIT_CLEAR) it->second->bits &= ~bits;
    return 0;
}

// ---------- file ----------
void set_cdrom_root(const char* d) { g_cdrom_root = d ? d : "."; }
void set_host_root(const char* d) { g_host_root = d ? d : "."; }

s32 sceOpen(const char* path, s32 mode) {
    std::string mapped = map_path(path, g_cdrom_root);
    int fl;
    if ((mode & 3) == O_RD) fl = PMODE_R;
    else if ((mode & 3) == O_WR) fl = PMODE_W;
    else fl = PMODE_RW;
    int fd = POPEN_(mapped.c_str(), fl);
    if (fd < 0) return -1;
    s32 id = g_next_fd++;
    std::lock_guard<std::mutex> lk(g_mu);
    g_files[id] = std::make_shared<FileEnt>(FileEnt{fd});
    return id;
}

s32 sceRead(s32 fd, void* buf, u32 n) {
    std::shared_ptr<FileEnt> f;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_files.find(fd);
        if (it == g_files.end()) return -1;
        f = it->second;
    }
    s32 r = (s32)PREAD(f->fd, buf, n);
    return r;
}

s32 sceWrite(s32 fd, const void* buf, u32 n) {
    std::shared_ptr<FileEnt> f;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_files.find(fd);
        if (it == g_files.end()) return -1;
        f = it->second;
    }
    return (s32)PWRITE(f->fd, buf, n);
}

s32 sceLseek(s32 fd, s32 off, s32 whence) {
    std::shared_ptr<FileEnt> f;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_files.find(fd);
        if (it == g_files.end()) return -1;
        f = it->second;
    }
    int w = whence == 0 ? 0 : (whence == 1 ? 1 : 2);
    return (s32)PSEEK(f->fd, off, w);
}

s32 sceClose(s32 fd) {
    std::shared_ptr<FileEnt> f;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_files.find(fd);
        if (it == g_files.end()) return -1;
        f = it->second;
        g_files.erase(it);
    }
    return POPEN_CLOSE(f->fd) == 0 ? 0 : -1;
}

// ---------- misc ----------
void scePrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
}

u32 FlushCache(u32) { return 0; }

void set_gif_kick(GifKickFn fn) { g_gif_kick = fn; }

void dma_ch2_kick(const void* packet, u32 qwc) {
    if (g_gif_kick) g_gif_kick(packet, qwc);
}

} // namespace eekern
