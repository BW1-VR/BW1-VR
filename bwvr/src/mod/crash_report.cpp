// The report of a crash (crash_report=1, docs/backend.md, "The report of a
// crash").
//
// A fault that no handler takes ends the game, and Windows records only the
// code and one address. With crash_report=1 an unhandled exception filter of
// the mod writes one block of lines to bwvr.log first:
//  - the code of the exception, the address of the code, with its module and
//    the offset in the module, and the address of the access,
//  - the registers,
//  - the return addresses on the stack that are in a module, the nearest
//    first,
//  - the frame, the Hand state, the hand mode, the object in the Hand and the
//    object of the next pick record.
// Then it writes a small minidump, bwvr-crash.dmp, beside the game, with
// MiniDumpWriteDump of dbghelp.dll. Then it calls the filter that was there
// before it, so the game and Windows do what they did with no report.
//
// The filter goes in when the DLL loads. The C runtime of the game then puts
// its own filter in front of it, and that filter calls the mod's filter for
// each fault that is not a C++ exception (__CxxUnhandledExceptionFilter
// 0x007D5160). At the first frame the filter of the mod goes in front again.
//
// The filter runs on the thread of the fault. It does not use the lock of the
// loader: the module of an address comes from VirtualQuery and from the
// headers of the module in memory, not from the list of the loader. ModLog
// writes and flushes each line.
//
// crash_test_frame=N (a test key): at frame N of FinishFrame the mod writes
// to address 0, so a desktop run shows the report.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scene_sample_math.h"
#include "thread_id.h"

extern void ModLog(const char* format, ...);
uint64_t ControllersFrameCount();
bool HandDirectCrashFacts(int32_t* state, int32_t* mode, void** held,
                          char* held_class, size_t held_size, void** next,
                          char* next_class, size_t next_size,
                          uint64_t* frame);

namespace {

const int kMaxReturns = 32;
const int kStackWords = 4096;

bool g_on = false;
bool g_first_frame_done = false;
int g_test_frame = 0;
// The filter that was there before the filter of the mod. The mod calls it
// after its report.
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
// The frame thread, from the first frame.
DWORD g_frame_thread = 0;
char g_dump_path[MAX_PATH] = "";

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(
    HANDLE process, DWORD process_id, HANDLE file, MINIDUMP_TYPE type,
    PMINIDUMP_EXCEPTION_INFORMATION exception,
    PMINIDUMP_USER_STREAM_INFORMATION user, PMINIDUMP_CALLBACK_INFORMATION cb);
MiniDumpWriteDumpFn g_write_dump = nullptr;

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info);

// The name of a module from the headers in memory: "runblack.exe" for the
// game, else the name in the export directory of the module. False when the
// module has no name there.
bool ModuleName(const uint8_t* base, char* out, size_t size) {
  if (base == reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr))) {
    strcpy_s(out, size, "runblack.exe");
    return true;
  }
  const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return false;
  }
  const IMAGE_NT_HEADERS32* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) {
    return false;
  }
  const IMAGE_DATA_DIRECTORY& dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (dir.VirtualAddress == 0 || dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) {
    return false;
  }
  const IMAGE_EXPORT_DIRECTORY* exports =
      reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
  if (IsBadReadPtr(exports, sizeof(*exports)) || exports->Name == 0) {
    return false;
  }
  const char* name = reinterpret_cast<const char*>(base + exports->Name);
  if (IsBadReadPtr(name, 1)) return false;
  size_t n = 0;
  while (n + 1 < size && !IsBadReadPtr(name + n, 1) && name[n] != 0 &&
         n < 63) {
    out[n] = name[n];
    ++n;
  }
  out[n] = 0;
  return n > 0;
}

// The module of an address, as text: "name+0xOFFSET", or "a module at 0xBASE
// +0xOFFSET" when the module has no name in its headers. False, with
// "0xADDRESS (no module)", when no module holds the address.
bool Where(uint32_t address, char* out, size_t size) {
  MEMORY_BASIC_INFORMATION memory;
  if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &memory,
                   sizeof(memory)) != sizeof(memory) ||
      memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
      memory.AllocationBase == nullptr) {
    sprintf_s(out, size, "0x%08X (no module)", address);
    return false;
  }
  const uint8_t* base = static_cast<const uint8_t*>(memory.AllocationBase);
  const uint32_t offset =
      address - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base));
  char name[64];
  if (ModuleName(base, name, sizeof(name))) {
    sprintf_s(out, size, "%s+0x%X", name, offset);
  } else {
    sprintf_s(out, size, "a module at 0x%08X+0x%X",
              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)),
              offset);
  }
  return true;
}

