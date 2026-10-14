// A count of each call of DirectDraw and Direct3D 7 in a whole session
// (ddraw_census=1, a test key, docs/backend.md, "The calls of DirectDraw and
// Direct3D 7 in a session").
//
// WHY. A backend of the mod in place of dxwrapper must make each method that
// the game calls. This count gives the list: each method of each interface
// with a call, the count in the scene walks and outside them, and the
// interfaces that the game asks for with QueryInterface.
//
// HOW. A hook on DirectDrawCreateEx and DirectDrawCreate of the backend sees
// the first DirectDraw object. Its table of methods goes to counting thunks
// (as in d3d7_count.cpp: inc of the count, then a jump to the method). Hooks
// on the methods that make objects (CreateSurface, CreatePalette,
// CreateClipper, QueryInterface, CreateDevice, CreateVertexBuffer,
// GetAttachedSurface) give the table of each new kind of object to the
// thunks too. A table is patched one time: dxwrapper has one table for each
// interface. QueryInterface of each kind logs each new interface id.

#define WIN32_LEAN_AND_MEAN
// The interface ids of DirectDraw and Direct3D 7 for this file.
#define INITGUID
#include <windows.h>
#include <d3d.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "ddraw_census.h"

void ModLog(const char* format, ...);
// proxy/ddraw_proxy.cpp
void* ProxyRealExport(const char* name);

