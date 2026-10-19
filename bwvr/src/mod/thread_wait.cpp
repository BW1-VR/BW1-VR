// How the threads of the mod wait for work (thread_wait=1,
// docs/backend.md, "How the threads wait").
//
// WHY. The render thread (render_thread.cpp) and the D3D7 thread
// (d3d7_thread.cpp) read a queue that the frame thread writes. A reader that
// spins on an empty queue sees a new record at once, but it holds a core the
// whole time. On a PC with few cores that core is missing for the game and
// for the driver. Two logical cores of one physical core share its units, so
// a thread that spins also slows the thread on the other logical core. A
// laptop also gives less clock to all cores when one more core is busy.
//
// THE RULE. A reader spins for a short time and then sleeps on an event. The
// time comes from the count of the cores (Policy). In a walk a reader spins
// longer, because a record comes soon. A writer wakes a reader that sleeps
// when some records wait for it, at a stop and at the end of a walk. A wake
// is a call of the kernel, so on a PC with few cores one wake takes some
// records.
//
// THE WAIT FOR THE GPU. The second thread of frame_send (gpu_bridge.cpp,
// GpuBridgeWaitOnThread) waits for the event query of the frame. It sleeps
// with a timer of high resolution until a short time before the GPU is done
// in the last frames, and only then it asks the query in short steps.
//
// THE LOAD. ThreadLoadReport writes the CPU time of each thread of the mod
// and of the frame thread for a frame. The time includes the spins, so it
// is the time that a core was not free for other work.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "thread_wait.h"

void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

int g_mode = 1;
bool g_said_cores = false;
int g_logical = 0;
int g_physical = 0;
int g_classes = 1;  // efficiency classes of the cores (2 on a hybrid CPU)
LARGE_INTEGER g_frequency = {};

// The spin times in microseconds, and the records for a wake.
struct Policy {
  int walk_us;
  int idle_us;
  uint32_t wake_records;
};
Policy g_policy = {100, 20, 1};

// Counts the cores that the process can use: a core of the process affinity
// mask (test_cores, or a tool of the player) counts, others do not.
void ReadCores() {
  if (g_logical != 0) return;
  DWORD_PTR process_mask = 0, system_mask = 0;
  if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask,
                              &system_mask)) {
    process_mask = ~static_cast<DWORD_PTR>(0);
  }
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  uint8_t* buffer =
      bytes > 0 ? static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, bytes))
                : nullptr;
  if (buffer != nullptr &&
      GetLogicalProcessorInformationEx(
          RelationProcessorCore,
          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer),
          &bytes)) {
    BYTE low = 255, high = 0;
    for (DWORD at = 0; at < bytes;) {
      const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info =
          reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
              buffer + at);
      if (info->Relationship == RelationProcessorCore) {
        int logical = 0;
        for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
          // A 32 bit process runs in group 0.
          KAFFINITY mask = info->Processor.GroupMask[g].Mask;
          if (info->Processor.GroupMask[g].Group == 0) {
            mask &= static_cast<KAFFINITY>(process_mask);
          } else {
            mask = 0;
          }
          while (mask != 0) {
            logical += static_cast<int>(mask & 1);
            mask >>= 1;
          }
        }
        if (logical > 0) {
          ++g_physical;
          g_logical += logical;
          const BYTE cls = info->Processor.EfficiencyClass;
          if (cls < low) low = cls;
          if (cls > high) high = cls;
        }
      }
      if (info->Size == 0) break;
      at += info->Size;
    }
    g_classes = high > low ? 2 : 1;
  }
  if (buffer != nullptr) HeapFree(GetProcessHeap(), 0, buffer);
  if (g_logical == 0) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_logical = static_cast<int>(si.dwNumberOfProcessors);
  }
  if (g_physical == 0) g_physical = g_logical;
}

// The frame thread, the D3D7 thread, the render thread and the second
// thread of frame_send can each use a core. The driver and the runtime of
// OpenXR have threads too.
Policy MakePolicy() {
  if (g_logical >= 8) return {100, 20, 1};
  if (g_logical >= 4) return {50, 10, 1};
  return {0, 0, 4};
}

void SayPolicy() {
  if (g_mode == 0) {
    ModLog("thread wait  : thread_wait=0. The render thread and the D3D7 "
           "thread spin for the whole walk, and the second thread of "
           "frame_send asks the GPU each 50 microseconds");
    return;
  }
  ModLog("thread wait  : thread_wait=1. %d logical cores on %d physical "
         "cores%s. A reader spins %d microseconds in a walk and %d "
         "microseconds outside it, and then it sleeps. A writer wakes it "
         "after %u records. The second thread of frame_send sleeps until "
         "the GPU is nearly done",
         g_logical, g_physical,
         g_classes > 1 ? " with two classes of efficiency" : "",
         g_policy.walk_us, g_policy.idle_us, g_policy.wake_records);
}