// True when the word on the stack is the address after a call in a module.
bool IsReturnAddress(uint32_t word) {
  if (word < 0x10000) return false;
  MEMORY_BASIC_INFORMATION memory;
  if (VirtualQuery(reinterpret_cast<LPCVOID>(word), &memory, sizeof(memory)) !=
          sizeof(memory) ||
      memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE) {
    return false;
  }
  const uint8_t* before = reinterpret_cast<const uint8_t*>(word - 7);
  if (IsBadReadPtr(before, 7)) return false;
  return scene_sample::EndsWithCall(before);
}

const char* AccessKind(ULONG_PTR kind) {
  switch (kind) {
    case 0: return "a read of";
    case 1: return "a write to";
    case 8: return "a run of code at";
    default: return "an access to";
  }
}

void Report(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  const CONTEXT* c = info->ContextRecord;
  const DWORD thread = ThisThreadId();
  const uint32_t code_at =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(record->ExceptionAddress));
  char at[96];
  Where(code_at, at, sizeof(at));
  char access[96] = "";
  if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
       record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
      record->NumberParameters >= 2) {
    sprintf_s(access, ". It was %s 0x%08X",
              AccessKind(record->ExceptionInformation[0]),
              static_cast<uint32_t>(record->ExceptionInformation[1]));
  }
  ModLog("crash        : a fault that no handler takes. Exception 0x%08lX at "
         "%s, on thread %lu (%s)%s",
         record->ExceptionCode, at, thread,
         thread == g_frame_thread ? "the frame thread" : "not the frame thread",
         access);
  ModLog("crash        : registers eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX "
         "esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX eflags=%08lX",
         c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp,
         c->Eip, c->EFlags);

  // The return addresses: each word of the stack from esp to the top of the
  // stack of this thread, at most kStackWords, that is the address after a
  // call in a module.
  const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
  const uint32_t top =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tib->StackBase));
  uint32_t words = 0;
  if (c->Esp < top) words = (top - c->Esp) / sizeof(uint32_t);
  if (words > static_cast<uint32_t>(kStackWords)) words = kStackWords;
  static char line[3072];
  int used = 0;
  int found = 0;
  const uint32_t* stack = reinterpret_cast<const uint32_t*>(
      static_cast<uintptr_t>(c->Esp));
  for (uint32_t w = 0; w < words && found < kMaxReturns; ++w) {
    if (IsBadReadPtr(stack + w, sizeof(uint32_t))) break;
    const uint32_t word = stack[w];
    if (!IsReturnAddress(word)) continue;
    char where[96];
    Where(word, where, sizeof(where));
    used += sprintf_s(line + used, sizeof(line) - used, "%s%s",
                      found == 0 ? "" : ", ", where);
    ++found;
    if (used >= static_cast<int>(sizeof(line)) - 120) break;
  }
  if (found == 0) strcpy_s(line, "none");
  ModLog("crash        : %d return addresses on the stack, the nearest first: "
         "%s", found, line);

  int32_t state = 0, mode = 0;
  void* held = nullptr;
  void* next = nullptr;
  char held_class[48] = "", next_class[48] = "";
  uint64_t hand_frame = 0;
  if (HandDirectCrashFacts(&state, &mode, &held, held_class,
                           sizeof(held_class), &next, next_class,
                           sizeof(next_class), &hand_frame)) {
    ModLog("crash        : frame %llu (hand frame %llu). Hand state %d, hand "
           "mode 0x%X. The Hand holds %p (%s). The pick record holds %p (%s)",
           ControllersFrameCount(), hand_frame, state, mode, held, held_class,
           next, next_class);
  } else {
    ModLog("crash        : frame %llu (hand frame %llu). The game has no Hand "
           "now", ControllersFrameCount(), hand_frame);
  }
}

