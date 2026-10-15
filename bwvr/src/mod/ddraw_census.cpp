// A count of each call of DirectDraw and Direct3D 7 in a whole session
// (ddraw_census=1, a test key, docs/backend.md, "The calls of DirectDraw and
// Direct3D 7 in a session").
//
// WHY. A backend of the mod in place of dxwrapper must make each method that
// the game calls. This count gives the list: each method of each interface
// with a call, the count in the scene walks and outside them, and the
// interfaces that the game asks for with QueryInterface.
//
// HOW. The two exports of the proxy that make a DirectDraw object give
// it to this file (ddraw_proxy.h). Its table of methods goes to counting thunks
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
#include "../proxy/ddraw_proxy.h"

void ModLog(const char* format, ...);

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

// THE STATES. Each pair of a state and a value that a walk set, with its
// count. The D3D7 thread writes them, and the report reads them.
const int kMaxPairs = 2048;
struct Pair {
  uint32_t key;  // kind << 31 | type
  uint32_t value;
  uint32_t count;
};
Pair g_pairs[kMaxPairs];
int g_pair_count = 0;
SRWLOCK g_pair_lock = SRWLOCK_INIT;

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

QueryFn o_Query[kKinds] = {nullptr};
CreateSurfaceFn o_CreateSurface = nullptr;
CreatePaletteFn o_CreatePalette = nullptr;
CreateClipperFn o_CreateClipper = nullptr;
CreateDeviceFn o_CreateDevice = nullptr;
CreateVertexBufferFn o_CreateVertexBuffer = nullptr;
GetAttachedFn o_GetAttached = nullptr;

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

// THE SHAPES. Each kind of call that a backend must make in its own way,
// as a line of text (the flags, the caps and the format), with its count.
const int kMaxShapes = 256;
struct Shape {
  char text[200];
  LONG count;
};
Shape g_shapes[kMaxShapes];
int g_shape_count = 0;
LONG g_shapes_lost = 0;

void NoteShape(const char* text) {
  EnterCriticalSection(&g_lock);
  int i = 0;
  for (; i < g_shape_count; ++i) {
    if (strcmp(g_shapes[i].text, text) == 0) break;
  }
  if (i == g_shape_count) {
    if (g_shape_count >= kMaxShapes) {
      ++g_shapes_lost;
      LeaveCriticalSection(&g_lock);
      return;
    }
    strcpy_s(g_shapes[i].text, text);
    g_shapes[i].count = 0;
    ++g_shape_count;
  }
  ++g_shapes[i].count;
  LeaveCriticalSection(&g_lock);
}

// The caps and the format of a surface, as text.
void DescribeDesc(const DDSURFACEDESC2& d, char* out, size_t size) {
  const DDPIXELFORMAT& p = d.ddpfPixelFormat;
  sprintf_s(out, size,
            "flags 0x%lX caps 0x%lX caps2 0x%lX %lux%lu back %lu pf flags "
            "0x%lX bits %lu r %lX g %lX b %lX a %lX fourcc %lX",
            d.dwFlags, d.ddsCaps.dwCaps, d.ddsCaps.dwCaps2,
            (d.dwFlags & DDSD_WIDTH) ? d.dwWidth : 0,
            (d.dwFlags & DDSD_HEIGHT) ? d.dwHeight : 0,
            (d.dwFlags & DDSD_BACKBUFFERCOUNT) ? d.dwBackBufferCount : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwFlags : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwRGBBitCount : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwRBitMask : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwGBitMask : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwBBitMask : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwRGBAlphaBitMask : 0,
            (d.dwFlags & DDSD_PIXELFORMAT) ? p.dwFourCC : 0);
}

// The caps of a surface, as text: its own caps and its format.
void DescribeSurface(void* surface, char* out, size_t size) {
  if (surface == nullptr) {
    strcpy_s(out, size, "none");
    return;
  }
  DDSURFACEDESC2 d = {};
  d.dwSize = sizeof(d);
  if (FAILED(static_cast<IDirectDrawSurface7*>(surface)->GetSurfaceDesc(&d))) {
    strcpy_s(out, size, "no desc");
    return;
  }
  sprintf_s(out, size, "caps 0x%lX bits %lu", d.ddsCaps.dwCaps,
            d.ddpfPixelFormat.dwRGBBitCount);
}

