// A pass-through ddraw.dll. The game imports DirectDrawCreate,
// DirectDrawCreateEx and DirectDrawEnumerateA by name, so a DLL of this name
// beside runblack.exe loads first. Every export is forwarded to a real
// DirectDraw implementation.
//
// WHICH implementation is a setting, because it is the hinge of the whole
// performance problem.
//
// System DirectDraw is the default and is what the game shipped against. It
// keeps the finished frame in a surface that only the CPU can read, so the
// mod has to lock it, copy the pixels through system memory and upload them
// again to reach the headset. That round trip costs more than the game's own
// drawing does at any resolution above 720p, and nothing in the mod can make
// it cheaper.
//
// The other two implementations reimplement DirectDraw on a modern API:
//
//   dxwrapper.dll   Direct3D 7 onto Direct3D 9, Windows native
//   d7vk ddraw.dll  Direct3D 7 onto Vulkan
//
// Either one puts the finished frame in a resource that a modern device can
// read directly, so the round trip through system memory disappears.
//
// Set the backend in bwvr.ini:
//
//   [bwvr]
//   backend=                     dxwrapper, when it is installed beside the
//                                game. This is the default, and it is what
//                                the installer puts there.
//   backend=dxwrapper\ddraw.dll  the same, named in full
//   backend=system               system DirectDraw, which cannot go far
//                                above 720p here
//   backend=vulkan               DirectDraw of the mod on Vulkan
//                                (vk_ddraw.cpp). With vulkan_fallback=1
//                                (the default) the start takes dxwrapper
//                                when the file bwvr-vulkan-pending.txt is
//                                beside the game, or when there is no
//                                vulkan-1.dll (TestVulkan).
//
// The stub keeps the name ddraw.dll inside the dxwrapper folder, because it
// takes its mode from its own name. backend=dxwrapper.dll loads the wrong
// file and crashes, see docs/backend.md.
//
// A name with no path is looked for beside the game first. Anything the
// chosen backend does not export falls back to system DirectDraw, so a
// partial implementation cannot leave a null pointer behind a thunk.

#include <windows.h>

#include "ddraw_proxy.h"