void WriteDump(EXCEPTION_POINTERS* info) {
  if (g_write_dump == nullptr || g_dump_path[0] == 0) {
    ModLog("crash        : no dump. dbghelp.dll is not loaded");
    return;
  }
  const HANDLE file = CreateFileA(g_dump_path, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    ModLog("crash        : no dump. The file %s did not open (error %lu)",
           g_dump_path, GetLastError());
    return;
  }
  MINIDUMP_EXCEPTION_INFORMATION exception;
  exception.ThreadId = ThisThreadId();
  exception.ExceptionPointers = info;
  exception.ClientPointers = FALSE;
  // A small dump: the threads, their stacks, the modules, and the memory
  // that the stacks point to.
  const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
      MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
  const BOOL ok = g_write_dump(GetCurrentProcess(), GetCurrentProcessId(),
                               file, type, &exception, nullptr, nullptr);
  const DWORD error = ok ? 0 : GetLastError();
  LARGE_INTEGER bytes;
  bytes.QuadPart = 0;
  GetFileSizeEx(file, &bytes);
  FlushFileBuffers(file);
  CloseHandle(file);
  if (ok) {
    ModLog("crash        : the dump is %s, %lld KiB", g_dump_path,
           bytes.QuadPart / 1024);
  } else {
    ModLog("crash        : the dump %s failed (error 0x%08lX)", g_dump_path,
           error);
  }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  // One report. A second fault, or a filter of the chain that calls this
  // filter again, goes on to the next filter.
  static volatile LONG s_once = 0;
  if (InterlockedExchange(&s_once, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
  Report(info);
  WriteDump(info);
  ModLog("crash        : the report ends. The filter before the mod's "
         "filter %s now", g_previous != nullptr ? "runs" : "is not there, "
         "Windows ends the game");
  if (g_previous != nullptr) return g_previous(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

// mod_main.cpp, EarlyHooks, when the DLL loads: the key, and the filter.
void CrashReportInstall(const char* ini) {
  g_on = GetPrivateProfileIntA("bwvr", "crash_report", 1, ini) != 0;
  g_test_frame =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "crash_test_frame", 0, ini));
  if (!g_on) {
    ModLog("crash report : crash_report=0. A fault writes no report and no "
           "dump");
    return;
  }
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash != nullptr && (slash - path) + 16 < MAX_PATH) {
    slash[1] = 0;
    sprintf_s(g_dump_path, "%sbwvr-crash.dmp", path);
  }
  g_previous = SetUnhandledExceptionFilter(&CrashFilter);
  ModLog("crash report : crash_report=1. A fault that no handler takes writes "
         "a report to this log and the dump %s", g_dump_path);
}

// mod_main.cpp, on the frame thread at the end of each frame (FinishFrame),
// with the frame of FinishFrame.
void CrashReportFrame(uint64_t frame) {
  if (!g_on) return;
  if (!g_first_frame_done) {
    g_first_frame_done = true;
    g_frame_thread = ThisThreadId();
    // A stack overflow of the frame thread leaves 64 KiB of its stack for
    // the report.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    // dbghelp.dll of Windows, loaded here and not in the filter: a load
    // needs the lock of the loader.
    HMODULE dbghelp = LoadLibraryExA("dbghelp.dll", nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbghelp == nullptr) dbghelp = LoadLibraryA("dbghelp.dll");
    if (dbghelp != nullptr) {
      g_write_dump = reinterpret_cast<MiniDumpWriteDumpFn>(
          GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    // The C runtime of the game put its filter in front of the mod's filter.
    // The mod's filter goes in front again, and it calls that filter after
    // its report. When the filter is still the mod's, nothing changes.
    const LPTOP_LEVEL_EXCEPTION_FILTER now =
        SetUnhandledExceptionFilter(&CrashFilter);
    if (now != &CrashFilter) g_previous = now;
    ModLog("crash report : at the first frame the filter of the mod is first. "
           "The filter after it is at %p. MiniDumpWriteDump %s",
           reinterpret_cast<void*>(g_previous),
           g_write_dump != nullptr ? "is ready" : "is not there, so no dump");
  }
  if (g_test_frame > 0 && frame == static_cast<uint64_t>(g_test_frame)) {
    ModLog("crash report : test key crash_test_frame=%d. The mod writes to "
           "address 0 now", g_test_frame);
    // No box of Windows for the fault of the test: a desktop run must not
    // put a window in front. The fault, the report and the record of
    // Windows are as for each other fault.
    SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX);
    int* volatile nowhere = nullptr;
    *nowhere = 1;
  }
}
