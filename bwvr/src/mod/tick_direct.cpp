// THE CLOCK OF THE GAME WITH NO CHECK OF BUSY WAITING. See docs/backend.md,
// "The clock of the game and the hooks of dxwrapper".
//
// dxwrapper runs the Win32 hooks of its copy of DDrawCompat (v0.3.2) when
// Dd7to9 is on, and the ini cannot turn them off (Dllmain.cpp,
// InstallDd7to9Hooks; DDrawCompat = 0 does not stop them). One of them is in
// front of GetTickCount: each call asks Windows for the cycle time of the
// thread (QueryThreadCycleTime, a call into the kernel), and after 3 million
// cycles it calls Sleep(0) (WaitFunctions.cpp, mitigateBusyWaiting). The
// game calls GetTickCount in the draw (fn_00837DF0, 0x00838302) 1000 to 1250
// times in a frame. In a desktop run of the six scenes of bench_run.ps1 with
// one_walk=1 that check was 8 percent of the time of the frame thread, and
// tick_direct=1 made the work of a frame 0.45 to 0.79 ms shorter.
//
// With tick_direct=1 the import slot of GetTickCount in runblack.exe gets
// this function. It gives the number of GetTickCount: the low 32 bits of
// GetTickCount64, which reads the shared page of the kernel and makes no
// call into the kernel. The game does not wait for its clock in a loop: the
// headset paces the frame (xrWaitFrame). The other hooks of dxwrapper stay.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

extern void ModLog(const char* format, ...);

namespace {

// The import slot of GetTickCount in runblack.exe v1.42 (KERNEL32.dll).
const uintptr_t kGetTickCountSlot = 0x008A91C4;

uint64_t g_calls = 0;  // calls of the game, this window

DWORD WINAPI h_GetTickCount() {
  ++g_calls;
  return static_cast<DWORD>(GetTickCount64());
}

}  // namespace

// True when the address is in the code of dxwrapper.dll.
static bool InDxwrapper(const void* address) {
  HMODULE module = nullptr;
  char name[MAX_PATH] = {};
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<const char*>(address), &module) ||
      GetModuleFileNameA(module, name, MAX_PATH) == 0) {
    return false;
  }
  const char* base = strrchr(name, '\\');
  return _stricmp(base != nullptr ? base + 1 : name, "dxwrapper.dll") == 0;
}

// Puts the function in the slot. The slot holds the GetTickCount of kernel32
// (dxwrapper changes the code of that function, a detour, and not the slot)
// or a hook of dxwrapper. A slot with anything else is not the slot of this
// build, or a different program changed it, so it stays.
void TickDirectInstall(bool on) {
  if (!on) {
    ModLog("tick direct  : tick_direct=0. The game calls GetTickCount through "
           "the hooks of dxwrapper, with its check of busy waiting");
    return;
  }
  void** slot = reinterpret_cast<void**>(kGetTickCountSlot);
  const HMODULE kernel = GetModuleHandleA("kernel32.dll");
  void* real = kernel != nullptr
                   ? reinterpret_cast<void*>(GetProcAddress(kernel, "GetTickCount"))
                   : nullptr;
  DWORD old = 0;
  const bool ok = real != nullptr && !IsBadReadPtr(slot, sizeof(void*)) &&
                  (*slot == real || InDxwrapper(*slot)) &&
                  VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
  if (ok) {
    *slot = reinterpret_cast<void*>(&h_GetTickCount);
    VirtualProtect(slot, sizeof(void*), old, &old);
  }
  ModLog("tick direct  : tick_direct=1. The GetTickCount import of the game "
         "(0x%08X) %s", static_cast<unsigned>(kGetTickCountSlot),
         ok ? "reads the clock with no check of busy waiting"
            : "FAILED: the slot does not hold GetTickCount, so the game keeps "
              "the hooks of dxwrapper");
}

void TickDirectReport() {
  if (g_calls == 0) return;
  ModLog("tick direct  : the game read its clock %llu times this window",
         static_cast<unsigned long long>(g_calls));
  g_calls = 0;
}
