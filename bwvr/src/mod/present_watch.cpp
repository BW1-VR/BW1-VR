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
#include <intrin.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "blt_copy_math.h"
#include "frame_profile.h"
#include "render_thread.h"

extern void ModLog(const char* format, ...);
// draw_merge.cpp: the batch of the merge of draw calls goes to Direct3D
// before a copy between surfaces and before the end of the frame.
void DrawMergeFlush();
// controllers.cpp: how many frames the controllers have seen.
uint64_t ControllersFrameCount();

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

// THE FRAME RATE OF A DESKTOP TEST (the test key test_frame_rate=N).
//
// A desktop run has no headset. Its frame is the two eye passes and the copy
// to the monitor, and the copy is about 8 ms of it, so a run has 55 to 80
// frames a second. A headset session has 90, and some code of the mod can
// depend on the time of a frame. With the key, the mod does not copy the
// picture to the monitor, and each frame then lasts 1/N s at the least: the
// call of the copy waits for the rest of that time. So a desktop run has the
// frame rate of a headset. The window of the game shows no new picture in
// such a run. 0 is off. The key acts from frame kTestRateFrom of the
// controllers, so the start of a run (the Main Menu of a test save, which a
// test key closes at a frame) has the frames of each other run.
int g_test_rate = 0;
const uint64_t kTestRateFrom = 150;
LARGE_INTEGER g_pace_last = {0};
uint64_t g_paced = 0;       // frames that waited, this window
uint64_t g_pace_late = 0;   // frames that were longer than 1/N s already

// Waits until 1/N s after the copy of the frame before. The wait gives the
// rest of each time slice away and does not sleep: a sleep can last as long
// as a timer tick of Windows, which is longer than a frame.
void PaceFrame() {
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  const LONGLONG period = g_frequency.QuadPart / g_test_rate;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (g_pace_last.QuadPart == 0 || now.QuadPart - g_pace_last.QuadPart >=
                                       period) {
    if (g_pace_last.QuadPart != 0) ++g_pace_late;
    g_pace_last = now;
    return;
  }
  const LONGLONG due = g_pace_last.QuadPart + period;
  while (now.QuadPart < due) {
    SwitchToThread();
    QueryPerformanceCounter(&now);
  }
  g_pace_last.QuadPart = due;
  ++g_paced;
}

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

// THE COPY BETWEEN TWO TEXTURES (blt_texture_copy). For a Blt to or from a
// texture, m_IDirectDrawSurfaceX::CopySurface of dxwrapper calls
// D3DXLoadSurfaceFromSurface. The game makes such a copy when it makes the
// two pictures of a save (0x007923D0, the second Blt at 0x0079260C, the rect
// 0 128 128 192), between two DXT textures of 256 by 256 pixels in system
// memory. On some PCs D3DX then writes to memory that is not there,
// and the game stops with an access violation in msvcrt.dll. The first save
// of a new game is such a save. The cause in dxwrapper is not known.
//
// So the mod makes the copy itself: it locks the two surfaces with the Lock
// of the game's own texture writes and copies the pixels, also between two
// RGB formats, or the blocks of 4 by 4 pixels of two textures with one
// DXT format (blt_copy_math.h). It does this for a copy with no stretch and
// no effect, when one of the two surfaces is a texture and none is the
// primary surface. Each other copy goes to dxwrapper.
bool g_texture_copy = true;
uint64_t g_texture_copies = 0;
uint64_t g_texture_copy_fails = 0;
// The log has the first two copies of each caller, for up to 24 callers.
const int kCallerMax = 24;
void* g_callers[kCallerMax] = {nullptr};
int g_caller_lines[kCallerMax] = {0};
int g_caller_count = 0;

bool CallerMayLog(void* caller) {
  for (int i = 0; i < g_caller_count; ++i) {
    if (g_callers[i] == caller) return g_caller_lines[i]++ < 2;
  }
  if (g_caller_count >= kCallerMax) return false;
  g_callers[g_caller_count] = caller;
  g_caller_lines[g_caller_count] = 1;
  ++g_caller_count;
  return true;
}