// The high resolution timer of Windows 10 1803 and later.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
__declspec(thread) HANDLE t_timer = nullptr;
__declspec(thread) bool t_timer_tried = false;

// ---------------------------------------------------------------------------
// The load.

struct Loaded {
  char name[32];
  HANDLE thread;
  ULONG64 cycles;  // at the last report
};
const int kMaxLoaded = 8;
Loaded g_loaded[kMaxLoaded];
volatile LONG g_loaded_count = 0;
SRWLOCK g_loaded_lock = SRWLOCK_INIT;
uint64_t g_tsc_last = 0;
LARGE_INTEGER g_qpc_last = {};
ULONGLONG g_process_last = 0;  // user and kernel time, 100 ns

ULONGLONG ProcessTime() {
  FILETIME created, ended, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
    return 0;
  }
  ULARGE_INTEGER k, u;
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;
  return k.QuadPart + u.QuadPart;
}

}  // namespace

void ThreadWaitLimitCores(int cores, int smt) {
  if (cores <= 0) return;
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  uint8_t* buffer =
      bytes > 0 ? static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, bytes))
                : nullptr;
  DWORD_PTR mask = 0;
  int taken = 0;
  if (buffer != nullptr &&
      GetLogicalProcessorInformationEx(
          RelationProcessorCore,
          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer),
          &bytes)) {
    for (DWORD at = 0; at < bytes && taken < cores;) {
      const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info =
          reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
              buffer + at);
      if (info->Relationship == RelationProcessorCore &&
          info->Processor.GroupCount > 0 &&
          info->Processor.GroupMask[0].Group == 0) {
        // The first logical core of this physical core, or each of them.
        KAFFINITY core = info->Processor.GroupMask[0].Mask;
        if (smt == 0) core &= ~(core - 1);
        mask |= static_cast<DWORD_PTR>(core);
        ++taken;
      }
      if (info->Size == 0) break;
      at += info->Size;
    }
  }
  if (buffer != nullptr) HeapFree(GetProcessHeap(), 0, buffer);
  if (mask != 0 && SetProcessAffinityMask(GetCurrentProcess(), mask)) {
    ModLog("thread wait  : test_cores=%d, test_smt=%d. The process runs on %d "
           "physical cores, %s (mask 0x%IX)", cores, smt, taken,
           smt != 0 ? "with each of their logical cores"
                    : "with one logical core of each",
           mask);
  } else {
    ModLog("thread wait  : test_cores=%d. The process affinity mask could "
           "not change", cores);
  }
}

void ThreadWaitSetOptions(int mode, int walk_us, int idle_us, int wake) {
  g_mode = mode != 0 ? 1 : 0;
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  ReadCores();
  g_policy = MakePolicy();
  if (walk_us >= 0) g_policy.walk_us = walk_us;
  if (idle_us >= 0) g_policy.idle_us = idle_us;
  if (wake > 0) g_policy.wake_records = static_cast<uint32_t>(wake);
  SayPolicy();
}

int ThreadWaitMode() { return g_mode; }

int ThreadWaitLogicalCores() {
  ReadCores();
  return g_logical;
}

void ThreadWaitSet(int mode) { g_mode = mode != 0 ? 1 : 0; }

void ThreadWaitToggle(const char* keys) {
  g_mode = g_mode != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_mode != 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, thread_wait is %d now", keys, g_mode);
}

int64_t ThreadWaitSpinTicks(bool in_walk) {
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  const int us = in_walk ? g_policy.walk_us : g_policy.idle_us;
  return g_frequency.QuadPart * us / 1000000;
}

uint32_t ThreadWaitWakeRecords() {
  return g_mode != 0 ? g_policy.wake_records : 1;
}

int64_t ThreadWaitFrequency() {
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  return g_frequency.QuadPart;
}

bool ThreadWaitSleep(int64_t ticks) {
  if (!t_timer_tried) {
    t_timer_tried = true;
    t_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
  }
  if (t_timer == nullptr) return false;
  if (ticks <= 0) return true;
  LARGE_INTEGER due;
  // A negative time is relative, in units of 100 ns.
  due.QuadPart = -static_cast<LONGLONG>(ticks * 10000000 / ThreadWaitFrequency());
  if (due.QuadPart == 0) return true;
  if (!SetWaitableTimer(t_timer, &due, 0, nullptr, nullptr, FALSE)) return false;
  WaitForSingleObject(t_timer, INFINITE);
  return true;
}