HRESULT WINAPI h_CreateSurface(void* self, LPDDSURFACEDESC2 desc,
                               LPDIRECTDRAWSURFACE7* out, IUnknown* outer) {
  const HRESULT hr = o_CreateSurface(self, desc, out, outer);
  if (desc != nullptr) {
    char d[180], line[200];
    DescribeDesc(*desc, d, sizeof(d));
    // The size of a texture is one shape for each size class.
    sprintf_s(line, "CreateSurface %s -> %s", d, SUCCEEDED(hr) ? "ok" : "fail");
    NoteShape(line);
  }
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) Patch(kSurface, *out);
  return hr;
}

typedef HRESULT(WINAPI* BltFn)(void*, LPRECT, void*, LPRECT, DWORD,
                               LPDDBLTFX);
typedef HRESULT(WINAPI* BltFastFn)(void*, DWORD, DWORD, void*, LPRECT, DWORD);
typedef HRESULT(WINAPI* LockFn)(void*, LPRECT, LPDDSURFACEDESC2, DWORD,
                                HANDLE);
typedef HRESULT(WINAPI* FlipFn)(void*, void*, DWORD);
typedef HRESULT(WINAPI* GetDcFn)(void*, HDC*);
typedef HRESULT(WINAPI* CoopFn)(void*, HWND, DWORD);
typedef HRESULT(WINAPI* ModeFn)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
BltFn o_Blt = nullptr;
BltFastFn o_BltFast = nullptr;
LockFn o_Lock = nullptr;
FlipFn o_Flip = nullptr;
GetDcFn o_GetDc = nullptr;
CoopFn o_Coop = nullptr;
ModeFn o_Mode = nullptr;

HRESULT WINAPI h_Blt(void* self, LPRECT to, void* from, LPRECT from_rect,
                     DWORD flags, LPDDBLTFX fx) {
  char a[48], b[48], line[200];
  DescribeSurface(self, a, sizeof(a));
  DescribeSurface(from, b, sizeof(b));
  sprintf_s(line, "Blt to %s from %s flags 0x%lX rects %d %d fill %d%s", a, b,
            flags, to != nullptr, from_rect != nullptr,
            (flags & DDBLT_COLORFILL) != 0, g_in_walk ? " (walk)" : "");
  NoteShape(line);
  return o_Blt(self, to, from, from_rect, flags, fx);
}

HRESULT WINAPI h_BltFast(void* self, DWORD x, DWORD y, void* from,
                         LPRECT from_rect, DWORD flags) {
  char a[48], b[48], line[200];
  DescribeSurface(self, a, sizeof(a));
  DescribeSurface(from, b, sizeof(b));
  sprintf_s(line, "BltFast to %s from %s flags 0x%lX%s", a, b, flags,
            g_in_walk ? " (walk)" : "");
  NoteShape(line);
  return o_BltFast(self, x, y, from, from_rect, flags);
}

HRESULT WINAPI h_Lock(void* self, LPRECT rect, LPDDSURFACEDESC2 desc,
                      DWORD flags, HANDLE event) {
  char a[48], line[200];
  DescribeSurface(self, a, sizeof(a));
  sprintf_s(line, "Lock of %s flags 0x%lX rect %d%s", a, flags,
            rect != nullptr, g_in_walk ? " (walk)" : "");
  NoteShape(line);
  return o_Lock(self, rect, desc, flags, event);
}

HRESULT WINAPI h_Flip(void* self, void* to, DWORD flags) {
  char line[200];
  sprintf_s(line, "Flip flags 0x%lX to %d", flags, to != nullptr);
  NoteShape(line);
  return o_Flip(self, to, flags);
}

HRESULT WINAPI h_GetDc(void* self, HDC* dc) {
  char a[48], line[200];
  DescribeSurface(self, a, sizeof(a));
  sprintf_s(line, "GetDC of %s", a);
  NoteShape(line);
  return o_GetDc(self, dc);
}

HRESULT WINAPI h_Coop(void* self, HWND window, DWORD flags) {
  char line[200];
  sprintf_s(line, "SetCooperativeLevel flags 0x%lX", flags);
  NoteShape(line);
  return o_Coop(self, window, flags);
}

