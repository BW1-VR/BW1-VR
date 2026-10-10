// The test of the end of the mod, for a desktop run (docs/backend.md, "The
// end of the mod").
//
// exit_test_frame=N: at frame N the mod asks the game to end by itself, on
// its usual exit path. A desktop run kills the game, and a killed process
// never comes to that path. The word "exit" of the scripted controller asks
// for the same thing at a frame of the script.
//
// The game writes the profile of the player on that path. From the frame of
// the test to the end of the process, the mod drops each write of the game
// process to the registry, and the log names each one. The log also names
// each file that the process opens for a write. The run scripts put those
// files back.
//
// registry_test_guard=1: a guard from the start of the mod, for a run that
// loads a test save (docs/backend.md, "A test save"). The game writes the
// values of the profile to the registry in its start too. The mod drops and
// names each write to a key of the game. A write of Windows to a different
// key passes, as in each other run.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "frame_profile.h"

void ModLog(const char* format, ...);

namespace {

int g_frame_wanted = 0;
// The registry is closed for writes from here: the test asked for the exit,
// or registry_test_guard=1 closed it at the start.
bool g_guard = false;
// The test asked for the exit.
bool g_asked = false;
// The first word of a line of the guard.
const char* g_tag = "exit test    ";
// The lines about a write that the mod drops, about a write that passes and
// about a file have a limit each, so one kind does not take the lines of a
// different kind.
int g_lines = 0;
const int kMaxLines = 120;
int g_pass_lines = 0;
const int kMaxPassLines = 6;
int g_file_lines = 0;
const int kMaxFileLines = 40;
// The writes to the registry that the mod dropped, and the writes that
// passed.
int g_dropped = 0;
int g_passed = 0;

// The window of the game, as controllers.cpp reads it.
const uintptr_t kGameWindow = 0x00E85078;

typedef LSTATUS(WINAPI* SetValueAFn)(HKEY, LPCSTR, DWORD, DWORD, const BYTE*,
                                     DWORD);
typedef LSTATUS(WINAPI* SetValueWFn)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*,
                                     DWORD);
typedef LSTATUS(WINAPI* DeleteAFn)(HKEY, LPCSTR);
typedef LSTATUS(WINAPI* DeleteWFn)(HKEY, LPCWSTR);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD,
                                      LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD,
                                      LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
SetValueAFn o_RegSetValueExA = nullptr;
SetValueWFn o_RegSetValueExW = nullptr;
DeleteAFn o_RegDeleteValueA = nullptr;
DeleteWFn o_RegDeleteValueW = nullptr;
DeleteAFn o_RegDeleteKeyA = nullptr;
DeleteWFn o_RegDeleteKeyW = nullptr;
CreateFileAFn o_CreateFileA = nullptr;
CreateFileWFn o_CreateFileW = nullptr;

// The path of an open key of the registry, for the log and for the test of
// the game's own key. NtQueryKey gives it. A root key such as
// HKEY_CURRENT_USER is not an open key: its path is empty here.
typedef LONG(NTAPI* NtQueryKeyFn)(HANDLE, int, PVOID, ULONG, PULONG);
const int kKeyNameInformation = 3;

void KeyPath(HKEY key, char* out, int size) {
  out[0] = 0;
  static NtQueryKeyFn query = nullptr;
  static bool looked = false;
  if (!looked) {
    looked = true;
    const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll != nullptr) {
      query = reinterpret_cast<NtQueryKeyFn>(
          GetProcAddress(ntdll, "NtQueryKey"));
    }
  }
  if (query == nullptr || key == nullptr ||
      (reinterpret_cast<uintptr_t>(key) & 0x80000000u) != 0) {
    return;
  }
  struct {
    ULONG bytes;
    wchar_t name[512];
  } info;
  ULONG got = 0;
  if (query(key, kKeyNameInformation, &info, sizeof(info), &got) != 0) return;
  const int chars = static_cast<int>(info.bytes / sizeof(wchar_t));
  int n = 0;
  for (int i = 0; i < chars && i < 512 && n < size - 1; ++i) {
    const wchar_t c = info.name[i];
    out[n++] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
  }
  out[n] = 0;
}

// The place of a text in a path, with no difference between capital and
// small letters. Null when it is not there.
const char* FindNoCase(const char* path, const char* text) {
  const size_t length = strlen(text);
  for (const char* p = path; *p != 0; ++p) {
    if (_strnicmp(p, text, length) == 0) return p;
  }
  return nullptr;
}

// The keys of the game are under "Lionhead Studios Ltd": the values of the
// profile, the display size and the sound volumes.
bool IsGameKey(const char* path) {
  return FindNoCase(path, "\\Lionhead Studios Ltd") != nullptr;
}

