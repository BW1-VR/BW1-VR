// WHERE THE TIME OF THE SCENE WALK GOES (profile_sample, a test key, and
// docs/backend.md, "Where the time of the scene walk goes").
//
// The scene walk is the largest part of a frame. It is code of the game, of
// dxwrapper, of the Direct3D 9 runtime, of the driver and of the mod. A timer
// on each call cannot split it: the calls of dxwrapper into Direct3D 9 have
// hooks of the mod already (draw_merge.cpp, one_walk.cpp), and a timer on
// each call adds time to each call. So this file takes samples.
//
// A thread of its own stops the frame thread for a moment each
// profile_sample microseconds while the frame thread is in a scene walk
// (SceneSampleWalk), reads the address of its next instruction and a copy of
// the top 384 bytes of its stack, and lets it go on. While the frame thread is
// stopped, the thread calls nothing that can take a lock that the stopped
// thread holds. Each 450 frames the report gives the share of each module and
// the buckets of 64 bytes with the most samples (scene_sample_math.h), and the
// time that the samples held the frame thread.
//
// An address in ntdll.dll or win32u.dll is mostly a call of the kernel: the
// driver in the kernel, or a wait. For these samples the report also gives
// the return addresses on the stack that are not in a module of the system:
// the code that waits.
//
//   profile_sample=0      off (the default)
//   profile_sample=N      a sample each N microseconds, 100 to 5000

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scene_sample.h"
#include "scene_sample_math.h"
#include "d3d7_thread.h"
#include "render_thread.h"

extern void ModLog(const char* format, ...);

