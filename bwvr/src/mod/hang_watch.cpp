// The watch of the frame thread (hang_watch=1, a test key, and
// docs/backend.md, "The watch of the frame thread").
//
// A desktop run that hangs ends with no word of where it hangs: the run
// script stops the game after its time. With hang_watch=1 a thread of its
// own looks at the count of the frames each second. When the count stays
// the same for kStillSeconds, the thread stops the frame thread for a
// moment, reads the address of its next instruction and the top 4 KiB of
// its stack, and lets it go on. Then it writes the
// address and the return addresses on the stack to the log, by module
// (scene_sample_math.h). It does that one time for each hang. The count goes
// up in the frames of the scene only, so a long load or a menu also gives a
// report.
//
// The names of the modules are read at the first frame, so the report needs
// no lock of the loader, which the hung thread can hold. The log can need a
// lock that the hung thread holds. Then the report does not come, and the
// run is as it is with no watch.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scene_sample_math.h"

extern void ModLog(const char* format, ...);

namespace {

const int kStillSeconds = 8;
const int kStackWords = 1024;
const int kMaxModules = 512;
const int kMaxReturns = 24;

bool g_read = false;
bool g_on = false;
HANDLE g_target = nullptr;
volatile LONG g_frames = 0;
scene_sample::Module g_modules[kMaxModules];
char g_names[kMaxModules][40];
int g_module_count = 0;

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

void ReadModules() {
  HMODULE handles[kMaxModules];
  DWORD bytes = 0;
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

// Module and offset of an address, as text.
void Where(uint32_t address, char* out, size_t size) {
  const int m =
      scene_sample::ModuleOf(address, g_modules, g_module_count);
  if (m < 0) {
    sprintf_s(out, size, "0x%08X", address);
  } else {
    sprintf_s(out, size, "%s+0x%X", g_names[m], address - g_modules[m].base);
  }
}

void Report(uint32_t eip, const uint32_t* stack, int words) {
  char at[64];
  Where(eip, at, sizeof(at));
  char line[2048];
  int used = 0;
  int found = 0;
  for (int w = 0; w < words && found < kMaxReturns; ++w) {
    const uint32_t word = stack[w];
    const int m = scene_sample::ModuleOf(word, g_modules, g_module_count);
    if (m < 0 || word - g_modules[m].base < 7) continue;
    uint8_t before[7];
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(),
                           reinterpret_cast<LPCVOID>(word - 7), before,
                           sizeof(before), &got) ||
        got != sizeof(before) || !scene_sample::EndsWithCall(before)) {
      continue;
    }
    char where[64];
    Where(word, where, sizeof(where));
    used += sprintf_s(line + used, sizeof(line) - used, "%s%s",
                      found == 0 ? "" : ", ", where);
    ++found;
    if (used >= static_cast<int>(sizeof(line)) - 80) break;
  }
  if (found == 0) strcpy_s(line, "none");
  ModLog("hang watch   : the frame thread made no frame for %d s. It is at "
         "%s. The return addresses on its stack, the nearest first: %s",
         kStillSeconds, at, line);
}

DWORD WINAPI WatchThread(LPVOID) {
  LONG last = InterlockedCompareExchange(&g_frames, 0, 0);
  int still = 0;
  bool reported = false;
  static uint32_t stack[kStackWords];
  for (;;) {
    Sleep(1000);
    const LONG now = InterlockedCompareExchange(&g_frames, 0, 0);
    if (now != last) {
      last = now;
      still = 0;
      reported = false;
      continue;
    }
    if (reported || ++still < kStillSeconds) continue;
    reported = true;
    if (SuspendThread(g_target) == static_cast<DWORD>(-1)) continue;
    CONTEXT context;
    ZeroMemory(&context, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    const BOOL got = GetThreadContext(g_target, &context);
    SIZE_T copied = 0;
    if (got) {
      // A stack can end before 4 KiB: then the read is shorter.
      for (int words = kStackWords; words >= 64; words /= 2) {
        if (ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(context.Esp), stack,
                              words * sizeof(uint32_t), &copied)) {
          break;
        }
        copied = 0;
      }
    }
    ResumeThread(g_target);
    if (!got) continue;
    Report(static_cast<uint32_t>(context.Eip), stack,
           static_cast<int>(copied / sizeof(uint32_t)));
  }
}

}  // namespace

// stereo.cpp, on the frame thread, at the start of each frame of the scene.
void HangWatchFrame() {
  if (!g_read) {
    g_read = true;
    g_on = IniKey("hang_watch", 0) != 0;
    if (!g_on) return;
    ReadModules();
    g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                              THREAD_QUERY_INFORMATION,
                          FALSE, GetCurrentThreadId());
    HANDLE thread = g_target != nullptr
                        ? CreateThread(nullptr, 0, WatchThread, nullptr, 0,
                                       nullptr)
                        : nullptr;
    if (thread == nullptr) {
      g_on = false;
      ModLog("hang watch   : hang_watch=1, but the thread of the watch could "
             "not start (error %lu)", GetLastError());
      return;
    }
    CloseHandle(thread);
    ModLog("hang watch   : hang_watch=1. When the frame thread makes no frame "
           "for %d s, the log gets the place where it is", kStillSeconds);
  }
  if (g_on) InterlockedIncrement(&g_frames);
}