bool DescOf(IDirectDrawSurface7* surface, DDSURFACEDESC2* desc) {
  ZeroMemory(desc, sizeof(*desc));
  desc->dwSize = sizeof(*desc);
  if (surface == nullptr || FAILED(surface->GetSurfaceDesc(desc))) {
    return false;
  }
  const DWORD need = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
  return (desc->dwFlags & need) == need;
}

blt_copy::Format FormatOf(const DDPIXELFORMAT& pf) {
  blt_copy::Format f = {0, 0, 0, 0, 0};
  if ((pf.dwFlags & DDPF_RGB) == 0 || (pf.dwFlags & DDPF_FOURCC) != 0 ||
      (pf.dwFlags & DDPF_PALETTEINDEXED8) != 0) {
    return f;
  }
  f.red = pf.dwRBitMask;
  f.green = pf.dwGBitMask;
  f.blue = pf.dwBBitMask;
  f.alpha = (pf.dwFlags & DDPF_ALPHAPIXELS) != 0 ? pf.dwRGBAlphaBitMask : 0;
  f.bytes = static_cast<int>(pf.dwRGBBitCount / 8);
  return f;
}

// The rect of a Blt, or the whole surface for no rect. False when the rect is
// empty, turned or outside the surface.
bool RectIn(const RECT* rect, const DDSURFACEDESC2& desc, RECT* out) {
  if (rect == nullptr) {
    out->left = 0;
    out->top = 0;
    out->right = static_cast<LONG>(desc.dwWidth);
    out->bottom = static_cast<LONG>(desc.dwHeight);
  } else {
    *out = *rect;
  }
  return out->left >= 0 && out->top >= 0 && out->left < out->right &&
         out->top < out->bottom &&
         out->right <= static_cast<LONG>(desc.dwWidth) &&
         out->bottom <= static_cast<LONG>(desc.dwHeight);
}

void LogCopy(const char* what, const DDSURFACEDESC2& to, const RECT& d,
             const DDSURFACEDESC2& from, const RECT& s, void* caller) {
  const DDPIXELFORMAT& a = to.ddpfPixelFormat;
  const DDPIXELFORMAT& b = from.ddpfPixelFormat;
  ModLog("present      : %s. Destination %lu x %lu, caps 0x%08lX, format "
         "0x%lX %.4s %lu bit %08lX %08lX %08lX %08lX, rect %ld %ld %ld %ld. "
         "Source %lu x %lu, caps 0x%08lX, format 0x%lX %.4s %lu bit %08lX "
         "%08lX %08lX %08lX, rect %ld %ld %ld %ld. The call is from 0x%08X",
         what, to.dwWidth, to.dwHeight, to.ddsCaps.dwCaps, a.dwFlags,
         reinterpret_cast<const char*>(&a.dwFourCC), a.dwRGBBitCount,
         a.dwRBitMask, a.dwGBitMask, a.dwBBitMask,
         a.dwRGBAlphaBitMask, d.left, d.top, d.right, d.bottom,
         from.dwWidth, from.dwHeight, from.ddsCaps.dwCaps, b.dwFlags,
         reinterpret_cast<const char*>(&b.dwFourCC), b.dwRGBBitCount,
         b.dwRBitMask, b.dwGBitMask, b.dwBBitMask,
         b.dwRGBAlphaBitMask, s.left, s.top, s.right, s.bottom,
         static_cast<unsigned>(reinterpret_cast<uintptr_t>(caller)));
}