namespace {

const int kRing = 1 << 17;          // the samples kept between two reports
const int kMaxModules = 512;
const int kTopModules = 8;
const int kTopBuckets = 16;
const int kTopCallers = 12;
// A copy of the top of the stack of each sample, for the callers of a wait in
// ntdll.dll. The ring of the stacks is smaller than the ring of the
// addresses: a report has about 20 samples in a frame.
const int kStackWords = 96;
const int kStackRing = 1 << 15;

int g_interval_us = 0;             // profile_sample
bool g_read = false;
bool g_failed = false;
HANDLE g_target = nullptr;         // the frame thread, or the render thread
// profile_sample_thread: 0 the frame thread, 1 the render thread, 2 the
// D3D7 thread.
int g_other = 0;
HANDLE g_thread = nullptr;         // the thread of the samples
volatile LONG g_in_walk = 0;
// profile_sample_outside=1: the samples come while the frame thread is NOT
// in a scene walk (the game turn, the streaming of the land, the loads, the
// waits for the headset), in place of the scene walks.
LONG g_outside = 0;

// Written by the thread of the samples, read by the report on the frame
// thread. The report reads only up to the count it read first.
uint32_t* g_ring = nullptr;
uint32_t* g_stacks = nullptr;      // kStackWords for each sample, or zeros
volatile LONG g_written = 0;       // samples written in all
LONG g_reported = 0;               // samples that a report took
volatile LONG64 g_hold_ticks = 0;  // the time that the frame thread was held
volatile LONG g_lost = 0;          // stops that gave no address

uint32_t* g_scratch = nullptr;
scene_sample::Module g_modules[kMaxModules];
char g_names[kMaxModules][40];
int g_module_count = 0;
LARGE_INTEGER g_frequency = {};

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

// It runs until the process ends. In a scene walk it spins on its core
// between two samples, so a run with profile_sample uses one more core.
// Outside the scene walks it sleeps.
DWORD WINAPI SampleThread(LPVOID) {
  const LONGLONG step = g_frequency.QuadPart * g_interval_us / 1000000;
  LARGE_INTEGER next, now;
  QueryPerformanceCounter(&next);
  for (;;) {
    if (InterlockedCompareExchange(&g_in_walk, 0, 0) == g_outside) {
      Sleep(1);
      QueryPerformanceCounter(&next);
      continue;
    }
    next.QuadPart += step;
    // A wait of more than 2 ms sleeps. A shorter one spins on this thread,
    // because Sleep has steps of 1 ms or more.
    for (;;) {
      QueryPerformanceCounter(&now);
      const LONGLONG left = next.QuadPart - now.QuadPart;
      if (left <= 0) break;
      if (left > g_frequency.QuadPart / 500) {
        Sleep(1);
      } else {
        YieldProcessor();
      }
    }
    if (next.QuadPart < now.QuadPart - 10 * step) next = now;
    if (InterlockedCompareExchange(&g_in_walk, 0, 0) == g_outside) continue;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    if (SuspendThread(g_target) == static_cast<DWORD>(-1)) continue;
    CONTEXT context;
    ZeroMemory(&context, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    const BOOL got = GetThreadContext(g_target, &context);
    const LONG at = InterlockedCompareExchange(&g_written, 0, 0);
    uint32_t* stack = g_stacks + (at & (kStackRing - 1)) * kStackWords;
    // The stack is read while the thread is stopped. ReadProcessMemory takes
    // no lock of the process, and it fails, with no fault, on a page that is
    // not there.
    SIZE_T copied = 0;
    if (!got || !ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<LPCVOID>(context.Esp),
                                   stack, kStackWords * sizeof(uint32_t),
                                   &copied)) {
      copied = 0;
    }
    ResumeThread(g_target);
    QueryPerformanceCounter(&b);
    InterlockedExchangeAdd64(&g_hold_ticks, b.QuadPart - a.QuadPart);
    if (copied < kStackWords * sizeof(uint32_t)) {
      ZeroMemory(stack, kStackWords * sizeof(uint32_t));
    }
    if (!got) {
      InterlockedIncrement(&g_lost);
      continue;
    }
    g_ring[at & (kRing - 1)] = static_cast<uint32_t>(context.Eip);
    InterlockedIncrement(&g_written);
  }
}

// The modules of the process now, sorted by base, with their names.
void ReadModules() {
  HMODULE handles[kMaxModules];
  DWORD bytes = 0;
  g_module_count = 0;
  if (!K32EnumProcessModules(GetCurrentProcess(), handles, sizeof(handles),
                             &bytes)) {
    return;
  }
  int count = static_cast<int>(bytes / sizeof(HMODULE));
  if (count > kMaxModules) count = kMaxModules;
  scene_sample::Module modules[kMaxModules];
  char names[kMaxModules][40];
  int n = 0;
  for (int i = 0; i < count; ++i) {
    MODULEINFO info;
    if (!K32GetModuleInformation(GetCurrentProcess(), handles[i], &info,
                                 sizeof(info))) {
      continue;
    }
    modules[n].base = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(info.lpBaseOfDll));
    modules[n].size = info.SizeOfImage;
    if (!K32GetModuleBaseNameA(GetCurrentProcess(), handles[i], names[n],
                               sizeof(names[n]))) {
      strcpy_s(names[n], "?");
    }
    ++n;
  }
  int order[kMaxModules];
  scene_sample::SortModules(modules, n, order);
  for (int i = 0; i < n; ++i) {
    g_modules[i] = modules[i];
    strcpy_s(g_names[i], names[order[i]]);
  }
  g_module_count = n;
}

// profile_sample_dump=1 (a test key): each report also appends its modules
// and each of its samples to bwvr-samples.txt beside runblack.exe, for a
// script that gives the share of each function. A line "M name base size"
// is a module, "W frames" starts a report, and each other line is the
// address of a sample, in hex.
bool g_dump = false;
bool g_dumped = false;  // the file of this run is open for appends