namespace {

HMODULE g_real = nullptr;
HMODULE g_system = nullptr;

// What happened, for the log. The proxy loads before logging starts, so it
// leaves a note for Startup to print.
char g_note[256] = "backend      : system DirectDraw";

// One slot per export, filled at load time. The exports that go to the
// backend with a naked thunk:
#define DDRAW_FORWARDED(X)          \
  X(AcquireDDThreadLock)            \
  X(CompleteCreateSysmemSurface)    \
  X(D3DParseUnknownCommand)         \
  X(DDGetAttachedSurfaceLcl)        \
  X(DDInternalLock)                 \
  X(DDInternalUnlock)               \
  X(DSoundHelp)                     \
  X(DirectDrawCreateClipper)        \
  X(DirectDrawEnumerateA)           \
  X(DirectDrawEnumerateExA)         \
  X(DirectDrawEnumerateExW)         \
  X(DirectDrawEnumerateW)           \
  X(DllCanUnloadNow)                \
  X(DllGetClassObject)              \
  X(GetDDSurfaceLocal)              \
  X(GetOLEThunkData)                \
  X(GetSurfaceFromDC)               \
  X(RegisterSpecialCase)            \
  X(ReleaseDDThreadLock)            \
  X(SetAppCompatData)

// Each export. The two that make a DirectDraw object go through a function
// (Proxy_DirectDrawCreateEx, Proxy_DirectDrawCreate).
#define DDRAW_EXPORTS(X) \
  DDRAW_FORWARDED(X)     \
  X(DirectDrawCreate)    \
  X(DirectDrawCreateEx)

#define DECLARE_SLOT(name) FARPROC real_##name = nullptr;
DDRAW_EXPORTS(DECLARE_SLOT)
#undef DECLARE_SLOT

// The folder the game runs from, with a trailing backslash.
void GameFolder(char* out, size_t size) {
  out[0] = 0;
  char exe[MAX_PATH];
  if (GetModuleFileNameA(nullptr, exe, MAX_PATH) == 0) return;
  int cut = -1;
  for (int i = 0; exe[i] != 0; ++i) {
    if (exe[i] == '\\' || exe[i] == '/') cut = i;
  }
  if (cut < 0) return;
  exe[cut + 1] = 0;
  lstrcpynA(out, exe, static_cast<int>(size));
}

// Set when the game is a build the mod does not fit. Then nothing of the mod
// runs, and the game gets the DirectDraw it would have had on its own.
bool g_force_system = false;

// backend=vulkan (ReadBackendSetting).
bool g_vulkan = false;

// THE FALL BACK OF backend=vulkan (vulkan_fallback=1, the default). The mod
// writes this file beside the game before it uses Vulkan, and the first
// picture deletes it (vk_fallback.cpp). A fault of Vulkan writes it again.
// When the file is there at a start, the start before ended before its
// first picture, or Vulkan failed. Then this start uses dxwrapper.
const char kVulkanPendingFile[] = "bwvr-vulkan-pending.txt";

// Why backend=vulkan uses dxwrapper in this start, or empty.
char g_vulkan_fallback[192] = "";

// Fills g_vulkan_fallback when backend=vulkan cannot start in this process.
// Only cheap tests: the proxy is in DllMain, under the loader lock.
void TestVulkan(const char* folder, const char* ini) {
  g_vulkan_fallback[0] = 0;
  if (GetPrivateProfileIntA("bwvr", "vulkan_fallback", 1, ini) == 0) return;
  char pending[MAX_PATH];
  lstrcpynA(pending, folder, MAX_PATH);
  lstrcatA(pending, kVulkanPendingFile);
  if (GetFileAttributesA(pending) != INVALID_FILE_ATTRIBUTES) {
    wsprintfA(g_vulkan_fallback,
              "the start before with Vulkan ended before its first picture, "
              "or Vulkan failed (%s is there)",
              kVulkanPendingFile);
    return;
  }
  char found[MAX_PATH];
  if (SearchPathA(nullptr, "vulkan-1.dll", nullptr, MAX_PATH, found,
                  nullptr) == 0) {
    lstrcpynA(g_vulkan_fallback,
              "there is no vulkan-1.dll (no Vulkan driver)",
              sizeof(g_vulkan_fallback));
  }
}

// Reads the backend name from bwvr.ini. Returns false when the setting names
// the system implementation, or when there is nothing to load.
//
// With no ini file, and with no backend key, the answer is dxwrapper when it
// is installed beside the game. That is what the installer puts there, and
// what every measurement in docs/backend.md was made on. System DirectDraw
// cannot go far above 720p here, so a missing key must not choose it.
//
// The choice is made one time, because the mod asks before
// ProxyLoadRealDDraw too, and the file of the fall back changes after the
// first answer.
bool DecideBackend(char* out, size_t size) {
  char folder[MAX_PATH];
  GameFolder(folder, MAX_PATH);
  if (folder[0] == 0) return false;

  char ini[MAX_PATH];
  lstrcpynA(ini, folder, MAX_PATH);
  lstrcatA(ini, "bwvr.ini");

  out[0] = 0;
  if (GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES) {
    GetPrivateProfileStringA("bwvr", "backend", "", out,
                             static_cast<DWORD>(size), ini);
  }
  // backend=vulkan: the mod makes DirectDraw itself (vk_ddraw.cpp). System
  // DirectDraw gives only the exports that the mod does not make. When
  // Vulkan cannot start, dxwrapper as with no key.
  if (lstrcmpiA(out, "vulkan") == 0) {
    TestVulkan(folder, ini);
    if (g_vulkan_fallback[0] == 0) {
      g_vulkan = true;
      return false;
    }
    out[0] = 0;
  }
  if (out[0] == 0) {
    const char* fallback = "dxwrapper\\ddraw.dll";
    char beside[MAX_PATH];
    lstrcpynA(beside, folder, MAX_PATH);
    lstrcatA(beside, fallback);
    if (GetFileAttributesA(beside) == INVALID_FILE_ATTRIBUTES) return false;
    lstrcpynA(out, fallback, static_cast<int>(size));
    return true;
  }
  if (lstrcmpiA(out, "system") == 0) return false;
  return true;
}

char g_backend[MAX_PATH] = "";
bool g_backend_load = false;
bool g_backend_read = false;

bool ReadBackendSetting(char* out, size_t size) {
  if (g_force_system) return false;
  if (!g_backend_read) {
    g_backend_read = true;
    g_backend_load = DecideBackend(g_backend, MAX_PATH);
  }
  lstrcpynA(out, g_backend, static_cast<int>(size));
  return g_backend_load;
}

// Loads system DirectDraw, on demand only.
//
// The order matters. dxwrapper logs "System ddraw.dll is already loaded
// before dxwrapper" and changes what it does, so the chosen backend has to
// load into a process where the system library is not present yet.
HMODULE SystemDDraw() {
  if (g_system != nullptr) return g_system;
  char path[MAX_PATH];
  UINT len = GetSystemDirectoryA(path, MAX_PATH);
  if (len == 0 || len > MAX_PATH - 16) return nullptr;
  lstrcatA(path, "\\ddraw.dll");
  g_system = LoadLibraryA(path);
  return g_system;
}

}  // namespace

// Called before ProxyLoadRealDDraw when the game build is not the one the
// mod fits. See CheckGameBuild in mod_main.cpp.
void ProxyForceSystemDDraw() { g_force_system = true; }

// The mod asks before ProxyLoadRealDDraw too (EarlyHooks), so the first call
// reads the setting.
bool ProxyBackendVulkan() {
  char backend[MAX_PATH];
  ReadBackendSetting(backend, MAX_PATH);
  return g_vulkan;
}