HRESULT WINAPI h_Mode(void* self, DWORD w, DWORD h, DWORD bits, DWORD rate,
                      DWORD flags) {
  char line[200];
  sprintf_s(line, "SetDisplayMode %lux%lu bits %lu rate %lu flags 0x%lX", w,
            h, bits, rate, flags);
  NoteShape(line);
  return o_Mode(self, w, h, bits, rate, flags);
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
    HookMethod(table, 20, reinterpret_cast<void*>(&h_Coop),
               reinterpret_cast<void**>(&o_Coop));
    HookMethod(table, 21, reinterpret_cast<void*>(&h_Mode),
               reinterpret_cast<void**>(&o_Mode));
  } else if (kind == kSurface) {
    HookMethod(table, 12, reinterpret_cast<void*>(&h_GetAttached),
               reinterpret_cast<void**>(&o_GetAttached));
    HookMethod(table, 5, reinterpret_cast<void*>(&h_Blt),
               reinterpret_cast<void**>(&o_Blt));
    HookMethod(table, 7, reinterpret_cast<void*>(&h_BltFast),
               reinterpret_cast<void**>(&o_BltFast));
    HookMethod(table, 11, reinterpret_cast<void*>(&h_Flip),
               reinterpret_cast<void**>(&o_Flip));
    HookMethod(table, 17, reinterpret_cast<void*>(&h_GetDc),
               reinterpret_cast<void**>(&o_GetDc));
    HookMethod(table, 25, reinterpret_cast<void*>(&h_Lock),
               reinterpret_cast<void**>(&o_Lock));
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

// QueryInterface of the IDirectDraw of DirectDrawCreate. An IDirectDraw7
// that it gives goes to the thunks.
QueryFn o_QueryOld = nullptr;
HRESULT WINAPI h_QueryOld(void* self, REFIID id, void** out) {
  const HRESULT hr = o_QueryOld(self, id, out);
  NoteQuery(kDirectDraw, id);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr &&
      KindOf(id) == kDirectDraw) {
    Patch(kDirectDraw, *out);
  }
  return hr;
}

// The two exports of the proxy that make a DirectDraw object
// (ddraw_proxy.h). The calls from the first object on count.
HRESULT CreateExOfProxy(ProxyCreateExFn real, GUID* driver, void** out,
                        REFIID id, IUnknown* outer) {
  const HRESULT hr = real(driver, out, id, outer);
  ModLog("ddraw census : the game called DirectDrawCreateEx, 0x%08lX", hr);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    const Kind kind = KindOf(id);
    if (kind == kDirectDraw) Patch(kDirectDraw, *out);
    NoteQuery(kDirectDraw, id);
  }
  return hr;
}

// DirectDrawCreate gives an IDirectDraw (the interface of DirectX 1). The
// game asks it for IDirectDraw7, and that QueryInterface patches the table
// of IDirectDraw7.
HRESULT CreateOfProxy(ProxyCreateFn real, GUID* driver, void** out,
                      IUnknown* outer) {
  const HRESULT hr = real(driver, out, outer);
  ModLog("ddraw census : the game called DirectDrawCreate (an old "
         "interface), 0x%08lX", hr);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    IUnknown* old = static_cast<IUnknown*>(*out);
    void** table = *reinterpret_cast<void***>(old);
    // Its QueryInterface: slot 0 of the table of IDirectDraw.
    static bool s_hooked = false;
    if (!s_hooked) {
      s_hooked = true;
      HookMethod(table, 0, reinterpret_cast<void*>(&h_QueryOld),
                 reinterpret_cast<void**>(&o_QueryOld));
    }
  }
  return hr;
}

// THE ANSWERS OF THE BACKEND, at the first walk: the caps of the device, the
// texture formats, the formats of the Z buffer, the identifier of the
// device and the desc of the render target. A backend of the mod gives the
// same answers.
HRESULT CALLBACK LogTextureFormat(LPDDPIXELFORMAT p, LPVOID) {
  ModLog("ddraw census : a texture format: flags 0x%lX fourcc %lX bits %lu r "
         "%lX g %lX b %lX a %lX",
         p->dwFlags, p->dwFourCC, p->dwRGBBitCount, p->dwRBitMask,
         p->dwGBitMask, p->dwBBitMask, p->dwRGBAlphaBitMask);
  return D3DENUMRET_OK;
}

