// The watch of the frame thread (hang_watch=1, a test key) and the crash log
// (crash_log=1). See docs/backend.md, "The watch of the frame thread" and
// "The crash log".
//
// A desktop run that hangs ends with no word of where it hangs: the run
// script stops the game after its time. With hang_watch=1 a thread of its
// own looks at the count of the frames each second. When the count stays
// the same for kStillSeconds, the thread stops the frame thread for a
// moment, reads the address of its next instruction and the top 4 KiB of
// its stack, and lets it go on. Then it writes the
// address and the return addresses on the stack to the log, by module
// (scene_sample_math.h). It does that one time for each hang. The count goes
// up at the end of each frame, also of the logos, so a long load also gives a
// report.
//
// THE CRASH LOG. The C runtime of the game (Visual C++ 6) ends the game with
// the box "abnormal program termination" in abort (0x007C9849). A C++
// exception that no code catches comes there on any thread: the unhandled
// exception filter of the game (__CxxUnhandledExceptionFilter 0x007D5160)
// calls terminate (0x007D480C), and terminate calls abort. The C++ exceptions
// of the mod and of the runtimes have the same code (0xE06D7363), so they come
// there too. A box with an error number (R6025, a pure virtual call) comes
// from _amsg_exit (0x007CA5A7). Each of these ends the game with no line in
// the log. So with crash_log=1:
//   - A vectored handler writes the first kMaxThrowLogs C++ exceptions, with
//     their type, their thread and the return addresses on the stack of the
//     throw. It only reads, and it gives each exception to the next handler.
//   - Hooks of abort, terminate and _amsg_exit write their caller and the
//     return addresses on the stack (16 KiB, so the frames of the throw are
//     in it), and then call the function of the game.
// A fault that no handler takes is in the report of a crash
// (crash_report.cpp).
//
// The names of the modules are read at the first frame and again each
// kModuleReadFrames frames, on the frame thread, into the table that a report
// does not read. So a report needs no lock of the loader, which the hung
// thread can hold. The log can need a lock that the hung thread holds. Then
// the report does not come, and the run is as it is with no watch.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stdexcept>

#include "MinHook.h"
#include "scene_sample_math.h"

extern void ModLog(const char* format, ...);

namespace {

const int kStillSeconds = 8;
const int kStackWords = 1024;
const int kCrashStackWords = 4096;
const int kMaxModules = 512;
const int kMaxReturns = 24;
const int kMaxCrashReturns = 48;
const int kMaxThrowLogs = 8;
const LONG kModuleReadFrames = 1800;

// The C runtime of the game.
const uintptr_t kAbort = 0x007C9849;      // abort
const uintptr_t kTerminate = 0x007D480C;  // terminate
const uintptr_t kAmsgExit = 0x007CA5A7;   // _amsg_exit
const DWORD kCxxCode = 0xE06D7363;        // a C++ exception of Visual C++
const ULONG_PTR kCxxMagic = 0x19930520;

bool g_read = false;
bool g_on = false;
// hang_watch=2 (a test key): the place of the frame thread each
// kStillSeconds, also while it makes frames.
bool g_always = false;
HANDLE g_target = nullptr;
volatile LONG g_frames = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_watch_before = nullptr;

// Two tables of the modules: a report reads the one of g_table, and a new
// read fills the other one.
struct ModuleTable {
  scene_sample::Module modules[kMaxModules];
  char names[kMaxModules][40];
  int count;
};
ModuleTable g_tables[2];
volatile LONG g_table = 0;

// The crash log.
bool g_crash_on = false;
volatile LONG g_crash_frame = 0;
DWORD g_frame_thread = 0;
int g_crash_test = 0;         // crash_log_test: the frame of the test throw
int g_crash_caught_test = 0;  // crash_log_caught_test
typedef void(__cdecl* VoidFn)();
typedef void(__cdecl* AmsgExitFn)(int);
VoidFn o_abort = nullptr;
VoidFn o_terminate = nullptr;
AmsgExitFn o_amsg_exit = nullptr;
volatile LONG g_throws = 0;
volatile LONG g_terminate_said = 0;
// The last C++ exception, for the line of abort.
char g_last_type[96] = "";
DWORD g_last_thread = 0;
DWORD g_last_tick = 0;
uint32_t g_crash_stack[kCrashStackWords];

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
  static scene_sample::Module modules[kMaxModules];
  static char names[kMaxModules][40];
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
  const LONG next = 1 - InterlockedCompareExchange(&g_table, 0, 0);
  ModuleTable& t = g_tables[next];
  for (int i = 0; i < n; ++i) {
    t.modules[i] = modules[i];
    strcpy_s(t.names[i], names[order[i]]);
  }
  t.count = n;
  InterlockedExchange(&g_table, next);
}

