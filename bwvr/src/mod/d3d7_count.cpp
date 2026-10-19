// A count of the calls of the Direct3D 7 device and of the DirectDraw
// surfaces in the scene walks (d3d7_count=1, a test key, docs/backend.md,
// "The calls of Direct3D 7 in a walk").
//
// The entries of the two tables of methods of dxwrapper go to a thunk of the
// mod: the thunk adds 1 to the count of its method and jumps to the function
// that the entry had. The tables are shared by each object of the class, so
// the count has each call through any object. A hook of MinHook in the
// function stays as it is. The report gives the calls of a frame in the
// walks for each method that had calls.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "d3d7_count.h"

void ModLog(const char* format, ...);

namespace {

const int kDeviceSlots = 49;
const int kSurfaceSlots = 49;
const int kSlots = kDeviceSlots + kSurfaceSlots;
const int kThunkBytes = 12;

const char* const kDeviceNames[kDeviceSlots] = {
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

const char* const kSurfaceNames[kSurfaceSlots] = {
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

bool g_on = false;
bool g_in = false;
bool g_failed = false;
volatile LONG g_count[kSlots] = {0};
void* g_original[kSlots] = {nullptr};
LONG g_walk_start[kSlots] = {0};
uint64_t g_walk_calls[kSlots] = {0};
uint64_t g_walks = 0;
uint8_t* g_thunks = nullptr;

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

bool PatchTable(void** table, int slots, int first) {
  DWORD old = 0;
  if (!VirtualProtect(table, slots * sizeof(void*), PAGE_READWRITE, &old)) {
    return false;
  }
  for (int i = 0; i < slots; ++i) {
    g_original[first + i] = table[i];
    MakeThunk(g_thunks + (first + i) * kThunkBytes, &g_count[first + i],
              &g_original[first + i]);
    table[i] = g_thunks + (first + i) * kThunkBytes;
  }
  VirtualProtect(table, slots * sizeof(void*), old, &old);
  return true;
}

bool Install() {
  void** slot = reinterpret_cast<void**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  IDirect3DDevice7* device = static_cast<IDirect3DDevice7*>(*slot);
  IDirectDrawSurface7* target = nullptr;
  if (FAILED(device->GetRenderTarget(&target)) || target == nullptr) {
    return false;
  }
  void** device_table = *reinterpret_cast<void***>(device);
  void** surface_table = *reinterpret_cast<void***>(target);
  target->Release();
  g_thunks = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, kSlots * kThunkBytes, MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (g_thunks == nullptr) return false;
  const bool ok = PatchTable(device_table, kDeviceSlots, 0) &&
                  PatchTable(surface_table, kSurfaceSlots, kDeviceSlots);
  FlushInstructionCache(GetCurrentProcess(), g_thunks, kSlots * kThunkBytes);
  ModLog("d3d7 count   : the tables of methods of the device (0x%08X) and of "
         "the surfaces (0x%08X) %s",
         static_cast<unsigned>(reinterpret_cast<uintptr_t>(device_table)),
         static_cast<unsigned>(reinterpret_cast<uintptr_t>(surface_table)),
         ok ? "count each call" : "FAILED");
  return ok;
}

}  // namespace

void D3D7CountSetOptions(int on) { g_on = on != 0; }

void D3D7CountWalk(bool begin) {
  if (!g_on) return;
  if (!g_in) {
    if (g_failed || !begin) return;
    g_in = Install();
    g_failed = !g_in;
    if (!g_in) return;
  }
  if (begin) {
    for (int i = 0; i < kSlots; ++i) g_walk_start[i] = g_count[i];
  } else {
    for (int i = 0; i < kSlots; ++i) {
      g_walk_calls[i] += static_cast<uint32_t>(g_count[i] - g_walk_start[i]);
    }
    ++g_walks;
  }
}

void D3D7CountReport(uint64_t frames) {
  if (!g_in || frames == 0) return;
  const double per = static_cast<double>(frames);
  char line[2048];
  for (int table = 0; table < 2; ++table) {
    int used = 0;
    line[0] = 0;
    const int first = table == 0 ? 0 : kDeviceSlots;
    const int slots = table == 0 ? kDeviceSlots : kSurfaceSlots;
    for (int i = 0; i < slots; ++i) {
      const uint64_t n = g_walk_calls[first + i];
      if (n == 0) continue;
      used += sprintf_s(line + used, sizeof(line) - used, "%s%s %.1f",
                        used == 0 ? "" : ", ",
                        table == 0 ? kDeviceNames[i] : kSurfaceNames[i],
                        n / per);
      if (used > static_cast<int>(sizeof(line)) - 64) break;
    }
    ModLog("d3d7 count   : %llu walks in %llu frames. The calls of the %s in "
           "the walks of a frame: %s",
           g_walks, frames, table == 0 ? "device" : "surfaces",
           used > 0 ? line : "none");
  }
  memset(g_walk_calls, 0, sizeof(g_walk_calls));
  g_walks = 0;
}