namespace {

enum Kind {
  kDirectDraw,
  kSurface,
  kDirect3D,
  kDevice,
  kPalette,
  kClipper,
  kVertexBuffer,
  kKinds
};

const char* const kKindNames[kKinds] = {
    "IDirectDraw7", "IDirectDrawSurface7", "IDirect3D7", "IDirect3DDevice7",
    "IDirectDrawPalette", "IDirectDrawClipper", "IDirect3DVertexBuffer7"};

const char* const kDirectDrawNames[] = {
    "QueryInterface", "AddRef", "Release", "Compact", "CreateClipper",
    "CreatePalette", "CreateSurface", "DuplicateSurface", "EnumDisplayModes",
    "EnumSurfaces", "FlipToGDISurface", "GetCaps", "GetDisplayMode",
    "GetFourCCCodes", "GetGDISurface", "GetMonitorFrequency", "GetScanLine",
    "GetVerticalBlankStatus", "Initialize", "RestoreDisplayMode",
    "SetCooperativeLevel", "SetDisplayMode", "WaitForVerticalBlank",
    "GetAvailableVidMem", "GetSurfaceFromDC", "RestoreAllSurfaces",
    "TestCooperativeLevel", "GetDeviceIdentifier", "StartModeTest",
    "EvaluateMode"};

const char* const kSurfaceNames[] = {
    "QueryInterface", "AddRef", "Release", "AddAttachedSurface",
    "AddOverlayDirtyRect", "Blt", "BltBatch", "BltFast",
    "DeleteAttachedSurface", "EnumAttachedSurfaces", "EnumOverlayZOrders",
    "Flip", "GetAttachedSurface", "GetBltStatus", "GetCaps", "GetClipper",
    "GetColorKey", "GetDC", "GetFlipStatus", "GetOverlayPosition",
    "GetPalette", "GetPixelFormat", "GetSurfaceDesc", "Initialize",
    "IsLost", "Lock", "ReleaseDC", "Restore", "SetClipper", "SetColorKey",
    "SetOverlayPosition", "SetPalette", "Unlock", "UpdateOverlay",
    "UpdateOverlayDisplay", "UpdateOverlayZOrder", "GetDDInterface",
    "PageLock", "PageUnlock", "SetSurfaceDesc", "SetPrivateData",
    "GetPrivateData", "FreePrivateData", "GetUniquenessValue",
    "ChangeUniquenessValue", "SetPriority", "GetPriority", "SetLOD",
    "GetLOD"};

const char* const kDirect3DNames[] = {
    "QueryInterface", "AddRef", "Release", "EnumDevices", "CreateDevice",
    "CreateVertexBuffer", "EnumZBufferFormats", "EvictManagedTextures"};

const char* const kDeviceNames[] = {
    "QueryInterface", "AddRef", "Release", "GetCaps", "EnumTextureFormats",
    "BeginScene", "EndScene", "GetDirect3D", "SetRenderTarget",
    "GetRenderTarget", "Clear", "SetTransform", "GetTransform", "SetViewport",
    "MultiplyTransform", "GetViewport", "SetMaterial", "GetMaterial",
    "SetLight", "GetLight", "SetRenderState", "GetRenderState",
    "BeginStateBlock", "EndStateBlock", "PreLoad", "DrawPrimitive",
    "DrawIndexedPrimitive", "SetClipStatus", "GetClipStatus",
    "DrawPrimitiveStrided", "DrawIndexedPrimitiveStrided", "DrawPrimitiveVB",
    "DrawIndexedPrimitiveVB", "ComputeSphereVisibility", "GetTexture",
    "SetTexture", "GetTextureStageState", "SetTextureStageState",
    "ValidateDevice", "ApplyStateBlock", "CaptureStateBlock",
    "DeleteStateBlock", "CreateStateBlock", "Load", "LightEnable",
    "GetLightEnable", "SetClipPlane", "GetClipPlane", "GetInfo"};

const char* const kPaletteNames[] = {"QueryInterface", "AddRef", "Release",
                                     "GetCaps", "GetEntries", "Initialize",
                                     "SetEntries"};

const char* const kClipperNames[] = {
    "QueryInterface", "AddRef", "Release", "GetClipList", "GetHWnd",
    "Initialize", "IsClipListChanged", "SetClipList", "SetHWnd"};

const char* const kVertexBufferNames[] = {
    "QueryInterface", "AddRef", "Release", "Lock", "Unlock",
    "ProcessVertices", "GetVertexBufferDesc", "Optimize",
    "ProcessVerticesStrided"};

const char* const* const kNames[kKinds] = {
    kDirectDrawNames, kSurfaceNames, kDirect3DNames, kDeviceNames,
    kPaletteNames, kClipperNames, kVertexBufferNames};
const int kSlotCounts[kKinds] = {30, 49, 8, 49, 7, 9, 9};
const int kMaxSlots = 49;
const int kThunkBytes = 12;

bool g_on = false;
bool g_in_walk = false;
void** g_table[kKinds] = {nullptr};
void* g_original[kKinds][kMaxSlots] = {{nullptr}};
volatile LONG g_count[kKinds][kMaxSlots] = {{0}};
LONG g_walk_start[kKinds][kMaxSlots] = {{0}};
uint64_t g_walk_calls[kKinds][kMaxSlots] = {{0}};
uint8_t* g_thunks = nullptr;
CRITICAL_SECTION g_lock;

// The interface ids that QueryInterface was asked for, by kind.
const int kMaxIds = 64;
struct Asked {
  int kind;
  GUID id;
  LONG count;
};
Asked g_asked[kMaxIds];
int g_asked_count = 0;

// inc dword ptr [count]; jmp dword ptr [original]
void MakeThunk(uint8_t* at, volatile LONG* count, void** original) {
  at[0] = 0xFF;
  at[1] = 0x05;
  const uint32_t c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(count));
  memcpy(at + 2, &c, 4);
  at[6] = 0xFF;
  at[7] = 0x25;
  const uint32_t o =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(original));
  memcpy(at + 8, &o, 4);
}

void Patch(Kind kind, void* object);

// The hooks of the methods that make objects. Each one patches the table of
// the new object.
typedef HRESULT(WINAPI* QueryFn)(void*, REFIID, void**);
typedef HRESULT(WINAPI* CreateSurfaceFn)(void*, LPDDSURFACEDESC2,
                                         LPDIRECTDRAWSURFACE7*, IUnknown*);
typedef HRESULT(WINAPI* CreatePaletteFn)(void*, DWORD, LPPALETTEENTRY,
                                         LPDIRECTDRAWPALETTE*, IUnknown*);
typedef HRESULT(WINAPI* CreateClipperFn)(void*, DWORD, LPDIRECTDRAWCLIPPER*,
                                         IUnknown*);
typedef HRESULT(WINAPI* CreateDeviceFn)(void*, REFCLSID, LPDIRECTDRAWSURFACE7,
                                        LPDIRECT3DDEVICE7*);
typedef HRESULT(WINAPI* CreateVertexBufferFn)(void*,
                                              LPD3DVERTEXBUFFERDESC,
                                              LPDIRECT3DVERTEXBUFFER7*, DWORD);
typedef HRESULT(WINAPI* GetAttachedFn)(void*, LPDDSCAPS2,
                                       LPDIRECTDRAWSURFACE7*);
