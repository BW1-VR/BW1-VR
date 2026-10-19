// THE ADDRESS SPACE OF THE PROCESS (memory_log, docs/backend.md, "The
// address space").
//
// runblack.exe is a 32 bit program with no flag for a large address space,
// so the process has 2 GiB of addresses. The game, the mod, dxwrapper,
// Direct3D 9 and 11, the Vulkan driver and the OpenXR runtime share them.
// When no free run of addresses is large enough, an allocation fails: the
// game then writes to the null pointer of malloc in LH3DMem::Alloc
// (0x00842D4A), and an allocation of the mod gives std::bad_alloc.
//
// With memory_log=N a thread of its own walks the address space with
// VirtualQuery each N seconds and writes one line: the use by kind (image,
// mapped, private) and state (committed, reserved), the free blocks of 64
// KiB and the largest free run, the private bytes, the heap of the game
// (LH3DMem) and the largest private allocations. The walk takes some
// milliseconds, so it is not on the frame thread. It only reads.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <string.h>

#include "address_space_math.h"

extern void ModLog(const char* format, ...);

namespace {

// LH3DMem of the game: its count of bytes and of blocks now.
const uintptr_t kGameHeapBytes = 0x00EF654C;  // LH3DMem::g_current_amount
const uintptr_t kGameHeapCount = 0x00EF6548;  // LH3DMem::g_current_count
const double kMiB = 1024.0 * 1024.0;
const uint64_t kLowFree = 64ull * 1024 * 1024;

int g_seconds = 0;

int IniKey(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return fallback;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return fallback;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return fallback;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void Walk(address_space::Summary* s, double* ms) {
  LARGE_INTEGER hz, start, end;
  QueryPerformanceFrequency(&hz);
  QueryPerformanceCounter(&start);
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  const uintptr_t top =
      reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
  address_space::Start(s);
  uintptr_t at = 0;
  while (at < top) {
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(at), &m, sizeof(m)) == 0) break;
    const address_space::Region r = {
        reinterpret_cast<uintptr_t>(m.BaseAddress), m.RegionSize,
        reinterpret_cast<uintptr_t>(m.AllocationBase), m.State, m.Type};
    address_space::Add(s, r);
    const uintptr_t next = reinterpret_cast<uintptr_t>(m.BaseAddress) +
                           m.RegionSize;
    if (next <= at) break;
    at = next;
  }
  address_space::Finish(s);
  QueryPerformanceCounter(&end);
  *ms = hz.QuadPart > 0
            ? (end.QuadPart - start.QuadPart) * 1000.0 / hz.QuadPart
            : 0.0;
}

DWORD WINAPI WatchThread(LPVOID) {
  using namespace address_space;
  double first_use = -1.0;
  double last_use = 0.0;
  double most_use = 0.0;
  DWORD last_tick = 0;
  bool low_said = false;
  for (;;) {
    Summary s;
    double ms = 0.0;
    Walk(&s, &ms);
    PROCESS_MEMORY_COUNTERS_EX counters;
    ZeroMemory(&counters, sizeof(counters));
    K32GetProcessMemoryInfo(
        GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters));
    double heap = -1.0;
    long blocks = 0;
    if (!IsBadReadPtr(reinterpret_cast<const void*>(kGameHeapCount), 8)) {
      heap = *reinterpret_cast<const uint32_t*>(kGameHeapBytes) / kMiB;
      blocks = *reinterpret_cast<const long*>(kGameHeapCount);
    }
    const double use = InUse(s) / kMiB;
    const DWORD now = GetTickCount();
    if (first_use < 0.0) first_use = use;
    if (use > most_use) most_use = use;
    const double minutes = last_tick != 0 ? (now - last_tick) / 60000.0 : 0.0;
    const double per_minute =
        minutes > 0.0 ? (use - last_use) / minutes : 0.0;
    ModLog("memory       : the address space has %.0f MB in use and %.0f MB "
           "in free blocks, the largest free run is %.1f MB (%u free runs). "
           "Committed: image %.0f, mapped %.0f, private %.0f MB. Reserved: "
           "image %.0f, mapped %.0f, private %.0f MB. Private bytes %.0f MB. "
           "The heap of the game %.1f MB in %ld blocks. %u private "
           "allocations, the largest %.1f, %.1f, %.1f and %.1f MB. In use: "
           "%+.0f MB since the first line, %+.1f MB a minute since the line "
           "before, %.0f MB at most. The walk took %.2f ms",
           use, s.free_usable / kMiB, s.free_largest / kMiB, s.free_runs,
           s.committed[kImage] / kMiB, s.committed[kMapped] / kMiB,
           s.committed[kPrivate] / kMiB, s.reserved[kImage] / kMiB,
           s.reserved[kMapped] / kMiB, s.reserved[kPrivate] / kMiB,
           counters.PrivateUsage / kMiB, heap, blocks, s.private_allocations,
           s.top[0] / kMiB, s.top[1] / kMiB, s.top[2] / kMiB, s.top[3] / kMiB,
           use - first_use, per_minute, most_use, ms);
    if (s.free_largest < kLowFree && !low_said) {
      low_said = true;
      ModLog("memory       : the largest free run is under %.0f MB. A large "
             "allocation of the game or of a driver can fail soon",
             kLowFree / kMiB);
    } else if (s.free_largest >= 2 * kLowFree) {
      low_said = false;
    }
    last_use = use;
    last_tick = now;
    Sleep(static_cast<DWORD>(g_seconds) * 1000);
  }
}

}  // namespace

// mod_main.cpp, in Startup, after the check of runblack.exe.
void MemoryWatchInstall() {
  g_seconds = IniKey("memory_log", 20);
  if (g_seconds <= 0) {
    ModLog("memory       : memory_log=0: no line of the address space");
    return;
  }
  HANDLE thread = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
  if (thread == nullptr) {
    ModLog("memory       : memory_log=%d, but the thread could not start "
           "(error %lu)", g_seconds, GetLastError());
    return;
  }
  SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
  CloseHandle(thread);
  ModLog("memory       : memory_log=%d. Each %d s a line gives the use of the "
         "address space of 2 GiB, the largest free run and the heap of the "
         "game", g_seconds, g_seconds);
}