// Module and offset of an address, as text.
void Where(uint32_t address, char* out, size_t size) {
  const ModuleTable& t = g_tables[InterlockedCompareExchange(&g_table, 0, 0)];
  const int m = scene_sample::ModuleOf(address, t.modules, t.count);
  if (m < 0) {
    sprintf_s(out, size, "0x%08X", address);
  } else {
    sprintf_s(out, size, "%s+0x%X", t.names[m], address - t.modules[m].base);
  }
}

// The words of the stack from `esp` up. A stack can end before the full
// read: then the read is shorter.
int ReadStack(uint32_t esp, uint32_t* out, int max_words) {
  SIZE_T copied = 0;
  for (int words = max_words; words >= 64; words /= 2) {
    if (ReadProcessMemory(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(esp)),
                          out, words * sizeof(uint32_t), &copied)) {
      break;
    }
    copied = 0;
  }
  return static_cast<int>(copied / sizeof(uint32_t));
}

void Report(const char* prefix, uint32_t eip, const uint32_t* stack,
            int words, const char* what, int max_returns) {
  const ModuleTable& t = g_tables[InterlockedCompareExchange(&g_table, 0, 0)];
  char at[64];
  Where(eip, at, sizeof(at));
  static char line[2048];
  int used = 0;
  int found = 0;
  for (int w = 0; w < words && found < max_returns; ++w) {
    const uint32_t word = stack[w];
    const int m = scene_sample::ModuleOf(word, t.modules, t.count);
    if (m < 0 || word - t.modules[m].base < 7) continue;
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
  ModLog("%s: %s. It is at %s. The return addresses on its stack, the "
         "nearest first: %s",
         prefix, what, at, line);
}

// A FAULT. With hang_watch=1 or 2 an exception that no handler takes writes
// its code, its place and the return addresses on the stack of its thread
// (a stack overflow shows the calls that repeat). The frame thread keeps 64
// KiB of its stack for this (SetThreadStackGuarantee). Then the filter that
// was there before gets the exception.
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  static volatile LONG s_once = 0;
  if (InterlockedExchange(&s_once, 1) == 0) {
    const CONTEXT* c = info->ContextRecord;
    char what[96];
    sprintf_s(what, "a fault 0x%08lX on thread %lu",
              info->ExceptionRecord->ExceptionCode, GetCurrentThreadId());
    static uint32_t stack[kStackWords];
    const int words = ReadStack(c->Esp, stack, kStackWords);
    Report("hang watch   ", static_cast<uint32_t>(c->Eip), stack, words, what,
           kMaxReturns);
  }
  return g_watch_before != nullptr ? g_watch_before(info)
                                   : EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI WatchThread(LPVOID) {
  LONG last = InterlockedCompareExchange(&g_frames, 0, 0);
  int still = 0;
  bool reported = false;
  static uint32_t stack[kStackWords];
  for (;;) {
    Sleep(1000);
    const LONG now = InterlockedCompareExchange(&g_frames, 0, 0);
    if (g_always) {
      if (++still < kStillSeconds) continue;
      still = 0;
    } else {
      if (now != last) {
        last = now;
        still = 0;
        reported = false;
        continue;
      }
      if (reported || ++still < kStillSeconds) continue;
      reported = true;
    }
    if (SuspendThread(g_target) == static_cast<DWORD>(-1)) continue;
    CONTEXT context;
    ZeroMemory(&context, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    const BOOL got = GetThreadContext(g_target, &context);
    int words = 0;
    if (got) words = ReadStack(context.Esp, stack, kStackWords);
    ResumeThread(g_target);
    if (!got) continue;
    Report("hang watch   ", static_cast<uint32_t>(context.Eip), stack, words,
           g_always ? "the place of the frame thread now"
                    : "the frame thread made no frame for 8 s",
           kMaxReturns);
  }
}

// THE CRASH LOG.

// Copies a text of printable characters. False when it is not readable, is
// empty, or has another character.
bool CopyText(const char* from, char* out, size_t size) {
  out[0] = 0;
  if (from == nullptr || size < 2 || IsBadStringPtrA(from, size - 1)) {
    return false;
  }
  size_t n = 0;
  for (; n < size - 1 && from[n] != 0; ++n) {
    const unsigned char ch = static_cast<unsigned char>(from[n]);
    if (ch < 0x20 || ch > 0x7E) {
      out[0] = 0;
      return false;
    }
    out[n] = from[n];
  }
  out[n] = 0;
  return n > 0;
}

// The type of a C++ exception of Visual C++ on x86: the record has the
// object and its ThrowInfo. ThrowInfo +0xC is the CatchableTypeArray, its
// first entry at +4 is the CatchableType of the thrown class, and its
// TypeDescriptor at +4 has the decorated name at +8. A class of the standard
// library (a name that ends with "@std@@") has the pointer to its text at
// +4 of the object, in the library of the mod and in the library of the game.
void ThrownType(const EXCEPTION_RECORD* r, char* type, size_t type_size,
                char* text, size_t text_size) {
  strcpy_s(type, type_size, "unknown");
  text[0] = 0;
  if (r->NumberParameters != 3 || r->ExceptionInformation[0] != kCxxMagic) {
    return;
  }
  const uint8_t* object =
      reinterpret_cast<const uint8_t*>(r->ExceptionInformation[1]);
  const uint8_t* info =
      reinterpret_cast<const uint8_t*>(r->ExceptionInformation[2]);
  if (info == nullptr || IsBadReadPtr(info, 16)) return;
  const uint8_t* array = *reinterpret_cast<const uint8_t* const*>(info + 0xC);
  if (array == nullptr || IsBadReadPtr(array, 8) ||
      *reinterpret_cast<const int32_t*>(array) <= 0) {
    return;
  }
  const uint8_t* catchable = *reinterpret_cast<const uint8_t* const*>(array + 4);
  if (catchable == nullptr || IsBadReadPtr(catchable, 8)) return;
  const uint8_t* descriptor =
      *reinterpret_cast<const uint8_t* const*>(catchable + 4);
  if (descriptor == nullptr ||
      !CopyText(reinterpret_cast<const char*>(descriptor + 8), type,
                type_size)) {
    strcpy_s(type, type_size, "unknown");
    return;
  }
  const size_t length = strlen(type);
  if (length > 6 && strcmp(type + length - 6, "@std@@") == 0 &&
      object != nullptr && !IsBadReadPtr(object, 8)) {
    CopyText(*reinterpret_cast<const char* const*>(object + 4), text,
             text_size);
  }
}

const char* ThreadName(DWORD thread) {
  return thread == g_frame_thread ? "the frame thread" : "not the frame thread";
}

// Each C++ exception, before a handler catches it. Only reads.
LONG CALLBACK FirstChance(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* r = info->ExceptionRecord;
  if (r->ExceptionCode != kCxxCode) return EXCEPTION_CONTINUE_SEARCH;
  const LONG n = InterlockedIncrement(&g_throws);
  char type[96];
  char text[128];
  ThrownType(r, type, sizeof(type), text, sizeof(text));
  strcpy_s(g_last_type, type);
  g_last_thread = GetCurrentThreadId();
  g_last_tick = GetTickCount();
  if (n > kMaxThrowLogs) return EXCEPTION_CONTINUE_SEARCH;
  char what[512];
  sprintf_s(what,
            "C++ exception %ld, of the type %s%s%s%s, on thread %lu (%s), "
            "frame %ld. A handler can still catch it",
            n, type, text[0] != 0 ? " with the text \"" : "", text,
            text[0] != 0 ? "\"" : "", g_last_thread,
            ThreadName(g_last_thread), g_crash_frame);
  static uint32_t stack[kStackWords];
  const CONTEXT* c = info->ContextRecord;
  const int words = ReadStack(c->Esp, stack, kStackWords);
  Report("crash log    ", static_cast<uint32_t>(c->Eip), stack, words, what,
         kMaxReturns);
  if (n == kMaxThrowLogs) {
    ModLog("crash log    : that is the last C++ exception with a line. The "
           "line of abort gives the count and the last type");
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// The line of an end of the game in the C runtime, on the thread that ends it.
void ReportEnd(const char* how, uint32_t caller, uint32_t esp) {
  const DWORD thread = GetCurrentThreadId();
  const DWORD now = GetTickCount();
  char last[192] = "";
  if (g_last_thread != 0) {
    sprintf_s(last, ". The last was of the type %s, on thread %lu, %lu ms ago",
              g_last_type, g_last_thread, now - g_last_tick);
  }
  char what[512];
  sprintf_s(what, "%s, on thread %lu (%s), frame %ld. %ld C++ exceptions came "
            "in this session%s. The caller",
            how, thread, ThreadName(thread), g_crash_frame, g_throws, last);
  const int words = ReadStack(esp, g_crash_stack, kCrashStackWords);
  Report("crash log    ", caller, g_crash_stack, words, what, kMaxCrashReturns);
}

void __cdecl h_Terminate() {
  if (InterlockedExchange(&g_terminate_said, 1) == 0) {
    ReportEnd("terminate: no handler caught a C++ exception, or the handling "
              "of an exception failed. terminate calls abort",
              static_cast<uint32_t>(
                  reinterpret_cast<uintptr_t>(_ReturnAddress())),
              static_cast<uint32_t>(
                  reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())));
  }
  o_terminate();
}

void __cdecl h_Abort() {
  static volatile LONG s_said = 0;
  if (InterlockedExchange(&s_said, 1) == 0) {
    if (g_terminate_said != 0) {
      ModLog("crash log    : abort, after terminate. The game shows \"abnormal "
             "program termination\" and ends");
    } else {
      ReportEnd("abort: the game shows \"abnormal program termination\" and "
                "ends",
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(_ReturnAddress())),
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())));
    }
  }
  o_abort();
}

