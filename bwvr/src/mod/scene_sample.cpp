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
// (SceneSampleWalk), reads the address of its next instruction, and lets it
// go on. The thread keeps only that address: it calls nothing that can take a
// lock that the stopped thread holds. Each 450 frames the report gives the
// share of each module and the buckets of 64 bytes with the most samples
// (scene_sample_math.h), and the time that the samples held the frame thread.
//
// An address in ntdll.dll or win32u.dll is mostly a call of the kernel: the
// driver in the kernel, or a wait.
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

extern void ModLog(const char* format, ...);

namespace {

const int kRing = 1 << 17;          // the samples kept between two reports
const int kMaxModules = 512;
const int kTopModules = 8;
const int kTopBuckets = 16;

int g_interval_us = 0;             // profile_sample
bool g_read = false;
bool g_failed = false;
HANDLE g_target = nullptr;         // the frame thread
HANDLE g_thread = nullptr;         // the thread of the samples
volatile LONG g_in_walk = 0;

// Written by the thread of the samples, read by the report on the frame
// thread. The report reads only up to the count it read first.
uint32_t* g_ring = nullptr;
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
    if (InterlockedCompareExchange(&g_in_walk, 0, 0) == 0) {
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
    if (InterlockedCompareExchange(&g_in_walk, 0, 0) == 0) continue;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    if (SuspendThread(g_target) == static_cast<DWORD>(-1)) continue;
    CONTEXT context;
    ZeroMemory(&context, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    const BOOL got = GetThreadContext(g_target, &context);
    ResumeThread(g_target);
    QueryPerformanceCounter(&b);
    InterlockedExchangeAdd64(&g_hold_ticks, b.QuadPart - a.QuadPart);
    if (!got) {
      InterlockedIncrement(&g_lost);
      continue;
    }
    const LONG at = InterlockedCompareExchange(&g_written, 0, 0);
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

void Start() {
  if (g_read) return;
  g_read = true;
  g_interval_us = IniKey("profile_sample", 0);
  if (g_interval_us <= 0) {
    g_interval_us = 0;
    return;
  }
  if (g_interval_us < 100) g_interval_us = 100;
  if (g_interval_us > 5000) g_interval_us = 5000;
  QueryPerformanceFrequency(&g_frequency);
  g_ring = new uint32_t[kRing];
  g_scratch = new uint32_t[kRing];
  g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                            THREAD_QUERY_INFORMATION,
                        FALSE, GetCurrentThreadId());
  if (g_target != nullptr) {
    g_thread = CreateThread(nullptr, 0, SampleThread, nullptr, 0, nullptr);
  }
  if (g_target == nullptr || g_thread == nullptr) {
    g_failed = true;
    ModLog("scene sample : profile_sample=%d, but the thread of the samples "
           "could not start (error %lu)", g_interval_us, GetLastError());
    return;
  }
  SetThreadPriority(g_thread, THREAD_PRIORITY_TIME_CRITICAL);
  ModLog("scene sample : profile_sample=%d. A thread stops the frame thread "
         "for a moment each %d microseconds in each scene walk and reads the "
         "address of its code. Each 450 frames the log has the share of each "
         "module", g_interval_us, g_interval_us);
}

}  // namespace

void SceneSampleWalk(bool in) {
  if (!g_read) {
    if (!in) return;
    Start();
  }
  if (g_interval_us == 0 || g_failed) return;
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
  ModLog("scene sample : %ld samples in the scene walks of %llu frames, %.0f "
         "in a frame. The samples held the frame thread %.3f ms in a frame, "
         "and %ld stops gave no address. The scene walks were %.2f ms in a "
         "frame. By module: %s",
         take, frames, static_cast<double>(take) / frames, held_ms, lost,
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
}