typedef HRESULT(WINAPI* CreateExFn)(GUID*, LPVOID*, REFIID, IUnknown*);
typedef HRESULT(WINAPI* CreateFn)(GUID*, LPDIRECTDRAW*, IUnknown*);

QueryFn o_Query[kKinds] = {nullptr};
CreateSurfaceFn o_CreateSurface = nullptr;
CreatePaletteFn o_CreatePalette = nullptr;
CreateClipperFn o_CreateClipper = nullptr;
CreateDeviceFn o_CreateDevice = nullptr;
CreateVertexBufferFn o_CreateVertexBuffer = nullptr;
GetAttachedFn o_GetAttached = nullptr;
CreateExFn o_CreateEx = nullptr;
CreateFn o_Create = nullptr;

void NoteQuery(int kind, REFIID id) {
  EnterCriticalSection(&g_lock);
  int i = 0;
  for (; i < g_asked_count; ++i) {
    if (g_asked[i].kind == kind && IsEqualGUID(g_asked[i].id, id)) break;
  }
  if (i == g_asked_count && g_asked_count < kMaxIds) {
    g_asked[i].kind = kind;
    g_asked[i].id = id;
    g_asked[i].count = 0;
    ++g_asked_count;
  }
  if (i < g_asked_count) ++g_asked[i].count;
  LeaveCriticalSection(&g_lock);
}

// The kind of the object that a QueryInterface gives, or kKinds.
Kind KindOf(REFIID id) {
  if (IsEqualGUID(id, IID_IDirectDraw7)) return kDirectDraw;
  if (IsEqualGUID(id, IID_IDirectDrawSurface7)) return kSurface;
  if (IsEqualGUID(id, IID_IDirect3D7)) return kDirect3D;
  if (IsEqualGUID(id, IID_IDirect3DDevice7)) return kDevice;
  return kKinds;
}

template <int K>
HRESULT WINAPI h_Query(void* self, REFIID id, void** out) {
  const HRESULT hr = o_Query[K](self, id, out);
  NoteQuery(K, id);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    const Kind kind = KindOf(id);
    if (kind != kKinds) Patch(kind, *out);
  }
  return hr;
}

HRESULT WINAPI h_CreateSurface(void* self, LPDDSURFACEDESC2 desc,
                               LPDIRECTDRAWSURFACE7* out, IUnknown* outer) {
  const HRESULT hr = o_CreateSurface(self, desc, out, outer);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kSurface, *out);
  return hr;
}

HRESULT WINAPI h_CreatePalette(void* self, DWORD flags, LPPALETTEENTRY entries,
                               LPDIRECTDRAWPALETTE* out, IUnknown* outer) {
  const HRESULT hr = o_CreatePalette(self, flags, entries, out, outer);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kPalette, *out);
  return hr;
}

HRESULT WINAPI h_CreateClipper(void* self, DWORD flags,
                               LPDIRECTDRAWCLIPPER* out, IUnknown* outer) {
  const HRESULT hr = o_CreateClipper(self, flags, out, outer);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kClipper, *out);
  return hr;
}

HRESULT WINAPI h_CreateDevice(void* self, REFCLSID id,
                              LPDIRECTDRAWSURFACE7 target,
                              LPDIRECT3DDEVICE7* out) {
  const HRESULT hr = o_CreateDevice(self, id, target, out);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kDevice, *out);
  return hr;
}

HRESULT WINAPI h_CreateVertexBuffer(void* self, LPD3DVERTEXBUFFERDESC desc,
                                    LPDIRECT3DVERTEXBUFFER7* out,
                                    DWORD flags) {
  const HRESULT hr = o_CreateVertexBuffer(self, desc, out, flags);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    Patch(kVertexBuffer, *out);
  }
  return hr;
}

HRESULT WINAPI h_GetAttached(void* self, LPDDSCAPS2 caps,
                             LPDIRECTDRAWSURFACE7* out) {
  const HRESULT hr = o_GetAttached(self, caps, out);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kSurface, *out);
  return hr;
}

bool HookMethod(void** table, int slot, void* hook, void** original) {
  void* target = table[slot];
  return MH_CreateHook(target, hook, original) == MH_OK &&
         MH_EnableHook(target) == MH_OK;
}

