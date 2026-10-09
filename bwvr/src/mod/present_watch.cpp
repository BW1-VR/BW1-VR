// What does it cost to put the frame on the monitor?
//
// At 1920x1080 in VR the frame takes about 20 ms, and only about 12 ms of it
// happens anywhere the mod can see. The rest sits between one scene walk and
// the next. LH3DRender::FinishFrame runs inside the scene walk, so it is not
// that. The rest is the game's own simulation and the present.
//
// The present is the interesting one, for two reasons. It is the only part
// that grows with resolution, and the picture it draws goes to a desktop
// window that nobody is looking at while the headset is on. If it is
// expensive then it is expensive for nothing.
//
// DirectDraw presents in one of two ways. A full screen game calls
// IDirectDrawSurface7::Flip to rotate the buffers. A windowed game calls
// IDirectDrawSurface7::Blt to copy the back buffer onto the primary surface,
// and that copy really does move every pixel. This game runs windowed,
// because full screen fights with the VR compositor for the display.
//
// The hooks go on the resolved function addresses rather than on one object's
// vtable, so every surface is covered, including the primary surface that the
// mod never gets a pointer to.
//
// This file needs the Direct3D 7 headers, which cannot share a file with the
// Direct3D 9 ones. See interop_probe.cpp for the same constraint.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

// IDirectDrawSurface7 vtable slots.
const int kSlotBlt = 5;
const int kSlotFlip = 11;

typedef HRESULT(WINAPI* BltFn)(IDirectDrawSurface7*, LPRECT,
                               IDirectDrawSurface7*, LPRECT, DWORD,
                               LPDDBLTFX);
typedef HRESULT(WINAPI* FlipFn)(IDirectDrawSurface7*, IDirectDrawSurface7*,
                                DWORD);

BltFn o_Blt = nullptr;
FlipFn o_Flip = nullptr;

bool g_enabled = false;
bool g_installed = false;

LARGE_INTEGER g_frequency = {0};
uint64_t g_blt_ticks = 0;
uint64_t g_blt_calls = 0;
uint64_t g_flip_ticks = 0;
uint64_t g_flip_calls = 0;

// The widest copy seen, so a full screen sized copy can be told apart from
// the many small ones the interface does.
uint32_t g_widest_blt = 0;

// SKIPPING THE DESKTOP PICTURE.
//
// Measured: the copy onto the primary surface costs 7.90 ms of a 19 ms frame
// at 1920x1080. While the headset is on, that picture is drawn for nobody.
// Skipping it is the single largest saving available, and it is safe in a way
// most optimizations are not, because the copy has no result other than what
// appears on the monitor. Nothing in the game reads the primary surface back.
//
// It only applies while the mod is actually sending frames to a headset. With
// VR off the monitor is the only output there is, so the copy stays.
bool g_skip_present = false;
uint64_t g_skipped = 0;

// Which surface is the primary one. GetCaps is not free and Blt runs several
// times a frame, so each surface is asked once and the answer is kept.
//
// The "not primary" side needs room for more than one answer. A frame blits
// to several destinations, and a single slot is evicted by each one in turn,
// so every call would miss and ask GetCaps again on the path this is meant
// to keep cheap.
const int kKnownOther = 16;
IDirectDrawSurface7* g_primary = nullptr;
IDirectDrawSurface7* g_not_primary[kKnownOther] = {nullptr};
int g_not_primary_next = 0;