// The part of a path that the log shows: from "\Software\" on.
const char* ShortPath(const char* path) {
  if (path[0] == 0) return "a root key";
  const char* software = FindNoCase(path, "\\Software\\");
  return software != nullptr ? software + 1 : path;
}

// WHAT THE GUARD DOES WITH A WRITE. After the test asked for the exit, the
// mod drops each write of the process. Before that, with
// registry_test_guard=1, it drops each write to a key of the game and each
// write to a key with no path. A write to a different key passes: Windows
// makes such writes in the game process at each start of the game
// (DirectInput keeps its list of devices there), and they are not values of
// the game. True when the mod drops the write.
bool DropWrite(HKEY key, char* path, int size) {
  KeyPath(key, path, size);
  if (g_asked || path[0] == 0 || IsGameKey(path)) {
    ++g_dropped;
    return true;
  }
  ++g_passed;
  return false;
}

// True when the log has room for a line about a write that the mod drops.
// The last line with room says that no more lines come.
bool Room() {
  if (g_lines > kMaxLines) return false;
  ++g_lines;
  if (g_lines > kMaxLines) {
    ModLog("%s: the log has %d lines about writes that the mod dropped, and "
           "it gets no more. The mod drops each more write too", g_tag,
           kMaxLines);
    return false;
  }
  return true;
}

// The same for a write that passes, and for a file.
bool PassRoom() {
  if (g_pass_lines >= kMaxPassLines) return false;
  ++g_pass_lines;
  return true;
}

bool FileRoom() {
  if (g_file_lines >= kMaxFileLines) return false;
  ++g_file_lines;
  return true;
}

// A number for the log, when the value is one.
long Number(DWORD type, const BYTE* data, DWORD size) {
  if (type != REG_DWORD || data == nullptr || size < sizeof(DWORD)) return -1;
  return static_cast<long>(*reinterpret_cast<const DWORD*>(data));
}

// The name of a value or of a key as plain text for the log.
void Plain(const wchar_t* name, char* out, int size) {
  int n = 0;
  for (; name != nullptr && name[n] != 0 && n < size - 1; ++n) {
    out[n] = name[n] >= 32 && name[n] < 127 ? static_cast<char>(name[n]) : '?';
  }
  out[n] = 0;
}

// One line for a write to a value. True when the mod drops the write.
bool GuardValue(HKEY key, const char* name, DWORD type, const BYTE* data,
                DWORD size) {
  char path[300];
  if (!DropWrite(key, path, sizeof(path))) {
    if (PassRoom()) {
      ModLog("%s: the process writes the registry value \"%s\" in the key "
             "%s, which is not a key of the game. The write passes",
             g_tag, name, ShortPath(path));
    }
    return false;
  }
  if (Room()) {
    ModLog("%s: the game writes the registry value \"%s\" (type %lu, %lu "
           "bytes, number %ld) in the key %s. The mod drops the write",
           g_tag, name, type, size, Number(type, data, size), ShortPath(path));
  }
  return true;
}

// One line for a delete of a value or of a key. True when the mod drops it.
bool GuardDelete(HKEY key, const char* what, const char* name) {
  char path[300];
  if (!DropWrite(key, path, sizeof(path))) {
    if (PassRoom()) {
      ModLog("%s: the process deletes the registry %s \"%s\" in the key %s, "
             "which is not a key of the game. The call passes",
             g_tag, what, name, ShortPath(path));
    }
    return false;
  }
  if (Room()) {
    ModLog("%s: the game deletes the registry %s \"%s\" in the key %s. The "
           "mod drops the call", g_tag, what, name, ShortPath(path));
  }
  return true;
}

LSTATUS WINAPI h_RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved,
                                DWORD type, const BYTE* data, DWORD size) {
  if (g_guard &&
      GuardValue(key, name != nullptr ? name : "", type, data, size)) {
    return ERROR_SUCCESS;
  }
  return o_RegSetValueExA(key, name, reserved, type, data, size);
}

LSTATUS WINAPI h_RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved,
                                DWORD type, const BYTE* data, DWORD size) {
  if (g_guard) {
    char plain[128];
    Plain(name, plain, sizeof(plain));
    if (GuardValue(key, plain, type, data, size)) return ERROR_SUCCESS;
  }
  return o_RegSetValueExW(key, name, reserved, type, data, size);
}

LSTATUS WINAPI h_RegDeleteValueA(HKEY key, LPCSTR name) {
  if (g_guard && GuardDelete(key, "value", name != nullptr ? name : "")) {
    return ERROR_SUCCESS;
  }
  return o_RegDeleteValueA(key, name);
}