void __cdecl h_AmsgExit(int error) {
  static volatile LONG s_said = 0;
  if (InterlockedExchange(&s_said, 1) == 0) {
    char how[160];
    sprintf_s(how, "_amsg_exit: the game shows the runtime error R6%03d and "
              "ends (R6025 is a pure virtual call)", error);
    ReportEnd(how,
              static_cast<uint32_t>(
                  reinterpret_cast<uintptr_t>(_ReturnAddress())),
              static_cast<uint32_t>(
                  reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())));
  }
  o_amsg_exit(error);
}

bool SameBytes(uintptr_t address, const uint8_t* want, int count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, want, count) == 0;
}

bool HookAt(uintptr_t address, const uint8_t* want, int count,
            void* replacement, void** original, const char* name) {
  if (!SameBytes(address, want, count)) {
    ModLog("crash log    : %s at 0x%08X has other bytes, no hook", name,
           static_cast<unsigned>(address));
    return false;
  }
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  if (status != MH_OK) {
    ModLog("crash log    : the hook of %s at 0x%08X FAILED", name,
           static_cast<unsigned>(address));
  }
  return status == MH_OK;
}

// The test throws, on the frame thread.
void CrashTests(LONG frame) {
  if (g_crash_caught_test > 0 && frame == g_crash_caught_test) {
    try {
      throw std::runtime_error("crash_log_caught_test");
    } catch (const std::exception&) {
      ModLog("crash log    : crash_log_caught_test=%d: the mod caught its "
             "test exception, and the game goes on, frame %ld",
             g_crash_caught_test, frame);
    }
  }
  if (g_crash_test > 0 && frame == g_crash_test) {
    ModLog("crash log    : crash_log_test=%d: the frame thread now throws a "
           "C++ exception that the mod does not catch, frame %ld",
           g_crash_test, frame);
    throw std::runtime_error("crash_log_test");
  }
}

}  // namespace