void Dump(LONG take, uint64_t frames) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return;
  slash[1] = 0;
  if (strlen(path) + 18 >= MAX_PATH) return;
  strcat_s(path, "bwvr-samples.txt");
  FILE* f = nullptr;
  if (fopen_s(&f, path, g_dumped ? "a" : "w") != 0 || f == nullptr) return;
  g_dumped = true;
  fprintf(f, "W %llu\n", frames);
  for (int m = 0; m < g_module_count; ++m) {
    fprintf(f, "M %s %08X %08X\n", g_names[m], g_modules[m].base,
            g_modules[m].size);
  }
  for (LONG i = 0; i < take; ++i) fprintf(f, "%08X\n", g_scratch[i]);
  fclose(f);
}

// The modules of the system that a wait goes through.
bool IsSystemModule(int m) {
  if (m < 0) return false;
  return _stricmp(g_names[m], "ntdll.dll") == 0 ||
         _stricmp(g_names[m], "KERNEL32.DLL") == 0 ||
         _stricmp(g_names[m], "KERNELBASE.dll") == 0 ||
         _stricmp(g_names[m], "win32u.dll") == 0;
}

// True when the word is in a module and a call is just before it.
bool IsReturnAddress(uint32_t word, int m) {
  if (m < 0 || word - g_modules[m].base < 7) return false;
  uint8_t before[7];
  SIZE_T got = 0;
  if (!ReadProcessMemory(GetCurrentProcess(),
                         reinterpret_cast<LPCVOID>(word - 7), before,
                         sizeof(before), &got) ||
      got != sizeof(before)) {
    return false;
  }
  return scene_sample::EndsWithCall(before);
}

uint32_t g_caller1[kStackRing];
uint32_t g_caller2[kStackRing];
int g_own_module = -1;  // the module of the mod

// For each sample in ntdll.dll of the newest kStackRing samples: the first
// return address on its stack outside the modules of the system, and the
// first one that is also outside d3d9.dll and the mod (0 when there is
// none). Gives the count.
int FindCallers(LONG first, LONG take) {
  g_own_module = scene_sample::ModuleOf(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FindCallers)),
      g_modules, g_module_count);
  int count = 0;
  const LONG from = take > kStackRing ? take - kStackRing : 0;
  for (LONG i = from; i < take; ++i) {
    const int m = scene_sample::ModuleOf(g_scratch[i], g_modules,
                                         g_module_count);
    if (m < 0 || _stricmp(g_names[m], "ntdll.dll") != 0) continue;
    const uint32_t* stack =
        g_stacks + ((first + i) & (kStackRing - 1)) * kStackWords;
    uint32_t c1 = 0, c2 = 0;
    for (int w = 0; w < kStackWords && c2 == 0; ++w) {
      const int wm =
          scene_sample::ModuleOf(stack[w], g_modules, g_module_count);
      if (wm < 0 || IsSystemModule(wm)) continue;
      if (!IsReturnAddress(stack[w], wm)) continue;
      if (c1 == 0) c1 = stack[w];
      if (wm != g_own_module && _stricmp(g_names[wm], "d3d9.dll") != 0) {
        c2 = stack[w];
      }
    }
    g_caller1[count] = c1;
    g_caller2[count] = c2;
    ++count;
  }
  return count;
}

// The return addresses with the most samples, as text. `addresses` is sorted
// here.
void CallerList(uint32_t* addresses, int count, LONG take, char* line,
                size_t size) {
  scene_sample::Bucket top[kTopCallers];
  const int got =
      scene_sample::TopBuckets(addresses, count, top, kTopCallers, 0);
  int used = 0;
  line[0] = 0;
  for (int k = 0; k < got; ++k) {
    const int m =
        scene_sample::ModuleOf(top[k].first, g_modules, g_module_count);
    if (top[k].first == 0) {
      used += sprintf_s(line + used, size - used, "%snone %.1f%%",
                        k == 0 ? "" : ", ", 100.0 * top[k].count / take);
    } else {
      used += sprintf_s(line + used, size - used, "%s%s+0x%X %.1f%%",
                        k == 0 ? "" : ", ", m >= 0 ? g_names[m] : "?",
                        m >= 0 ? top[k].first - g_modules[m].base
                               : top[k].first,
                        100.0 * top[k].count / take);
    }
    if (used >= static_cast<int>(size) - 80) break;
  }
}