LSTATUS WINAPI h_RegDeleteValueW(HKEY key, LPCWSTR name) {
  if (g_guard) {
    char plain[128];
    Plain(name, plain, sizeof(plain));
    if (GuardDelete(key, "value", plain)) return ERROR_SUCCESS;
  }
  return o_RegDeleteValueW(key, name);
}

LSTATUS WINAPI h_RegDeleteKeyA(HKEY key, LPCSTR name) {
  if (g_guard && GuardDelete(key, "key", name != nullptr ? name : "")) {
    return ERROR_SUCCESS;
  }
  return o_RegDeleteKeyA(key, name);
}

LSTATUS WINAPI h_RegDeleteKeyW(HKEY key, LPCWSTR name) {
  if (g_guard) {
    char plain[128];
    Plain(name, plain, sizeof(plain));
    if (GuardDelete(key, "key", plain)) return ERROR_SUCCESS;
  }
  return o_RegDeleteKeyW(key, name);
}

const DWORD kWriteAccess = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA |
                           FILE_APPEND_DATA;

// A device of Windows that the process opens as a file, for example a HID
// device of DirectInput. It is not a file, so it has no line.
bool IsDevice(const char* name) {
  return _strnicmp(name, "\\\\?\\hid#", 8) == 0 ||
         _strnicmp(name, "\\\\.\\", 4) == 0;
}

// THE FRAME COST MEASURE (profile=1, frame_profile.cpp) gets the time and the
// name of each file that the frame thread opens. One of the two hooks can
// call the other one in Windows, so only the outer call counts.
int g_open_depth = 0;

HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                            LPSECURITY_ATTRIBUTES security, DWORD creation,
                            DWORD flags, HANDLE model) {
  if (g_guard && (access & kWriteAccess) != 0 && name != nullptr &&
      !IsDevice(name) && FileRoom()) {
    ModLog("%s: the game opens a file for a write: %s", g_tag, name);
  }
  if (!FrameProfileOn()) {
    return o_CreateFileA(name, access, share, security, creation, flags,
                         model);
  }
  const uint64_t start = FrameProfileTicks();
  ++g_open_depth;
  const HANDLE file =
      o_CreateFileA(name, access, share, security, creation, flags, model);
  --g_open_depth;
  if (g_open_depth == 0 && name != nullptr && !IsDevice(name)) {
    FrameProfileAdd(kFpOpen, FrameProfileTicks() - start);
    FrameProfileCount(kFpcOpens, 1);
    FrameProfileFile(name);
  }
  return file;
}

HANDLE WINAPI h_CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                            LPSECURITY_ATTRIBUTES security, DWORD creation,
                            DWORD flags, HANDLE model) {
  if (g_guard && (access & kWriteAccess) != 0 && name != nullptr) {
    char plain[300];
    Plain(name, plain, sizeof(plain));
    if (!IsDevice(plain) && FileRoom()) {
      ModLog("%s: the game opens a file for a write: %s", g_tag, plain);
    }
  }
  if (!FrameProfileOn()) {
    return o_CreateFileW(name, access, share, security, creation, flags,
                         model);
  }
  const uint64_t start = FrameProfileTicks();
  ++g_open_depth;
  const HANDLE file =
      o_CreateFileW(name, access, share, security, creation, flags, model);
  --g_open_depth;
  if (g_open_depth == 0 && name != nullptr) {
    char plain[300];
    Plain(name, plain, sizeof(plain));
    if (!IsDevice(plain)) {
      FrameProfileAdd(kFpOpen, FrameProfileTicks() - start);
      FrameProfileCount(kFpcOpens, 1);
      FrameProfileFile(plain);
    }
  }
  return file;
}

// The functions of Windows that the game and its libraries write with. The
// guards go in one time. Returns how many of the 8 are there.
int g_guards = -1;
int g_file_guards = 0;  // of the 2 of CreateFile
int InstallGuards() {
  if (g_guards >= 0) return g_guards;
  struct Guard {
    const char* dll;
    const char* name;
    void* hook;
    void** original;
  };
  const Guard guards[] = {
      {"advapi32.dll", "RegSetValueExA", &h_RegSetValueExA,
       reinterpret_cast<void**>(&o_RegSetValueExA)},
      {"advapi32.dll", "RegSetValueExW", &h_RegSetValueExW,
       reinterpret_cast<void**>(&o_RegSetValueExW)},
      {"advapi32.dll", "RegDeleteValueA", &h_RegDeleteValueA,
       reinterpret_cast<void**>(&o_RegDeleteValueA)},
      {"advapi32.dll", "RegDeleteValueW", &h_RegDeleteValueW,
       reinterpret_cast<void**>(&o_RegDeleteValueW)},
      {"advapi32.dll", "RegDeleteKeyA", &h_RegDeleteKeyA,
       reinterpret_cast<void**>(&o_RegDeleteKeyA)},
      {"advapi32.dll", "RegDeleteKeyW", &h_RegDeleteKeyW,
       reinterpret_cast<void**>(&o_RegDeleteKeyW)},
      {"kernel32.dll", "CreateFileA", &h_CreateFileA,
       reinterpret_cast<void**>(&o_CreateFileA)},
      {"kernel32.dll", "CreateFileW", &h_CreateFileW,
       reinterpret_cast<void**>(&o_CreateFileW)},
  };
  int ok = 0;
  for (const Guard& g : guards) {
    const HMODULE dll = GetModuleHandleA(g.dll);
    void* target =
        dll != nullptr ? reinterpret_cast<void*>(GetProcAddress(dll, g.name))
                       : nullptr;
    if (target != nullptr &&
        MH_CreateHook(target, g.hook, g.original) == MH_OK &&
        MH_EnableHook(target) == MH_OK) {
      ++ok;
      if (g.hook == reinterpret_cast<void*>(&h_CreateFileA) ||
          g.hook == reinterpret_cast<void*>(&h_CreateFileW)) {
        ++g_file_guards;
      }
    }
  }
  g_guards = ok;
  return ok;
}