void* QueryHook(Kind kind) {
  switch (kind) {
    case kDirectDraw: return reinterpret_cast<void*>(&h_Query<kDirectDraw>);
    case kSurface: return reinterpret_cast<void*>(&h_Query<kSurface>);
    case kDirect3D: return reinterpret_cast<void*>(&h_Query<kDirect3D>);
    case kDevice: return reinterpret_cast<void*>(&h_Query<kDevice>);
    case kPalette: return reinterpret_cast<void*>(&h_Query<kPalette>);
    case kClipper: return reinterpret_cast<void*>(&h_Query<kClipper>);
    default: return reinterpret_cast<void*>(&h_Query<kVertexBuffer>);
  }
}

// The table of an object of this kind goes to the thunks, one time. The
// hooks of the methods that make objects go in first, so the thunks jump to
// the hooked functions.
void Patch(Kind kind, void* object) {
  void** table = *static_cast<void***>(object);
  EnterCriticalSection(&g_lock);
  if (g_table[kind] != nullptr) {
    if (g_table[kind] != table) {
      ModLog("ddraw census : a second table of %s at 0x%08X is not counted",
             kKindNames[kind],
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(table)));
    }
    LeaveCriticalSection(&g_lock);
    return;
  }
  g_table[kind] = table;
  HookMethod(table, 0, QueryHook(kind),
             reinterpret_cast<void**>(&o_Query[kind]));
  if (kind == kDirectDraw) {
    HookMethod(table, 4, reinterpret_cast<void*>(&h_CreateClipper),
               reinterpret_cast<void**>(&o_CreateClipper));
    HookMethod(table, 5, reinterpret_cast<void*>(&h_CreatePalette),
               reinterpret_cast<void**>(&o_CreatePalette));
    HookMethod(table, 6, reinterpret_cast<void*>(&h_CreateSurface),
               reinterpret_cast<void**>(&o_CreateSurface));
  } else if (kind == kSurface) {
    HookMethod(table, 12, reinterpret_cast<void*>(&h_GetAttached),
               reinterpret_cast<void**>(&o_GetAttached));
  } else if (kind == kDirect3D) {
    HookMethod(table, 4, reinterpret_cast<void*>(&h_CreateDevice),
               reinterpret_cast<void**>(&o_CreateDevice));
    HookMethod(table, 5, reinterpret_cast<void*>(&h_CreateVertexBuffer),
               reinterpret_cast<void**>(&o_CreateVertexBuffer));
  }
  const int slots = kSlotCounts[kind];
  DWORD old = 0;
  if (VirtualProtect(table, slots * sizeof(void*), PAGE_READWRITE, &old)) {
    for (int i = 0; i < slots; ++i) {
      g_original[kind][i] = table[i];
      uint8_t* thunk = g_thunks + (kind * kMaxSlots + i) * kThunkBytes;
      MakeThunk(thunk, &g_count[kind][i], &g_original[kind][i]);
      table[i] = thunk;
    }
    VirtualProtect(table, slots * sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(),
                          g_thunks + kind * kMaxSlots * kThunkBytes,
                          slots * kThunkBytes);
    ModLog("ddraw census : the table of %s (0x%08X) counts each call",
           kKindNames[kind],
           static_cast<unsigned>(reinterpret_cast<uintptr_t>(table)));
  }
  LeaveCriticalSection(&g_lock);
}

HRESULT WINAPI h_CreateEx(GUID* driver, LPVOID* out, REFIID id,
                          IUnknown* outer) {
  const HRESULT hr = o_CreateEx(driver, out, id, outer);
  ModLog("ddraw census : the game called DirectDrawCreateEx, 0x%08lX", hr);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    const Kind kind = KindOf(id);
    if (kind == kDirectDraw) Patch(kDirectDraw, *out);
    NoteQuery(kDirectDraw, id);
  }
  return hr;
}

HRESULT WINAPI h_Create(GUID* driver, LPDIRECTDRAW* out, IUnknown* outer) {
  const HRESULT hr = o_Create(driver, out, outer);
  ModLog("ddraw census : the game called DirectDrawCreate (an old "
         "interface), 0x%08lX", hr);
  return hr;
}

