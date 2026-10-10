// The test of the end of the mod, for a desktop run (docs/backend.md, "The
// end of the mod").
//
// exit_test_frame=N: at frame N the mod asks the game to end by itself, on
// its usual exit path. A desktop run kills the game, and a killed process
// never comes to that path.
//
// The game writes the profile of the player on that path. From the frame of
// the test to the end of the process, the mod drops each write of the game
// process to the registry, and the log names each one. The log also names
// each file that the process opens for a write. The run scripts put those
// files back.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

void ModLog(const char* format, ...);

namespace {

int g_frame_wanted = 0;
// The test asked for the exit. The registry is closed for writes from here.
bool g_guard = false;
int g_lines = 0;
const int kMaxLines = 80;

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

bool Room() {
  if (g_lines >= kMaxLines) return false;
  ++g_lines;
  return true;
}

// A number for the log, when the value is one.
long Number(DWORD type, const BYTE* data, DWORD size) {
  if (type != REG_DWORD || data == nullptr || size < sizeof(DWORD)) return -1;
  return static_cast<long>(*reinterpret_cast<const DWORD*>(data));
}

LSTATUS WINAPI h_RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved,
                                DWORD type, const BYTE* data, DWORD size) {
  if (!g_guard) return o_RegSetValueExA(key, name, reserved, type, data, size);
  if (Room()) {
    ModLog("exit test    : the game writes the registry value \"%s\" (type "
           "%lu, %lu bytes, number %ld). The mod drops the write",
           name != nullptr ? name : "", type, size, Number(type, data, size));
  }
  return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved,
                                DWORD type, const BYTE* data, DWORD size) {
  if (!g_guard) return o_RegSetValueExW(key, name, reserved, type, data, size);
  if (Room()) {
    ModLog("exit test    : the game writes the registry value \"%ls\" (type "
           "%lu, %lu bytes, number %ld). The mod drops the write",
           name != nullptr ? name : L"", type, size, Number(type, data, size));
  }
  return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegDeleteValueA(HKEY key, LPCSTR name) {
  if (!g_guard) return o_RegDeleteValueA(key, name);
  if (Room()) {
    ModLog("exit test    : the game deletes the registry value \"%s\". The "
           "mod drops the call", name != nullptr ? name : "");
  }
  return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegDeleteValueW(HKEY key, LPCWSTR name) {
  if (!g_guard) return o_RegDeleteValueW(key, name);
  if (Room()) {
    ModLog("exit test    : the game deletes the registry value \"%ls\". The "
           "mod drops the call", name != nullptr ? name : L"");
  }
  return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegDeleteKeyA(HKEY key, LPCSTR name) {
  if (!g_guard) return o_RegDeleteKeyA(key, name);
  if (Room()) {
    ModLog("exit test    : the game deletes the registry key \"%s\". The mod "
           "drops the call", name != nullptr ? name : "");
  }
  return ERROR_SUCCESS;
}

LSTATUS WINAPI h_RegDeleteKeyW(HKEY key, LPCWSTR name) {
  if (!g_guard) return o_RegDeleteKeyW(key, name);
  if (Room()) {
    ModLog("exit test    : the game deletes the registry key \"%ls\". The mod "
           "drops the call", name != nullptr ? name : L"");
  }
  return ERROR_SUCCESS;
}

const DWORD kWriteAccess = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA |
                           FILE_APPEND_DATA;

HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                            LPSECURITY_ATTRIBUTES security, DWORD creation,
                            DWORD flags, HANDLE model) {
  if (g_guard && (access & kWriteAccess) != 0 && name != nullptr && Room()) {
    ModLog("exit test    : the game opens a file for a write: %s", name);
  }
  return o_CreateFileA(name, access, share, security, creation, flags, model);
}

HANDLE WINAPI h_CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                            LPSECURITY_ATTRIBUTES security, DWORD creation,
                            DWORD flags, HANDLE model) {
  if (g_guard && (access & kWriteAccess) != 0 && name != nullptr && Room()) {
    ModLog("exit test    : the game opens a file for a write: %ls", name);
  }
  return o_CreateFileW(name, access, share, security, creation, flags, model);
}

// The functions of Windows that the game and its libraries write with.
int InstallGuards() {
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
    }
  }
  return ok;
}

}  // namespace

void ExitTestSetFrame(int frame) { g_frame_wanted = frame; }

// From FinishFrame, one time a frame.
//
// The message is the one of the close button of the window and of Alt and
// F4, WM_SYSCOMMAND with SC_CLOSE. The window procedure of the game
// (0x007DBF30) answers it with LHSystem::SetTerminate (0x007DB8A0), and
// GGame::Loop (at 0x0054D424) reads that flag and ends the game. A plain
// WM_CLOSE is not the way of the game: it destroys the window, and the game
// runs on with no window.
void ExitTestFrame(uint64_t frame) {
  if (g_frame_wanted <= 0 || g_guard ||
      frame != static_cast<uint64_t>(g_frame_wanted)) {
    return;
  }
  const HWND window = *reinterpret_cast<const HWND*>(kGameWindow);
  if (window == nullptr) {
    ModLog("exit test    : frame %llu, the game has no window, so the mod "
           "cannot ask it to close (exit_test_frame=%d)",
           frame, g_frame_wanted);
    return;
  }
  // The game gets the message only if each guard is there. If not, the run
  // script stops the game at its time limit.
  const int guards = InstallGuards();
  if (guards != 8) {
    ModLog("exit test    : frame %llu, only %d of 8 guards are there, so the "
           "mod does not ask the game to close (exit_test_frame=%d)",
           frame, guards, g_frame_wanted);
    return;
  }
  g_guard = true;
  const BOOL posted = PostMessageA(window, WM_SYSCOMMAND, SC_CLOSE, 0);
  ModLog("exit test    : frame %llu, the mod asks the game to close its "
         "window (exit_test_frame=%d). The message is %s. From here the mod "
         "drops each write to the registry",
         frame, g_frame_wanted, posted ? "posted" : "NOT posted");
}