HRESULT CALLBACK LogDevice(LPSTR description, LPSTR name, LPD3DDEVICEDESC7 d,
                           LPVOID) {
  ModLog("ddraw census : a device: \"%s\" \"%s\", guid %08lX, caps 0x%lX",
         description, name, d->deviceGUID.Data1, d->dwDevCaps);
  return D3DENUMRET_OK;
}

void LogBytes(const char* what, const void* data, size_t size) {
  const uint8_t* b = static_cast<const uint8_t*>(data);
  char line[1100];
  for (size_t at = 0; at < size; at += 256) {
    int used = 0;
    for (size_t i = at; i < size && i < at + 256; i += 4) {
      uint32_t v = 0;
      memcpy(&v, b + i, size - i >= 4 ? 4 : size - i);
      used += sprintf_s(line + used, sizeof(line) - used, "%08X ", v);
    }
    ModLog("ddraw census : %s +%u: %s", what, static_cast<unsigned>(at), line);
  }
}

void DumpAnswers(IDirect3DDevice7* device) {
  D3DDEVICEDESC7 caps = {};
  if (SUCCEEDED(device->GetCaps(&caps))) {
    LogBytes("D3DDEVICEDESC7", &caps, sizeof(caps));
    ModLog("ddraw census : the device caps 0x%lX, the texture caps 0x%lX, the "
           "texture filter caps 0x%lX, the max texture %lu by %lu, the stages "
           "%u, the textures %u, the src blend caps 0x%lX, the dest blend "
           "caps 0x%lX, the raster caps 0x%lX, the z compare caps 0x%lX",
           caps.dwDevCaps, caps.dpcTriCaps.dwTextureCaps,
           caps.dpcTriCaps.dwTextureFilterCaps, caps.dwMaxTextureWidth,
           caps.dwMaxTextureHeight, caps.wMaxTextureBlendStages,
           caps.wMaxSimultaneousTextures, caps.dpcTriCaps.dwSrcBlendCaps,
           caps.dpcTriCaps.dwDestBlendCaps, caps.dpcTriCaps.dwRasterCaps,
           caps.dpcTriCaps.dwZCmpCaps);
  }
  device->EnumTextureFormats(&LogTextureFormat, nullptr);
  IDirect3D7* d3d = nullptr;
  if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d != nullptr) {
    d3d->EnumDevices(&LogDevice, nullptr);
    if (caps.deviceGUID.Data1 != 0) {
      ModLog("ddraw census : the device of the game has the guid "
             "{%08lX-%04X-%04X-...}", caps.deviceGUID.Data1,
             caps.deviceGUID.Data2, caps.deviceGUID.Data3);
      d3d->EnumZBufferFormats(caps.deviceGUID, &LogTextureFormat, nullptr);
    }
    d3d->Release();
  }
  IDirectDrawSurface7* target = nullptr;
  if (SUCCEEDED(device->GetRenderTarget(&target)) && target != nullptr) {
    DDSURFACEDESC2 desc = {};
    desc.dwSize = sizeof(desc);
    if (SUCCEEDED(target->GetSurfaceDesc(&desc))) {
      char d[180];
      DescribeDesc(desc, d, sizeof(d));
      ModLog("ddraw census : the render target: %s, pitch %ld", d,
             desc.lPitch);
    }
    IDirectDraw7* dd = nullptr;
    if (SUCCEEDED(target->GetDDInterface(reinterpret_cast<void**>(&dd))) &&
        dd != nullptr) {
      DDDEVICEIDENTIFIER2 id = {};
      if (SUCCEEDED(dd->GetDeviceIdentifier(&id, 0))) {
        ModLog("ddraw census : the device identifier: \"%s\" \"%s\", vendor "
               "0x%lX device 0x%lX sub 0x%lX revision %lu, the driver "
               "version %lu.%lu",
               id.szDriver, id.szDescription, id.dwVendorId, id.dwDeviceId,
               id.dwSubSysId, id.dwRevision, id.liDriverVersion.HighPart,
               id.liDriverVersion.LowPart);
      }
      DDCAPS dd_caps = {};
      dd_caps.dwSize = sizeof(dd_caps);
      if (SUCCEEDED(dd->GetCaps(&dd_caps, nullptr))) {
        ModLog("ddraw census : the DirectDraw caps 0x%lX caps2 0x%lX, the "
               "video memory %lu of %lu bytes",
               dd_caps.dwCaps, dd_caps.dwCaps2, dd_caps.dwVidMemFree,
               dd_caps.dwVidMemTotal);
      }
      DDSURFACEDESC2 mode = {};
      mode.dwSize = sizeof(mode);
      if (SUCCEEDED(dd->GetDisplayMode(&mode))) {
        char d[180];
        DescribeDesc(mode, d, sizeof(d));
        ModLog("ddraw census : the display mode: %s", d);
      }
      dd->Release();
    }
    target->Release();
  }
}