void Report(const char* when) {
  char line[4096];
  for (int k = 0; k < kKinds; ++k) {
    if (g_table[k] == nullptr) continue;
    int used = 0;
    line[0] = 0;
    for (int i = 0; i < kSlotCounts[k]; ++i) {
      const LONG all = g_count[k][i];
      if (all == 0) continue;
      used += sprintf_s(line + used, sizeof(line) - used, "%s%s %ld (%llu)",
                        used == 0 ? "" : ", ", kNames[k][i], all,
                        g_walk_calls[k][i]);
      if (used > static_cast<int>(sizeof(line)) - 96) break;
    }
    ModLog("ddraw census : %s, %s: %s", when, kKindNames[k],
           used > 0 ? line : "no call");
  }
  EnterCriticalSection(&g_lock);
  for (int i = 0; i < g_asked_count; ++i) {
    const GUID& g = g_asked[i].id;
    ModLog("ddraw census : %s, QueryInterface of %s for "
           "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}: %ld",
           when, kKindNames[g_asked[i].kind], g.Data1, g.Data2, g.Data3,
           g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4],
           g.Data4[5], g.Data4[6], g.Data4[7], g_asked[i].count);
  }
  LeaveCriticalSection(&g_lock);
}

}  // namespace

void DdrawCensusInstall(int on) {
  g_on = on != 0;
  if (!g_on) return;
  InitializeCriticalSection(&g_lock);
  MH_Initialize();  // Gives MH_ERROR_ALREADY_INITIALIZED after EarlyHooks.
  g_thunks = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, kKinds * kMaxSlots * kThunkBytes,
                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  void* create_ex = ProxyRealExport("DirectDrawCreateEx");
  void* create = ProxyRealExport("DirectDrawCreate");
  bool ok = g_thunks != nullptr && create_ex != nullptr;
  if (ok) {
    ok = MH_CreateHook(create_ex, reinterpret_cast<void*>(&h_CreateEx),
                       reinterpret_cast<void**>(&o_CreateEx)) == MH_OK &&
         MH_EnableHook(create_ex) == MH_OK;
  }
  if (ok && create != nullptr &&
      MH_CreateHook(create, reinterpret_cast<void*>(&h_Create),
                    reinterpret_cast<void**>(&o_Create)) == MH_OK) {
    MH_EnableHook(create);
  }
  ModLog("ddraw census : ddraw_census=1. %s",
         ok ? "Each call of DirectDraw and Direct3D 7 counts, from the "
              "first DirectDraw object"
            : "the hook of DirectDrawCreateEx FAILED");
}

void DdrawCensusWalk(bool begin) {
  if (!g_on) return;
  // The objects of the game, from its device, when no hook of a create saw
  // them: the calls from here on count.
  if (begin && g_table[kDevice] == nullptr) {
    void** slot = reinterpret_cast<void**>(bw::render::kDirect3DDevice7);
    IDirect3DDevice7* device =
        IsBadReadPtr(slot, sizeof(void*)) ? nullptr
                                          : static_cast<IDirect3DDevice7*>(*slot);
    if (device != nullptr) {
      ModLog("ddraw census : no create was seen, so the tables come from the "
             "device of the game at the first walk");
      IDirectDrawSurface7* target = nullptr;
      IDirect3D7* d3d = nullptr;
      IUnknown* dd = nullptr;
      if (SUCCEEDED(device->GetRenderTarget(&target)) && target != nullptr) {
        if (SUCCEEDED(target->GetDDInterface(reinterpret_cast<void**>(&dd))) &&
            dd != nullptr) {
          Patch(kDirectDraw, dd);
          dd->Release();
        }
        Patch(kSurface, target);
        target->Release();
      }
      if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d != nullptr) {
        Patch(kDirect3D, d3d);
        d3d->Release();
      }
      Patch(kDevice, device);
    }
  }
  g_in_walk = begin;
  for (int k = 0; k < kKinds; ++k) {
    for (int i = 0; i < kSlotCounts[k]; ++i) {
      if (begin) {
        g_walk_start[k][i] = g_count[k][i];
      } else {
        g_walk_calls[k][i] +=
            static_cast<uint32_t>(g_count[k][i] - g_walk_start[k][i]);
      }
    }
  }
}

void DdrawCensusReport(uint64_t frames) {
  if (!g_on || frames == 0) return;
  // Each 10th report, so the log keeps its other lines in view.
  static int s_reports = 0;
  if (++s_reports % 10 != 1) return;
  Report("since the start (in the scene walks in brackets)");
}

void DdrawCensusExit() {
  if (!g_on) return;
  Report("at the end");
}