// True when the mod made the copy, with its result in *result.
bool TextureCopy(IDirectDrawSurface7* self, const RECT* destination,
                 IDirectDrawSurface7* source, const RECT* source_rect,
                 DWORD flags, LPDDBLTFX fx, void* caller, HRESULT* result) {
  if (source == nullptr || source == self) return false;
  DDSURFACEDESC2 to, from;
  if (!DescOf(self, &to) || !DescOf(source, &from)) return false;
  if (((to.ddsCaps.dwCaps | from.ddsCaps.dwCaps) & DDSCAPS_TEXTURE) == 0 ||
      ((to.ddsCaps.dwCaps | from.ddsCaps.dwCaps) & DDSCAPS_PRIMARYSURFACE) !=
          0) {
    return false;
  }
  RECT d = {0, 0, 0, 0};
  RECT s = {0, 0, 0, 0};
  const bool fits_to = RectIn(destination, to, &d);
  const bool fits_from = RectIn(source_rect, from, &s);
  const blt_copy::Format format_to = FormatOf(to.ddpfPixelFormat);
  const blt_copy::Format format_from = FormatOf(from.ddpfPixelFormat);
  const bool plain =
      fx == nullptr &&
      (flags & ~(DDBLT_WAIT | DDBLT_ASYNC | DDBLT_DONOTWAIT)) == 0;
  const bool rgb =
      blt_copy::Usable(format_to) && blt_copy::Usable(format_from);
  // Two compressed textures with one FourCC code: the copy is of blocks.
  const DDPIXELFORMAT& pf_to = to.ddpfPixelFormat;
  const DDPIXELFORMAT& pf_from = from.ddpfPixelFormat;
  const int block_bytes =
      (pf_to.dwFlags & pf_from.dwFlags & DDPF_FOURCC) != 0 &&
              pf_to.dwFourCC == pf_from.dwFourCC
          ? blt_copy::BlockBytes(pf_to.dwFourCC)
          : 0;
  const bool blocks =
      block_bytes > 0 &&
      blt_copy::OnBlocks(d.left, d.top, d.right, d.bottom,
                         static_cast<int>(to.dwWidth),
                         static_cast<int>(to.dwHeight)) &&
      blt_copy::OnBlocks(s.left, s.top, s.right, s.bottom,
                         static_cast<int>(from.dwWidth),
                         static_cast<int>(from.dwHeight));
  const bool log = CallerMayLog(caller);
  if (!plain || !fits_to || !fits_from || !(rgb || blocks) ||
      d.right - d.left != s.right - s.left ||
      d.bottom - d.top != s.bottom - s.top) {
    if (log) {
      LogCopy(plain ? "a copy with a texture goes to dxwrapper"
                    : "a copy with a texture and an effect goes to dxwrapper",
              to, d, from, s, caller);
    }
    return false;
  }
  if (log) LogCopy("the mod copies with a texture", to, d, from, s, caller);

  DDSURFACEDESC2 locked_from, locked_to;
  ZeroMemory(&locked_from, sizeof(locked_from));
  locked_from.dwSize = sizeof(locked_from);
  ZeroMemory(&locked_to, sizeof(locked_to));
  locked_to.dwSize = sizeof(locked_to);
  HRESULT hr = source->Lock(nullptr, &locked_from,
                            DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
  if (FAILED(hr)) {
    if (log) ModLog("present      : the lock of the source failed, 0x%08lX", hr);
    ++g_texture_copy_fails;
    *result = hr;
    return true;
  }
  hr = self->Lock(nullptr, &locked_to, DDLOCK_WAIT | DDLOCK_WRITEONLY,
                  nullptr);
  if (FAILED(hr)) {
    if (log) {
      ModLog("present      : the lock of the destination failed, 0x%08lX",
             hr);
    }
    source->Unlock(nullptr);
    ++g_texture_copy_fails;
    *result = hr;
    return true;
  }
  const bool done =
      blocks
          ? blt_copy::CopyBlocks(
                static_cast<const uint8_t*>(locked_from.lpSurface),
                blt_copy::BlockPitch(static_cast<int>(locked_from.lPitch),
                                     static_cast<int>(from.dwWidth),
                                     block_bytes),
                s.left, s.top, static_cast<uint8_t*>(locked_to.lpSurface),
                blt_copy::BlockPitch(static_cast<int>(locked_to.lPitch),
                                     static_cast<int>(to.dwWidth),
                                     block_bytes),
                d.left, d.top, d.right - d.left, d.bottom - d.top,
                block_bytes)
          : blt_copy::CopyBlock(
                static_cast<const uint8_t*>(locked_from.lpSurface),
                static_cast<int>(locked_from.lPitch), s.left, s.top,
                format_from, static_cast<uint8_t*>(locked_to.lpSurface),
                static_cast<int>(locked_to.lPitch), d.left, d.top, format_to,
                d.right - d.left, d.bottom - d.top);
  self->Unlock(nullptr);
  source->Unlock(nullptr);
  if (done) {
    ++g_texture_copies;
    *result = DD_OK;
  } else {
    ++g_texture_copy_fails;
    *result = DDERR_GENERIC;
  }
  return true;
}

HRESULT WINAPI h_Blt(IDirectDrawSurface7* self, LPRECT destination,
                     IDirectDrawSurface7* source, LPRECT source_rect,
                     DWORD flags, LPDDBLTFX fx) {
  DrawMergeFlush();
  RenderThreadSync();
  HRESULT copied = DD_OK;
  if (g_texture_copy && TextureCopy(self, destination, source, source_rect,
                                    flags, fx, _ReturnAddress(), &copied)) {
    return copied;
  }
  // self is the destination surface. A copy onto the primary surface is the
  // frame going to the monitor, and while the headset is on it is wasted.
  if (g_skip_present && IsPrimary(self)) {
    ++g_skipped;
    return DD_OK;
  }
  // A desktop test at the frame rate of a headset: no copy, and the frame
  // lasts 1/N s at the least.
  if (g_test_rate > 0 && ControllersFrameCount() >= kTestRateFrom &&
      IsPrimary(self)) {
    ++g_skipped;
    PaceFrame();
    return DD_OK;
  }
  LARGE_INTEGER before, after;
  QueryPerformanceCounter(&before);
  const HRESULT hr = o_Blt(self, destination, source, source_rect, flags, fx);
  QueryPerformanceCounter(&after);
  g_blt_ticks += after.QuadPart - before.QuadPart;
  ++g_blt_calls;
  // The copy to the monitor is in the time of GGame::FlipScreen.
  if (FrameProfileOn() && !IsPrimary(self)) {
    FrameProfileAdd(kFpCopy, after.QuadPart - before.QuadPart);
    FrameProfileCount(kFpcCopies, 1);
    FrameProfileCaller(kFpCallCopy, _ReturnAddress(), 0,
                       after.QuadPart - before.QuadPart,
                       _AddressOfReturnAddress());
  }
  if (destination != nullptr) {
    const uint32_t width =
        static_cast<uint32_t>(destination->right - destination->left);
    if (width > g_widest_blt) g_widest_blt = width;
  }
  return hr;
}

HRESULT WINAPI h_Flip(IDirectDrawSurface7* self, IDirectDrawSurface7* target,
                      DWORD flags) {
  DrawMergeFlush();
  RenderThreadSync();
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
  // The keys blt_texture_copy and test_frame_rate, from bwvr.ini beside the
  // game.
  {
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\')
                                                  : nullptr;
    if (slash != nullptr && (slash - path) + 10 < MAX_PATH) {
      slash[1] = 0;
      strcat_s(path, "bwvr.ini");
      g_test_rate = static_cast<int>(
          GetPrivateProfileIntA("bwvr", "test_frame_rate", 0, path));
      g_texture_copy =
          GetPrivateProfileIntA("bwvr", "blt_texture_copy", 1, path) != 0;
    }
    ModLog("present      : blt_texture_copy=%d. %s", g_texture_copy ? 1 : 0,
           g_texture_copy
               ? "The mod makes a copy with a texture and no stretch itself, "
                 "and not dxwrapper"
               : "Each copy between surfaces goes to dxwrapper");
    if (g_test_rate < 20 || g_test_rate > 240) g_test_rate = 0;
    if (g_test_rate > 0) {
      ModLog("present      : test_frame_rate=%d, a test key of the desktop "
             "runs. The mod does not copy the picture to the monitor, and "
             "each frame lasts %.2f ms at the least, from frame %llu of the "
             "controllers. So the run has the frame rate of a headset. The "
             "window of the game shows no new picture", g_test_rate,
             1000.0 / g_test_rate, kTestRateFrom);
    }
  }
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
  if (g_texture_copies + g_texture_copy_fails > 0) {
    ModLog("present      : the mod made %llu copies between two textures, "
           "and %llu failed", g_texture_copies, g_texture_copy_fails);
    g_texture_copies = 0;
    g_texture_copy_fails = 0;
  }
  if (g_test_rate > 0) {
    ModLog("present      : test_frame_rate=%d: %llu frames waited for their "
           "time of %.2f ms, and %llu frames were longer than that",
           g_test_rate, g_paced, 1000.0 / g_test_rate, g_pace_late);
    g_paced = g_pace_late = 0;
  }
  g_blt_ticks = 0;
  g_blt_calls = 0;
  g_flip_ticks = 0;
  g_flip_calls = 0;
}