void Report(const char* when) {
  EnterCriticalSection(&g_lock);
  for (int i = 0; i < g_shape_count; ++i) {
    ModLog("ddraw census : %s, shape: %s: %ld", when, g_shapes[i].text,
           g_shapes[i].count);
  }
  if (g_shapes_lost > 0) {
    ModLog("ddraw census : %ld calls of more shapes than %d", g_shapes_lost,
           kMaxShapes);
  }
  LeaveCriticalSection(&g_lock);
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
  const bool ok = g_thunks != nullptr;
  if (ok) {
    ProxyCreateHooks hooks = {};
    hooks.create_ex = &CreateExOfProxy;
    hooks.create = &CreateOfProxy;
    ProxySetCreateHooks(hooks);
  }
  ModLog("ddraw census : ddraw_census=1. %s",
         ok ? "Each call of DirectDraw and Direct3D 7 counts, from the "
              "first DirectDraw object that the exports of the proxy give"
            : "no room for the thunks, so nothing counts");
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
      DumpAnswers(device);
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

void DdrawCensusState(int kind, uint32_t type, uint32_t value) {
  if (!g_on) return;
  const uint32_t key = (static_cast<uint32_t>(kind) << 31) | type;
  AcquireSRWLockExclusive(&g_pair_lock);
  int i = 0;
  for (; i < g_pair_count; ++i) {
    if (g_pairs[i].key == key && g_pairs[i].value == value) break;
  }
  if (i == g_pair_count && g_pair_count < kMaxPairs) {
    g_pairs[i].key = key;
    g_pairs[i].value = value;
    g_pairs[i].count = 0;
    ++g_pair_count;
  }
  if (i < g_pair_count) ++g_pairs[i].count;
  ReleaseSRWLockExclusive(&g_pair_lock);
}

void DdrawCensusReport(uint64_t frames) {
  if (!g_on || frames == 0) return;
  // Each 10th report, so the log keeps its other lines in view.
  static int s_reports = 0;
  if (++s_reports % 10 != 1) return;
  Report("since the start (in the scene walks in brackets)");
  // The states: for each state the values that a walk set (value=count).
  AcquireSRWLockExclusive(&g_pair_lock);
  char line[2048];
  for (int kind = 0; kind < 2; ++kind) {
    for (int a = 0; a < g_pair_count; ++a) {
      const uint32_t key = g_pairs[a].key;
      if ((key >> 31) != static_cast<uint32_t>(kind)) continue;
      bool first = true;
      for (int b = 0; b < a; ++b) {
        if (g_pairs[b].key == key) first = false;
      }
      if (!first) continue;
      int used = 0;
      line[0] = 0;
      for (int b = a; b < g_pair_count; ++b) {
        if (g_pairs[b].key != key) continue;
        used += sprintf_s(line + used, sizeof(line) - used, "%s0x%X=%u",
                          used == 0 ? "" : " ", g_pairs[b].value,
                          g_pairs[b].count);
        if (used > static_cast<int>(sizeof(line)) - 40) break;
      }
      const uint32_t type = key & 0x7FFFFFFFu;
      if (kind == 0) {
        ModLog("ddraw census : render state %u: %s", type, line);
      } else {
        ModLog("ddraw census : stage %u state %u: %s", type >> 8, type & 255,
               line);
      }
    }
  }
  ReleaseSRWLockExclusive(&g_pair_lock);
}

void DdrawCensusExit() {
  if (!g_on) return;
  Report("at the end");
}