// Why backend=vulkan uses dxwrapper in this start, or null.
const char* ProxyVulkanFallback() {
  char backend[MAX_PATH];
  ReadBackendSetting(backend, MAX_PATH);
  return g_vulkan_fallback[0] != 0 ? g_vulkan_fallback : nullptr;
}

const char* ProxyVulkanPendingFile() { return kVulkanPendingFile; }

bool ProxyLoadRealDDraw() {
  char backend[MAX_PATH];
  if (ReadBackendSetting(backend, MAX_PATH)) {
    char folder[MAX_PATH];
    GameFolder(folder, MAX_PATH);
    char beside[MAX_PATH];
    lstrcpynA(beside, folder, MAX_PATH);
    lstrcatA(beside, backend);

    g_real = LoadLibraryA(beside);
    if (g_real == nullptr) g_real = LoadLibraryA(backend);

    if (g_real != nullptr) {
      wsprintfA(g_note, "backend      : %s%s", backend,
                g_vulkan_fallback[0] != 0
                    ? " in place of vulkan (the line vulkan fallback says why)"
                    : "");
    } else {
      wsprintfA(g_note,
                "backend      : %s would not load (error %lu), falling back "
                "to system DirectDraw",
                backend, GetLastError());
    }
  }

  if (g_vulkan) {
    lstrcpynA(g_note,
              "backend      : vulkan, DirectDraw of the mod (vk_ddraw.cpp)",
              sizeof(g_note));
  }
  if (g_real == nullptr) g_real = SystemDDraw();
  if (g_real == nullptr) return false;

  // Resolve from the chosen backend, and fill anything it does not export
  // from system DirectDraw so no thunk is left pointing at nothing.
#define RESOLVE(name)                                            \
  real_##name = GetProcAddress(g_real, #name);                   \
  if (real_##name == nullptr) {                                  \
    HMODULE gap = SystemDDraw();                                 \
    if (gap != nullptr) real_##name = GetProcAddress(gap, #name); \
  }
  DDRAW_EXPORTS(RESOLVE)
#undef RESOLVE

  // Only the three the game actually imports are required.
  return real_DirectDrawCreate != nullptr && real_DirectDrawCreateEx != nullptr &&
         real_DirectDrawEnumerateA != nullptr;
}

// An export of the backend that ProxyLoadRealDDraw loaded, or null.
void* ProxyRealExport(const char* name) {
  return g_real != nullptr
             ? reinterpret_cast<void*>(GetProcAddress(g_real, name))
             : nullptr;
}

// What ProxyLoadRealDDraw did, ready to log.
const char* ProxyBackendNote() { return g_note; }

// Naked thunks keep the caller's stack and registers untouched, so the
// forward is transparent regardless of each export's calling convention.
#define FORWARD(name)                                    \
  extern "C" __declspec(naked) void Proxy_##name() {     \
    __asm { jmp dword ptr[real_##name] }                 \
  }

DDRAW_FORWARDED(FORWARD)
#undef FORWARD

namespace {
ProxyCreateHooks g_hooks = {};
}  // namespace

void ProxySetCreateHooks(const ProxyCreateHooks& hooks) { g_hooks = hooks; }

extern "C" HRESULT WINAPI Proxy_DirectDrawCreateEx(GUID* driver, void** out,
                                                   REFIID id, IUnknown* outer);
extern "C" HRESULT WINAPI Proxy_DirectDrawCreate(GUID* driver, void** out,
                                                 IUnknown* outer);

// The first two bytes of the two exports that make a DirectDraw object.
// dxwrapper puts a hot patch on these exports of each module named ddraw.dll
// that is loaded, also of the mod: a short jump back into the bytes before
// the function. A change of these bytes shows it.
void ProxyCreateStarts(unsigned char ex[2], unsigned char old[2]) {
  const unsigned char* a =
      reinterpret_cast<const unsigned char*>(&Proxy_DirectDrawCreateEx);
  const unsigned char* b =
      reinterpret_cast<const unsigned char*>(&Proxy_DirectDrawCreate);
  ex[0] = a[0];
  ex[1] = a[1];
  old[0] = b[0];
  old[1] = b[1];
}

// The two exports that make a DirectDraw object, with the function of the
// mod when one is set (ddraw_proxy.h).
extern "C" HRESULT WINAPI Proxy_DirectDrawCreateEx(GUID* driver, void** out,
                                                   REFIID id,
                                                   IUnknown* outer) {
  const ProxyCreateExFn real =
      reinterpret_cast<ProxyCreateExFn>(real_DirectDrawCreateEx);
  if (g_hooks.create_ex != nullptr) {
    return g_hooks.create_ex(real, driver, out, id, outer);
  }
  return real(driver, out, id, outer);
}

extern "C" HRESULT WINAPI Proxy_DirectDrawCreate(GUID* driver, void** out,
                                                 IUnknown* outer) {
  const ProxyCreateFn real =
      reinterpret_cast<ProxyCreateFn>(real_DirectDrawCreate);
  if (g_hooks.create != nullptr) {
    return g_hooks.create(real, driver, out, outer);
  }
  return real(driver, out, outer);
}