// mod_main.cpp, in Startup, after MH_Initialize.
void CrashLogInstall() {
  g_crash_on = IniKey("crash_log", 1) != 0;
  if (!g_crash_on) {
    ModLog("crash log    : crash_log=0: an end of the game in its C runtime "
           "gives no line");
    return;
  }
  g_crash_test = IniKey("crash_log_test", 0);
  g_crash_caught_test = IniKey("crash_log_caught_test", 0);
  ReadModules();
  const bool first = AddVectoredExceptionHandler(1, &FirstChance) != nullptr;
  const uint8_t abort_bytes[] = {0x6A, 0x0A, 0xE8};
  const uint8_t terminate_bytes[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
  const uint8_t amsg_bytes[] = {0x83, 0x3D, 0x20, 0x42, 0xE8, 0x00, 0x01};
  const bool abort_hooked =
      HookAt(kAbort, abort_bytes, sizeof(abort_bytes),
             reinterpret_cast<void*>(&h_Abort),
             reinterpret_cast<void**>(&o_abort), "abort");
  const bool terminate_hooked =
      HookAt(kTerminate, terminate_bytes, sizeof(terminate_bytes),
             reinterpret_cast<void*>(&h_Terminate),
             reinterpret_cast<void**>(&o_terminate), "terminate");
  const bool amsg_hooked =
      HookAt(kAmsgExit, amsg_bytes, sizeof(amsg_bytes),
             reinterpret_cast<void*>(&h_AmsgExit),
             reinterpret_cast<void**>(&o_amsg_exit), "_amsg_exit");
  ModLog("crash log    : crash_log=1. The log gets the first %d C++ "
         "exceptions (%s) and an end of the game in abort (%s), terminate "
         "(%s) and _amsg_exit (%s), each with the return addresses on its "
         "stack. A fault that no handler takes is in the report of a crash "
         "(crash_report)%s",
         kMaxThrowLogs, first ? "ok" : "FAILED", abort_hooked ? "ok" : "FAILED",
         terminate_hooked ? "ok" : "FAILED", amsg_hooked ? "ok" : "FAILED",
         g_crash_test > 0 || g_crash_caught_test > 0
             ? ". A test throw is set (crash_log_test, crash_log_caught_test)"
             : "");
}

// mod_main.cpp, on the frame thread, at the end of each frame (FinishFrame),
// also of the logos and the intro.
void HangWatchFrame() {
  if (g_crash_on) {
    const LONG frame = InterlockedIncrement(&g_crash_frame);
    if (frame == 1) g_frame_thread = GetCurrentThreadId();
    if (frame == 1 || frame % kModuleReadFrames == 0) ReadModules();
    CrashTests(frame);
  }
  if (!g_read) {
    g_read = true;
    const int key = IniKey("hang_watch", 0);
    g_on = key != 0;
    g_always = key == 2;
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
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    g_watch_before = SetUnhandledExceptionFilter(&CrashFilter);
    ModLog("hang watch   : hang_watch=%d. When the frame thread makes no frame "
           "for %d s, the log gets the place where it is%s. A fault that no "
           "handler takes writes its place too",
           g_always ? 2 : 1, kStillSeconds,
           g_always ? ", and also each 8 s while it makes frames" : "");
  }
  if (g_on) InterlockedIncrement(&g_frames);
}