bool IsPrimary(IDirectDrawSurface7* surface) {
  if (surface == nullptr) return false;
  if (surface == g_primary) return true;
  for (int i = 0; i < kKnownOther; ++i) {
    if (g_not_primary[i] == surface) return false;
  }
  DDSCAPS2 caps;
  ZeroMemory(&caps, sizeof(caps));
  if (SUCCEEDED(surface->GetCaps(&caps)) &&
      (caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0) {
    g_primary = surface;
    return true;
  }
  g_not_primary[g_not_primary_next] = surface;
  g_not_primary_next = (g_not_primary_next + 1) % kKnownOther;
  return false;
}

HRESULT WINAPI h_Blt(IDirectDrawSurface7* self, LPRECT destination,
                     IDirectDrawSurface7* source, LPRECT source_rect,
                     DWORD flags, LPDDBLTFX fx) {
  // self is the destination surface. A copy onto the primary surface is the
  // frame going to the monitor, and while the headset is on it is wasted.
  if (g_skip_present && IsPrimary(self)) {
    ++g_skipped;
    return DD_OK;
  }
  LARGE_INTEGER before, after;
  QueryPerformanceCounter(&before);
  const HRESULT hr = o_Blt(self, destination, source, source_rect, flags, fx);
  QueryPerformanceCounter(&after);
  g_blt_ticks += after.QuadPart - before.QuadPart;
  ++g_blt_calls;
  if (destination != nullptr) {
    const uint32_t width =
        static_cast<uint32_t>(destination->right - destination->left);
    if (width > g_widest_blt) g_widest_blt = width;
  }
  return hr;
}

HRESULT WINAPI h_Flip(IDirectDrawSurface7* self, IDirectDrawSurface7* target,
                      DWORD flags) {
  LARGE_INTEGER before, after;
  QueryPerformanceCounter(&before);
  const HRESULT hr = o_Flip(self, target, flags);
  QueryPerformanceCounter(&after);
  g_flip_ticks += after.QuadPart - before.QuadPart;
  ++g_flip_calls;
  return hr;
}

}  // namespace

void PresentWatchSetEnabled(bool on) { g_enabled = on; }

// Turned on once the mod is sending frames to a headset, and off again if it
// stops, so the monitor never goes dark while it is the only output.
void PresentWatchSkipDesktop(bool on) {
  if (g_skip_present == on) return;
  g_skip_present = on;
  ModLog("present      : the desktop copy is now %s",
         on ? "skipped, the monitor will not update" : "drawn again");
}

// Hooks the two ways DirectDraw puts a picture on the screen. Needs the game
// to have a render target already, so it can read the surface vtable.
void PresentWatchInstall() {
  if (!g_enabled || g_installed) return;

  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return;

  IDirectDrawSurface7* target = nullptr;
  if (FAILED((*slot)->GetRenderTarget(&target)) || target == nullptr) return;

  void** vtable = *reinterpret_cast<void***>(target);
  if (IsBadReadPtr(vtable, sizeof(void*) * (kSlotFlip + 1))) {
    target->Release();
    return;
  }

  int hooked = 0;
  void* blt = vtable[kSlotBlt];
  if (MH_CreateHook(blt, reinterpret_cast<void*>(&h_Blt),
                    reinterpret_cast<void**>(&o_Blt)) == MH_OK &&
      MH_EnableHook(blt) == MH_OK) {
    ++hooked;
  }
  void* flip = vtable[kSlotFlip];
  if (MH_CreateHook(flip, reinterpret_cast<void*>(&h_Flip),
                    reinterpret_cast<void**>(&o_Flip)) == MH_OK &&
      MH_EnableHook(flip) == MH_OK) {
    ++hooked;
  }
  target->Release();

  QueryPerformanceFrequency(&g_frequency);
  g_installed = hooked > 0;
  ModLog("present      : %d of 2 present paths hooked", hooked);
}

bool g_first_report = true;

void PresentWatchReport(uint64_t frames) {
  if (!g_installed || frames == 0 || g_frequency.QuadPart == 0) return;

  // These counters start when the hooks go in, which is during startup, and
  // they collect every blit the menus, the intro and the loading screens do.
  // The first report would divide all of that by one window of stereo frames
  // and print a number far larger than any frame. Throw that window away.
  if (g_first_report) {
    g_first_report = false;
    g_blt_ticks = 0;
    g_blt_calls = 0;
    g_flip_ticks = 0;
    g_flip_calls = 0;
    g_skipped = 0;
    return;
  }
  const double to_ms = 1000.0 / static_cast<double>(g_frequency.QuadPart);
  const double per_frame = static_cast<double>(frames);
  ModLog("present      : Blt %.2f ms over %llu calls + Flip %.2f ms over %llu "
         "calls, per frame. Widest copy %u px.",
         (static_cast<double>(g_blt_ticks) / per_frame) * to_ms,
         g_blt_calls / frames,
         (static_cast<double>(g_flip_ticks) / per_frame) * to_ms,
         g_flip_calls / frames, g_widest_blt);
  if (g_skipped > 0) {
    ModLog("present      : %llu desktop copies skipped", g_skipped);
    g_skipped = 0;
  }
  g_blt_ticks = 0;
  g_blt_calls = 0;
  g_flip_ticks = 0;
  g_flip_calls = 0;
}
