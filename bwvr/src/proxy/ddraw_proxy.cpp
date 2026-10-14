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
//
// The stub keeps the name ddraw.dll inside the dxwrapper folder, because it
// takes its mode from its own name. backend=dxwrapper.dll loads the wrong
// file and crashes, see docs/backend.md.
//
// A name with no path is looked for beside the game first. Anything the
// chosen backend does not export falls back to system DirectDraw, so a
// partial implementation cannot leave a null pointer behind a thunk.

#include <windows.h>

namespace {

HMODULE g_real = nullptr;
HMODULE g_system = nullptr;

// What happened, for the log. The proxy loads before logging starts, so it
// leaves a note for Startup to print.
char g_note[256] = "backend      : system DirectDraw";

// One slot per forwarded export, filled at load time.
#define DDRAW_EXPORTS(X)            \
  X(AcquireDDThreadLock)            \
  X(CompleteCreateSysmemSurface)    \
  X(D3DParseUnknownCommand)         \
  X(DDGetAttachedSurfaceLcl)        \
  X(DDInternalLock)                 \
  X(DDInternalUnlock)               \
  X(DSoundHelp)                     \
  X(DirectDrawCreate)               \
  X(DirectDrawCreateClipper)        \
  X(DirectDrawCreateEx)             \
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

// Reads the backend name from bwvr.ini. Returns false when the setting names
// the system implementation, or when there is nothing to load.
//
// With no ini file, and with no backend key, the answer is dxwrapper when it
// is installed beside the game. That is what the installer puts there, and
// what every measurement in docs/backend.md was made on. System DirectDraw
// cannot go far above 720p here, so a missing key must not choose it.
bool ReadBackendSetting(char* out, size_t size) {
  if (g_force_system) return false;

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
      wsprintfA(g_note, "backend      : %s", backend);
    } else {
      wsprintfA(g_note,
                "backend      : %s would not load (error %lu), falling back "
                "to system DirectDraw",
                backend, GetLastError());
    }
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

DDRAW_EXPORTS(FORWARD)
#undef FORWARD