void ThreadWaitTune(void* thread, const wchar_t* name) {
  HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  if (kernel == nullptr || thread == nullptr) return;
  typedef HRESULT(WINAPI * Describe)(HANDLE, PCWSTR);
  Describe describe =
      reinterpret_cast<Describe>(GetProcAddress(kernel, "SetThreadDescription"));
  if (describe != nullptr) describe(static_cast<HANDLE>(thread), name);
  // ThreadPowerThrottling (3) with the execution speed in the control mask
  // and not in the state mask: the thread is never background work.
  struct PowerThrottling {
    ULONG version;
    ULONG control_mask;
    ULONG state_mask;
  };
  typedef BOOL(WINAPI * SetInformation)(HANDLE, int, LPVOID, DWORD);
  SetInformation set = reinterpret_cast<SetInformation>(
      GetProcAddress(kernel, "SetThreadInformation"));
  if (set != nullptr) {
    PowerThrottling state = {1, 1, 0};
    set(static_cast<HANDLE>(thread), 3, &state, sizeof(state));
  }
}

void ThreadLoadAdd(const char* name, void* thread) {
  if (thread == nullptr) return;
  AcquireSRWLockExclusive(&g_loaded_lock);
  const LONG count = g_loaded_count;
  bool known = false;
  for (LONG i = 0; i < count; ++i) {
    if (strcmp(g_loaded[i].name, name) == 0) known = true;
  }
  HANDLE copy = nullptr;
  if (!known && count < kMaxLoaded &&
      DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(thread),
                      GetCurrentProcess(), &copy,
                      THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
    Loaded& l = g_loaded[count];
    strncpy_s(l.name, name, _TRUNCATE);
    l.thread = copy;
    l.cycles = 0;
    QueryThreadCycleTime(copy, &l.cycles);
    InterlockedExchange(&g_loaded_count, count + 1);
  }
  ReleaseSRWLockExclusive(&g_loaded_lock);
}

void ThreadLoadAddThis(const char* name) {
  ThreadLoadAdd(name, GetCurrentThread());
}

void ThreadLoadReport(uint64_t frames) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const uint64_t tsc = __rdtsc();
  const ULONGLONG process = ProcessTime();
  if (g_tsc_last == 0 || frames == 0) {
    g_tsc_last = tsc;
    g_qpc_last = now;
    g_process_last = process;
    AcquireSRWLockExclusive(&g_loaded_lock);
    for (LONG i = 0; i < g_loaded_count; ++i) {
      QueryThreadCycleTime(g_loaded[i].thread, &g_loaded[i].cycles);
    }
    ReleaseSRWLockExclusive(&g_loaded_lock);
    return;
  }
  const double seconds =
      static_cast<double>(now.QuadPart - g_qpc_last.QuadPart) /
      static_cast<double>(ThreadWaitFrequency());
  // The cycle times of Windows count with the time stamp counter.
  const double tsc_per_ms =
      seconds > 0.0 ? static_cast<double>(tsc - g_tsc_last) / seconds / 1000.0
                    : 0.0;
  const double per = static_cast<double>(frames);
  char line[512];
  int used = 0;
  AcquireSRWLockExclusive(&g_loaded_lock);
  for (LONG i = 0; i < g_loaded_count && tsc_per_ms > 0.0; ++i) {
    ULONG64 cycles = 0;
    if (!QueryThreadCycleTime(g_loaded[i].thread, &cycles)) continue;
    const double ms =
        static_cast<double>(cycles - g_loaded[i].cycles) / tsc_per_ms / per;
    g_loaded[i].cycles = cycles;
    used += sprintf_s(line + used, sizeof(line) - used, "%s%s %.2f",
                      used == 0 ? "" : ", ", g_loaded[i].name, ms);
  }
  ReleaseSRWLockExclusive(&g_loaded_lock);
  const double cores =
      seconds > 0.0 ? static_cast<double>(process - g_process_last) / 1e7 /
                          seconds
                    : 0.0;
  ModLog("thread load  : thread_wait=%d. The CPU time of a frame in ms "
         "(spins included): %s. The process used %.2f cores of %d",
         g_mode, used > 0 ? line : "none", cores, g_logical);
  g_tsc_last = tsc;
  g_qpc_last = now;
  g_process_last = process;
}