void Start() {
  if (g_read) return;
  g_read = true;
  g_interval_us = IniKey("profile_sample", 0);
  g_dump = IniKey("profile_sample_dump", 0) != 0;
  // profile_sample_outside=N (100 to 5000) takes samples each N microseconds
  // outside the scene walks. It needs no profile_sample, so a save of the
  // launcher, which removes profile_sample, keeps it.
  const int outside = IniKey("profile_sample_outside", 0);
  g_outside = outside > 0 ? 1 : 0;
  if (g_outside != 0 && g_interval_us <= 0) g_interval_us = outside;
  if (g_interval_us <= 0) {
    g_interval_us = 0;
    return;
  }
  if (g_interval_us < 100) g_interval_us = 100;
  if (g_interval_us > 5000) g_interval_us = 5000;
  QueryPerformanceFrequency(&g_frequency);
  g_ring = new uint32_t[kRing];
  g_scratch = new uint32_t[kRing];
  g_stacks = new uint32_t[kStackRing * kStackWords];
  ZeroMemory(g_stacks, kStackRing * kStackWords * sizeof(uint32_t));
  // profile_sample_thread=1 takes the samples of the render thread, and 2
  // of the D3D7 thread (d3d7_thread.cpp), in place of the frame thread. That
  // thread starts with the first walk that has it, so SceneSampleWalk opens
  // it then.
  g_other = IniKey("profile_sample_thread", 0);
  if (g_other != 1 && g_other != 2) g_other = 0;
  if (g_other == 0) {
    g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                              THREAD_QUERY_INFORMATION,
                          FALSE, GetCurrentThreadId());
  }
  if (g_target != nullptr || g_other != 0) {
    g_thread = CreateThread(nullptr, 0, SampleThread, nullptr, 0, nullptr);
  }
  if ((g_target == nullptr && g_other == 0) || g_thread == nullptr) {
    g_failed = true;
    ModLog("scene sample : profile_sample=%d, but the thread of the samples "
           "could not start (error %lu)", g_interval_us, GetLastError());
    return;
  }
  SetThreadPriority(g_thread, THREAD_PRIORITY_TIME_CRITICAL);
  ModLog("scene sample : profile_sample=%d. A thread stops the %s thread "
         "for a moment each %d microseconds %s and reads the "
         "address of its code. Each 450 frames the log has the share of each "
         "module", g_interval_us,
         g_other == 1 ? "render" : g_other == 2 ? "D3D7" : "frame",
         g_interval_us,
         g_outside != 0 ? "outside the scene walks (profile_sample_outside=1)"
                        : "in each scene walk");
}

}  // namespace

void SceneSampleWalk(bool in) {
  if (!g_read) {
    if (!in) return;
    Start();
  }
  if (g_interval_us == 0 || g_failed) return;
  if (g_target == nullptr) {
    const DWORD id = g_other == 1   ? RenderThreadWorkerId()
                     : g_other == 2 ? D3D7ThreadWorkerId()
                                    : 0;
    if (id == 0) return;
    g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                              THREAD_QUERY_INFORMATION,
                          FALSE, id);
    if (g_target == nullptr) return;
  }
  InterlockedExchange(&g_in_walk, in ? 1 : 0);
}

