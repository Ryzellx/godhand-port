// ee_kernel.h — shim kernel EE buat port God Hand native (POSIX + mingw portable)
// Sumber kontrak: ~/ports/godhand/extract/syscall_contract.csv (156 situs, 141 nomor,
// nama ps2sdk verified). Subset yang dipakai game (thread/sema/eventflag/file/print)
// diimplement di sini; sisanya (intc/dmac/sif) menyusul sebagai no-op trace dulu.
#pragma once
#include <cstdint>

namespace eekern {

using s8 = int8_t; using u8 = uint8_t;
using s16 = int16_t; using u16 = uint16_t;
using s32 = int32_t; using u32 = uint32_t;
using s64 = int64_t; using u64 = uint64_t;

// ---- waktu ----
s64  GetSystemTimeUsec();          // epoch USEC (GetCPUTick::per usec juga disini)

// ---- thread (syscall 32-60) ----
using ThreadEntry = s32 (*)(s32 arg);
struct ThreadParam {
    const char* name = "ee";
    s32 prio = 1;                  // makin kecil makin atas (urut FIFO sederhana)
    u32 stack = 64 * 1024;
};
s32 CreateThread(const ThreadParam* tp, ThreadEntry entry, s32 arg); // -> tid (>0), <0 gagal
s32 StartThread(s32 tid, s32 arg);
s32 ExitThread();                  // thread keluar sendiri
s32 ExitDeleteThread();
s32 DeleteThread(s32 tid);
s32 TerminateThread(s32 tid);
s32 GetThreadId();                 // tid thread berjalan (0 utk main)
s32 SleepThread(s32 usec);         // -1 = selamanya (sampai Wakeup),0 = yield
s32 WakeupThread(s32 tid);
s32 WaitThreadEnd(s32 tid);
s32 ChangeThreadPriority(s32 tid, s32 prio);
s32 ReferThreadStatus(s32 tid, struct ThreadStatus* out);

struct ThreadStatus { s32 tid; s32 prio; u32 status; /*0=run 1=wait 2=stop*/ };

// ---- semaphore (syscall 64-73) ----
struct SemaParam {
    const char* name = "sema";
    s32 init = 0;
    s32 max = 1;
    u32 opt = 0;
};
s32 CreateSema(const SemaParam* sp);   // -> id
s32 DeleteSema(s32 id);
s32 WaitSema(s32 id);                  // blokir sampai >0, lalu decrement
s32 SignalSema(s32 id);                // increment (+ broadcast)
s32 PollSema(s32 id);                  // non-blokir: <0 kalau kosong
s32 ReferSemaStatus(s32 id, s32* out_count);

// ---- event flag (syscall 80-90) ----
constexpr u32 EF_WAIT_CLEAR = 0x01;    // WAITMODE: clear bit setelah cocok
constexpr u32 EF_WAIT_OR    = 0x00;    // default AND kalau tak ada OR
s32 CreateEventFlag(const char* name, u32 bits);
s32 DeleteEventFlag(s32 id);
s32 SetEventFlag(s32 id, u32 bits);    // OR-kan bit + broadcast
s32 ClearEventFlag(s32 id, u32 bits);  // AND-kan ~bits
s32 WaitEventFlag(s32 id, u32 bits, u32 mode, u32* out); // AND mode kecuali EF_WAIT_OR
s32 PollEventFlag(s32 id, u32 bits, u32 mode, u32* out);

// ---- file I/O (sceOpen/Read/Write/Close/Lseek) ----
// Path cdrom0:\DIR\FILE;1 -> ROOT_<cdrom0>/DIR/FILE  (set_cdrom_root dulu)
constexpr s32 O_RD = 0, O_WR = 1, O_RDWR = 2, O_CREAT = 0x200, O_TRUNC = 0x400;
void set_cdrom_root(const char* posix_dir);
void set_host_root(const char* posix_dir);        // host: -> dir ini
s32 sceOpen(const char* path, s32 mode);
s32 sceRead(s32 fd, void* buf, u32 n);
s32 sceWrite(s32 fd, const void* buf, u32 n);
s32 sceLseek(s32 fd, s32 off, s32 whence);
s32 sceClose(s32 fd);

// ---- misc ----
void scePrintf(const char* fmt, ...); // -> stdout (trace level bisa dipasang nanti)
u32  FlushCache(u32 addr);            // no-op di native (pipeline coherency tak relevan)

// ---- DMA ch2 (GIF) hook — register 0x1000A000 ----
// Dipanggil oleh build runtime nanti saat menulis MMIO; default: forward ke callback
using GifKickFn = void (*)(const void* packet_qwords, u32 qwc);
void set_gif_kick(GifKickFn fn);
void dma_ch2_kick(const void* packet, u32 qwc);   // dipanggil shim MMIO

} // namespace eekern