// The message is the one of the close button of the window and of Alt and
// F4, WM_SYSCOMMAND with SC_CLOSE. The window procedure of the game
// (0x007DBF30) answers it with LHSystem::SetTerminate (0x007DB8A0), and
// GGame::Loop (at 0x0054D424) reads that flag and ends the game. A plain
// WM_CLOSE is not the way of the game: it destroys the window, and the game
// runs on with no window.
void AskTheGameToClose(const char* why, uint64_t frame) {
  if (g_asked) return;
  const HWND window = *reinterpret_cast<const HWND*>(kGameWindow);
  if (window == nullptr) {
    ModLog("exit test    : frame %llu, the game has no window, so the mod "
           "cannot ask it to close (%s)", frame, why);
    return;
  }
  // The game gets the message only if each guard is there. If not, the run
  // script stops the game at its time limit.
  const int guards = InstallGuards();
  if (guards != 8) {
    ModLog("exit test    : frame %llu, only %d of 8 guards are there, so the "
           "mod does not ask the game to close (%s)", frame, guards, why);
    return;
  }
  g_guard = true;
  g_asked = true;
  g_tag = "exit test    ";
  const BOOL posted = PostMessageA(window, WM_SYSCOMMAND, SC_CLOSE, 0);
  ModLog("exit test    : frame %llu, the mod asks the game to close its "
         "window (%s). The message is %s. From here the mod drops each write "
         "to the registry", frame, why, posted ? "posted" : "NOT posted");
}

}  // namespace

void ExitTestSetFrame(int frame) { g_frame_wanted = frame; }

// For the frame cost measure (frame_profile.cpp): the hooks go in, and no
// guard goes on. Each hook passes its call while no guard is on. Returns how
// many of the 2 hooks of CreateFile are there.
int ExitTestFileHooks() {
  InstallGuards();
  return g_file_guards;
}

// registry_test_guard=1, from Startup: the guard of the registry from the
// start of the mod. The game reads the registry as it is, and the mod drops
// and names each write to a key of the game (DropWrite).
void ExitTestGuardFromStart() {
  const int guards = InstallGuards();
  if (guards != 8) {
    ModLog("registry test: only %d of 8 guards are there, so the registry is "
           "NOT closed for writes (registry_test_guard=1)", guards);
    return;
  }
  g_guard = true;
  g_tag = "registry test";
  ModLog("registry test: registry_test_guard=1 (a test). From here the mod "
         "drops each write of the game process to a key of the game in the "
         "registry (Lionhead Studios Ltd), and the log names each one. A "
         "write to a different key passes: the log names the first %d and "
         "counts each one. The log also names each file that the process "
         "opens for a write", kMaxPassLines);
}

// How many writes to the registry the mod dropped and how many passed, and
// whether the guard is on. For the line of the start of the game
// (game_start.cpp).
int ExitTestDropped(bool* guard, int* passed) {
  if (guard != nullptr) *guard = g_guard;
  if (passed != nullptr) *passed = g_passed;
  return g_dropped;
}

// From FinishFrame, one time a frame, with the frame of FinishFrame.
void ExitTestFrame(uint64_t frame) {
  if (g_frame_wanted <= 0 || g_asked ||
      frame != static_cast<uint64_t>(g_frame_wanted)) {
    return;
  }
  char why[48];
  wsprintfA(why, "exit_test_frame=%d", g_frame_wanted);
  AskTheGameToClose(why, frame);
}

// The word "exit" of the scripted controller, with the frame of the script.
void ExitTestNow(uint64_t frame) {
  AskTheGameToClose("the word \"exit\" of the scripted controller", frame);
}