void SceneSampleReport(uint64_t frames, double scene_ms) {
  if (g_interval_us == 0 || g_failed || g_ring == nullptr) return;
  const LONG written = InterlockedCompareExchange(&g_written, 0, 0);
  LONG take = written - g_reported;
  // The ring holds the newest kRing samples. The report is between two
  // frames, outside each scene walk, so no sample comes while it reads.
  const LONG first = take > kRing ? written - kRing : g_reported;
  if (take > kRing) take = kRing;
  g_reported = written;
  const LONG lost = InterlockedExchange(&g_lost, 0);
  const LONG64 held = InterlockedExchange64(&g_hold_ticks, 0);
  if (take <= 0 || frames == 0) return;
  for (LONG i = 0; i < take; ++i) {
    g_scratch[i] = g_ring[(first + i) & (kRing - 1)];
  }

  ReadModules();
  const int callers = FindCallers(first, take);
  if (g_dump) Dump(take, frames);
  // The samples of each module, and of no module.
  uint32_t per_module[kMaxModules + 1];
  ZeroMemory(per_module, sizeof(per_module));
  for (LONG i = 0; i < take; ++i) {
    const int m =
        scene_sample::ModuleOf(g_scratch[i], g_modules, g_module_count);
    ++per_module[m >= 0 ? m : kMaxModules];
  }
  const double share = 100.0 / static_cast<double>(take);
  char line[1024];
  int used = 0;
  bool shown[kMaxModules + 1];
  ZeroMemory(shown, sizeof(shown));
  for (int k = 0; k < kTopModules; ++k) {
    int best = -1;
    for (int m = 0; m <= kMaxModules; ++m) {
      if (m < kMaxModules && m >= g_module_count) continue;
      if (shown[m] || per_module[m] == 0) continue;
      if (best < 0 || per_module[m] > per_module[best]) best = m;
    }
    if (best < 0) break;
    shown[best] = true;
    used += sprintf_s(line + used, sizeof(line) - used,
                      "%s%s %.1f%% (%.2f ms)", k == 0 ? "" : ", ",
                      best == kMaxModules ? "no module" : g_names[best],
                      per_module[best] * share,
                      scene_ms * per_module[best] / take);
    if (used >= static_cast<int>(sizeof(line)) - 80) break;
  }
  const double held_ms =
      1000.0 * static_cast<double>(held) /
      static_cast<double>(g_frequency.QuadPart) / static_cast<double>(frames);
  ModLog("scene sample : %ld samples %s of %llu frames, %.0f "
         "in a frame. The samples held the frame thread %.3f ms in a frame, "
         "and %ld stops gave no address. The scene walks were %.2f ms in a "
         "frame. By module: %s",
         take, g_outside != 0 ? "outside the scene walks" : "in the scene walks",
         frames, static_cast<double>(take) / frames, held_ms, lost,
         scene_ms, line);

  // The buckets with the most samples.
  scene_sample::Bucket top[kTopBuckets];
  const int got = scene_sample::TopBuckets(g_scratch, take, top, kTopBuckets);
  used = 0;
  for (int k = 0; k < got; ++k) {
    const int m =
        scene_sample::ModuleOf(top[k].first, g_modules, g_module_count);
    used += sprintf_s(line + used, sizeof(line) - used, "%s%s+0x%X %.1f%%",
                      k == 0 ? "" : ", ", m >= 0 ? g_names[m] : "?",
                      m >= 0 ? top[k].first - g_modules[m].base : top[k].first,
                      top[k].count * share);
    if (used >= static_cast<int>(sizeof(line)) - 80) break;
  }
  ModLog("scene sample : the code with the most samples, in pieces of 64 "
         "bytes: %s", line);

  if (callers > 0) {
    char first_line[1024];
    char second_line[1024];
    CallerList(g_caller1, callers, take, first_line, sizeof(first_line));
    CallerList(g_caller2, callers, take, second_line, sizeof(second_line));
    ModLog("scene sample : %d samples in ntdll.dll. The first return address "
           "on the stack outside ntdll.dll, KERNEL32.DLL, KERNELBASE.dll and "
           "win32u.dll, in %% of all samples: %s",
           callers, first_line);
    ModLog("scene sample : the first return address that is also outside "
           "d3d9.dll and the mod: %s", second_line);
  }
}
